#include "strings.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "llvm/ADT/APInt.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/Support/Casting.h"

namespace cs2::protect {

namespace {

// --- The names ---------------------------------------------------------------

/// The name of the global the payload publishes its image base in.
///
/// The name is fixed and has C linkage on both sides: the pass *declares* it
/// in every module that decodes a string, and the payload *defines* it. If
/// the payload does not define it, the link fails loudly -- that is the
/// right failure, because a protected build that decodes garbage is a wrong
/// behaviour, not a crash.
constexpr std::string_view k_instance_base_name = "cs2_protect_instance_base";

/// The name of the decode helper.
constexpr std::string_view k_decode_name = "cs2.protect.strdecode";

/// The names of the two constants of the build seed.
constexpr std::string_view k_seed0_name = "cs2.protect.strseed0";
constexpr std::string_view k_seed1_name = "cs2.protect.strseed1";

/// The names of the constants of the mixer.
///
/// They live in globals and not in the instruction stream: the X86 backend
/// of the SDK's LLVM 22.1.8 mis-materialises a 64-bit immediate in this shape
/// (see `tools/protect/README.md` §12), and a constant that reaches the image
/// as a different value is a wrong key, which is a wrong plaintext.
constexpr std::string_view k_multiplier_name = "cs2.protect.strmul";
constexpr std::string_view k_xor_name = "cs2.protect.strxor";

/// The prefix of the names of the blobs.
constexpr std::string_view k_blob_prefix = "cs2.protect.strblob.";

// --- The key -----------------------------------------------------------------

/// The key of one string, at build time.
///
/// The key is the build seed (the two constants of the module) mixed with the
/// string's index. It does **not** depend on the instance base: the
/// ciphertext is baked into the image, so the key that decrypts it has to be
/// computable at build time. The instance base is mixed into the *runtime*
/// key and removed from it again (see the decode helper), because a
/// per-injection value cannot key a value that is already in the file.
[[nodiscard]] auto build_key(std::uint64_t seed0, std::uint64_t seed1, std::uint32_t index) noexcept
    -> std::uint64_t {
  return mix64(seed0 ^ seed1 ^ static_cast<std::uint64_t>(index));
}

/// The byte of the key stream at a position.
///
/// The stream is a per-byte mix: the ciphertext is not the plaintext under
/// any single build-time constant, and the bytes of two strings do not
/// cancel each other.
[[nodiscard]] auto stream_byte(std::uint64_t key, std::size_t position) noexcept -> std::uint8_t {
  const std::uint64_t mixed = mix64(key + static_cast<std::uint64_t>(position));
  return static_cast<std::uint8_t>((mixed >> 56U) & 0xFFU);
}

// --- The uses ----------------------------------------------------------------

/// Whether a global is a literal the pass may replace.
///
/// A string literal is `private` in the Itanium ABI, but the MSVC ABI (the
/// one the payload is built with) gives it `linkonce_odr` linkage, so that
/// the linker can fold the identical strings of two translation units. The
/// ODR-safe linkages are therefore accepted as well: the literal is deleted
/// and replaced by a private blob, and every translation unit that uses the
/// string protects it as well, so no reference to the deleted symbol is left
/// behind.
[[nodiscard]] auto is_protectable_linkage(const llvm::GlobalVariable& literal) -> bool {
  return literal.hasPrivateLinkage() || literal.hasInternalLinkage() ||
         literal.hasLinkOnceODRLinkage() || literal.hasWeakODRLinkage();
}

/// The bytes of a literal, if it is a blob of bytes.
///
/// Two shapes carry bytes: a `ConstantDataSequential` of `i8`, and a
/// `ConstantAggregateZero` of `i8` (an all-zero blob: the reader folds
/// `c"\00"` into it, so the *empty* literal has that shape).
[[nodiscard]] auto literal_bytes(const llvm::GlobalVariable& literal)
    -> std::optional<std::vector<std::uint8_t>> {
  const llvm::Constant* initializer = literal.getInitializer();
  std::vector<std::uint8_t> bytes;
  if (const auto* data = llvm::dyn_cast<llvm::ConstantDataSequential>(initializer)) {
    if (!data->getElementType()->isIntegerTy(8)) {
      return std::nullopt;
    }
    bytes.reserve(data->getNumElements());
    for (unsigned index = 0; index < data->getNumElements(); ++index) {
      const auto* element = llvm::dyn_cast<llvm::ConstantInt>(data->getElementAsConstant(index));
      if (element == nullptr) {
        return std::nullopt;
      }
      bytes.push_back(static_cast<std::uint8_t>(element->getValue().getZExtValue() & 0xFFU));
    }
    return bytes;
  }
  if (const auto* zero = llvm::dyn_cast<llvm::ConstantAggregateZero>(initializer)) {
    const auto* array = llvm::dyn_cast<llvm::ArrayType>(zero->getType());
    if (array == nullptr || !array->getElementType()->isIntegerTy(8)) {
      return std::nullopt;
    }
    bytes.assign(array->getNumElements(), 0U);
    return bytes;
  }
  return std::nullopt;
}

/// One use of a literal.
struct UseSite {
  /// The instruction that uses the literal.
  llvm::Instruction* instruction{nullptr};
  /// The operand of the instruction that holds the literal.
  unsigned operand{0};
  /// Whether the operand is a constant expression (a folded GEP).
  bool through_expression{false};
  /// The byte offset inside the literal, for a use through an expression.
  std::uint64_t offset{0};
  /// Whether the expression promised `inbounds`.
  bool in_bounds{false};
  /// The function the use is in.
  llvm::Function* function{nullptr};
  /// Whether the use is one the pass **cannot** rewrite.
  ///
  /// A constant expression the pass does not understand -- a `ptrtoint`, an
  /// `inttoptr`, a `select`, a GEP of a GEP -- is a use like any other: the
  /// gate has to **refuse** the literal, not skip the use. A skipped use
  /// would let the pass delete the literal while the expression still
  /// refers to it, and the X86 backend dies on the dangling reference
  /// ("Cannot select: ... PLEASE submit a bug report").
  bool unsupported{false};
};

/// Whether the address of a constant ends up in a global initializer.
///
/// A `constexpr` array of `string_view` bakes the pointers of the literals
/// into a global initializer, and a pointer to a decoded buffer cannot exist
/// before the program runs. That is the `KNOWN_EVENTS` case of the payload.
[[nodiscard]] auto constant_escapes(const llvm::Constant& constant) -> bool {
  for (const llvm::User* user : constant.users()) {
    if (llvm::isa<llvm::Instruction>(user)) {
      continue;  // a use in code
    }
    if (const auto* expression = llvm::dyn_cast<llvm::ConstantExpr>(user)) {
      if (constant_escapes(*expression)) {
        return true;
      }
      continue;
    }
    // Any other constant user is a piece of a global initializer.
    return true;
  }
  return false;
}

/// The byte offset of a constant GEP inside the literal it points into.
struct ExpressionOffset {
  std::uint64_t offset{0};
  bool in_bounds{false};
};

[[nodiscard]] auto expression_offset(const llvm::ConstantExpr& expression,
                                     const llvm::DataLayout& layout)
    -> std::optional<ExpressionOffset> {
  const auto* gep = llvm::dyn_cast<llvm::GEPOperator>(&expression);
  if (gep == nullptr) {
    return std::nullopt;
  }
  llvm::APInt offset(64, 0);
  if (!gep->accumulateConstantOffset(layout, offset)) {
    return std::nullopt;
  }
  if (offset.isNegative()) {
    return std::nullopt;
  }
  return ExpressionOffset{offset.getZExtValue(), gep->isInBounds()};
}

/// The use sites of a literal, in the order of the module's use list.
///
/// Every user of the literal is a use. A user the pass cannot rewrite is
/// reported as an **unsupported** use, and the gate refuses the literal
/// because of it: the pass never skips a use, because a skipped use leaves
/// the literal deleted and the expression that referred to it dangling.
[[nodiscard]] auto use_sites(llvm::GlobalVariable& literal, const llvm::DataLayout& layout)
    -> std::vector<UseSite> {
  std::vector<UseSite> sites;
  for (llvm::User* user : literal.users()) {
    if (auto* instruction = llvm::dyn_cast<llvm::Instruction>(user)) {
      for (unsigned index = 0; index < instruction->getNumOperands(); ++index) {
        if (instruction->getOperand(index) != &literal) {
          continue;
        }
        sites.push_back(UseSite{instruction, index, false, 0, false, instruction->getFunction(),
                                false});
      }
      continue;
    }
    // A constant expression: a folded GEP or a bitcast.
    auto* expression = llvm::dyn_cast<llvm::ConstantExpr>(user);
    if (expression == nullptr) {
      // Neither an instruction nor a constant expression: a piece of a
      // global initializer. The gate refuses such a literal before this
      // matters, and the use is reported so that the gate cannot pass a
      // literal it cannot rewrite.
      sites.push_back(UseSite{nullptr, 0, false, 0, false, nullptr, true});
      continue;
    }
    const auto offset = expression_offset(*expression, layout);
    if (!offset.has_value()) {
      // The expression is not a GEP the pass can rewrite: a `ptrtoint`, an
      // `inttoptr`, a `select`, a GEP with a variable index. The literal is
      // refused.
      sites.push_back(UseSite{nullptr, 0, false, 0, false, nullptr, true});
      continue;
    }
    for (llvm::User* nested : expression->users()) {
      auto* instruction = llvm::dyn_cast<llvm::Instruction>(nested);
      if (instruction == nullptr) {
        // A GEP of a GEP, or a GEP that reaches a global initializer: the
        // pass cannot rewrite it, and the offset it would compute is
        // relative to the nested expression and not to the literal.
        sites.push_back(UseSite{nullptr, 0, false, 0, false, nullptr, true});
        continue;
      }
      for (unsigned index = 0; index < instruction->getNumOperands(); ++index) {
        if (instruction->getOperand(index) != expression) {
          continue;
        }
        sites.push_back(UseSite{instruction, index, true, offset->offset, offset->in_bounds,
                                instruction->getFunction(), false});
      }
    }
  }
  return sites;
}

/// Whether a function has an EH pad.
///
/// The rewritten IR has to stay exception-free: the decode is a call, and a
/// call in a function with EH pads would need an `invoke` and an EH edge.
[[nodiscard]] auto has_eh_pads(const llvm::Function& function) -> bool {
  for (const llvm::BasicBlock& block : function) {
    if (block.isEHPad()) {
      return true;
    }
  }
  return false;
}

/// The block that dominates every use of a literal in a function.
///
/// The decode has to run **before every use**: a use that reads the buffer
/// before the decode filled it reads an uninitialised stack slot, which is
/// a *wrong string* and not a crash -- the failure the specification calls
/// out. The block that dominates every use is the nearest common dominator
/// of the blocks of the uses, and the decode goes at its **top**, so that
/// it is before the uses of the block itself as well.
///
/// The entry block is the fallback: it dominates every block of the
/// function, so a decode at its top is before every use.
///
/// `Instruction::comesBefore` is **not** the tool for this: the LLVM
/// header defines it only *within one basic block*, so it answers "false"
/// for two instructions of two blocks and the selection degenerates into
/// "the first use of the use list".
[[nodiscard]] auto decode_block(llvm::Function& function, const std::vector<UseSite>& sites)
    -> llvm::BasicBlock* {
  llvm::BasicBlock* entry = &function.getEntryBlock();
  if (sites.empty()) {
    return entry;
  }
  llvm::DominatorTree tree(function);
  llvm::BasicBlock* result = nullptr;
  for (const UseSite& site : sites) {
    llvm::BasicBlock* block = site.instruction->getParent();
    if (tree.getNode(block) == nullptr) {
      // A block that is not in the tree is not reachable from the entry: the
      // entry block is the safe answer.
      return entry;
    }
    result = result == nullptr ? block : tree.findNearestCommonDominator(result, block);
  }
  return result == nullptr ? entry : result;
}

/// Whether a function is selected at the `virtualized` level.
///
/// The VM's blob format and the string blob format must not meet: the two
/// transforms write different things into the same module.
[[nodiscard]] auto is_virtualized(const llvm::Function& function, const RecordedSelection& selection)
    -> bool {
  for (const RecordedFunction& recorded : selection.functions) {
    if (recorded.symbol == function.getName().str() &&
        recorded.level == ProtectionLevel::virtualized) {
      return true;
    }
  }
  return false;
}

/// Whether a use is a plain pointer use.
///
/// A plain pointer use reads the bytes of the literal, or passes its address
/// to a call. A use that *compares* the address, or *stores* it in a
/// structure, or lets it escape the function, is not: the buffer lives in
/// the stack frame of the function, and the address of a decoded buffer
/// cannot outlive it.
[[nodiscard]] auto is_plain_pointer_use(const UseSite& site) -> bool {
  const llvm::Instruction* instruction = site.instruction;
  if (const auto* load = llvm::dyn_cast<llvm::LoadInst>(instruction)) {
    return load->getPointerOperandIndex() == site.operand;
  }
  if (const auto* gep = llvm::dyn_cast<llvm::GetElementPtrInst>(instruction)) {
    return gep->getPointerOperandIndex() == site.operand;
  }
  if (const auto* call = llvm::dyn_cast<llvm::CallBase>(instruction)) {
    return call->getArgOperand(site.operand)->getType()->isPointerTy();
  }
  if (const auto* copy = llvm::dyn_cast<llvm::MemTransferInst>(instruction)) {
    return copy->getArgOperand(0) == instruction->getOperand(site.operand);
  }
  return false;
}

// --- The decode helper -------------------------------------------------------

/// The globals the decode helper reads.
struct Decoder {
  llvm::Function* function{nullptr};
  llvm::GlobalVariable* seed0{nullptr};
  llvm::GlobalVariable* seed1{nullptr};
  llvm::GlobalVariable* multiplier{nullptr};
  llvm::GlobalVariable* xor_constant{nullptr};
  llvm::GlobalVariable* instance_base{nullptr};
};

/// Creates the decode helper of the module.
///
/// The helper is emitted as IR: no CRT call, no `memcpy`, no `memset`. The
/// loop writes the plaintext byte by byte.
///
/// The loads of the seeds, of the constants of the mixer and of the blob are
/// **volatile**, and that is load-bearing: the whole decode is a compile-time
/// constant once the seeds are known, so a compiler that folds it would
/// constant-fold the *plaintext* into the image -- which is exactly what this
/// task exists to prevent. A volatile load is never folded, and it keeps the
/// image read-only.
[[nodiscard]] auto make_decoder(llvm::Module& module, std::uint64_t seed0_value,
                                std::uint64_t seed1_value) -> Decoder {
  llvm::LLVMContext& context = module.getContext();
  llvm::Type* i8_type = llvm::Type::getInt8Ty(context);
  llvm::Type* i32_type = llvm::Type::getInt32Ty(context);
  llvm::Type* i64_type = llvm::Type::getInt64Ty(context);
  llvm::Type* pointer_type = llvm::PointerType::getUnqual(context);

  Decoder decoder;

  decoder.seed0 = new llvm::GlobalVariable(module, i64_type, /*isConstant=*/true,
                                            llvm::GlobalValue::PrivateLinkage,
                                            llvm::ConstantInt::get(i64_type, seed0_value),
                                            k_seed0_name);
  decoder.seed1 = new llvm::GlobalVariable(module, i64_type, /*isConstant=*/true,
                                            llvm::GlobalValue::PrivateLinkage,
                                            llvm::ConstantInt::get(i64_type, seed1_value),
                                            k_seed1_name);
  decoder.multiplier = new llvm::GlobalVariable(module, i64_type, /*isConstant=*/true,
                                                 llvm::GlobalValue::PrivateLinkage,
                                                 llvm::ConstantInt::get(i64_type, k_mix_multiplier),
                                                 k_multiplier_name);
  decoder.xor_constant = new llvm::GlobalVariable(module, i64_type, /*isConstant=*/true,
                                                   llvm::GlobalValue::PrivateLinkage,
                                                   llvm::ConstantInt::get(i64_type, k_mix_xor),
                                                   k_xor_name);
  for (llvm::GlobalVariable* global :
       {decoder.seed0, decoder.seed1, decoder.multiplier, decoder.xor_constant}) {
    global->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
  }

  // The instance base: an external symbol the payload defines. The pass only
  // declares it, so a protected build that does not define it fails to link
  // -- loudly, and at link time, which is the right moment.
  decoder.instance_base = module.getGlobalVariable(k_instance_base_name);
  if (decoder.instance_base == nullptr) {
    decoder.instance_base = new llvm::GlobalVariable(module, i64_type, /*isConstant=*/false,
                                                      llvm::GlobalValue::ExternalLinkage, nullptr,
                                                      k_instance_base_name);
  }

  llvm::FunctionType* signature = llvm::FunctionType::get(
      llvm::Type::getVoidTy(context), {pointer_type, i32_type, i32_type, pointer_type}, false);
  llvm::Function* function = llvm::Function::Create(signature, llvm::GlobalValue::InternalLinkage,
                                                    k_decode_name, module);
  // The helper must not be inlined: an inlined helper would let the
  // optimiser see the blob as a constant and fold the plaintext into the
  // instruction stream.
  function->addFnAttr(llvm::Attribute::NoInline);
  decoder.function = function;

  llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", function);
  llvm::BasicBlock* loop = llvm::BasicBlock::Create(context, "loop", function);
  llvm::BasicBlock* exit = llvm::BasicBlock::Create(context, "exit", function);

  llvm::IRBuilder<> builder(entry);
  llvm::Value* seed0 = builder.CreateLoad(i64_type, decoder.seed0, /*isVolatile=*/true, "seed0");
  llvm::Value* seed1 = builder.CreateLoad(i64_type, decoder.seed1, /*isVolatile=*/true, "seed1");
  llvm::Value* base =
      builder.CreateLoad(i64_type, decoder.instance_base, /*isVolatile=*/true, "base");
  llvm::Value* index = builder.CreateZExt(function->getArg(2), i64_type, "index64");
  llvm::Value* mixed = builder.CreateXor(seed0, seed1, "keymix0");
  mixed = builder.CreateXor(mixed, index, "keymix1");
  llvm::Value* multiplier =
      builder.CreateLoad(i64_type, decoder.multiplier, /*isVolatile=*/true, "multiplier");
  llvm::Value* product = builder.CreateMul(mixed, multiplier, "keyproduct");
  llvm::Value* xor_constant =
      builder.CreateLoad(i64_type, decoder.xor_constant, /*isVolatile=*/true, "xorconstant");
  llvm::Value* xored = builder.CreateXor(product, xor_constant, "keyxor");
  llvm::Value* low = builder.CreateShl(xored, k_mix_rotate, "keylow");
  llvm::Value* high = builder.CreateLShr(xored, 64U - k_mix_rotate, "keyhigh");
  llvm::Value* rotated = builder.CreateOr(low, high, "keyrotated");
  // The runtime key: the build key with the instance base mixed in. The base
  // is a per-injection value, and a value that is already in the file cannot
  // be keyed by it -- so the base is mixed in and taken out again, and the
  // decode is written against the runtime key.
  llvm::Value* runtime_key = builder.CreateXor(rotated, base, "runtime_key");
  llvm::Value* stream_key = builder.CreateXor(runtime_key, base, "stream_key");
  builder.CreateBr(loop);

  llvm::IRBuilder<> loop_builder(loop);
  llvm::PHINode* counter = loop_builder.CreatePHI(i32_type, 2, "position");
  counter->addIncoming(llvm::ConstantInt::get(i32_type, 0), entry);
  llvm::Value* blob_address =
      loop_builder.CreateGEP(i8_type, function->getArg(0), counter, "blob_at");
  llvm::Value* cipher =
      loop_builder.CreateLoad(i8_type, blob_address, /*isVolatile=*/true, "cipher");
  llvm::Value* position = loop_builder.CreateZExt(counter, i64_type, "position64");
  llvm::Value* stream_position = loop_builder.CreateAdd(stream_key, position, "stream_position");
  llvm::Value* stream_product = loop_builder.CreateMul(stream_position, multiplier, "stream_product");
  llvm::Value* stream_xored = loop_builder.CreateXor(stream_product, xor_constant, "stream_xor");
  llvm::Value* stream_low = loop_builder.CreateShl(stream_xored, k_mix_rotate);
  llvm::Value* stream_high = loop_builder.CreateLShr(stream_xored, 64U - k_mix_rotate);
  llvm::Value* stream = loop_builder.CreateOr(stream_low, stream_high, "stream");
  llvm::Value* key_byte = loop_builder.CreateTrunc(loop_builder.CreateLShr(stream, 56), i8_type);
  llvm::Value* plain = loop_builder.CreateXor(cipher, key_byte, "plain");
  llvm::Value* out_address = loop_builder.CreateGEP(i8_type, function->getArg(3), counter, "out_at");
  loop_builder.CreateStore(plain, out_address);
  llvm::Value* next = loop_builder.CreateAdd(counter, llvm::ConstantInt::get(i32_type, 1), "next");
  llvm::Value* done = loop_builder.CreateICmpUGE(next, function->getArg(1), "done");
  loop_builder.CreateCondBr(done, exit, loop);
  counter->addIncoming(next, loop);

  llvm::IRBuilder<> exit_builder(exit);
  exit_builder.CreateRetVoid();

  return decoder;
}

}  // namespace

[[nodiscard]] auto to_string(StringSkipReason reason) noexcept -> const char* {
  switch (reason) {
    case StringSkipReason::escapes_to_initializer:
      return "escapes_to_initializer";
    case StringSkipReason::unsupported_shape:
      return "unsupported_shape";
    case StringSkipReason::empty:
      return "empty";
    case StringSkipReason::too_short:
      return "too_short";
    case StringSkipReason::optnone_function:
      return "optnone_function";
    case StringSkipReason::virtualized_function:
      return "virtualized_function";
    case StringSkipReason::eh_use:
      return "eh_use";
    case StringSkipReason::unsupported_use:
      return "unsupported_use";
    default:
      return "unknown";
  }
}

[[nodiscard]] auto string_selected(const Policy& policy, std::string_view content) noexcept -> bool {
  bool selected = false;
  for (const std::string& pattern : policy.strings) {
    if (glob_match(pattern, content)) {
      selected = true;
      break;
    }
  }
  if (!selected) {
    return false;
  }
  for (const std::string& pattern : policy.strings_deny) {
    if (glob_match(pattern, content)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] auto string_content(const llvm::GlobalVariable& literal) -> std::optional<std::string> {
  const auto bytes = literal_bytes(literal);
  if (!bytes.has_value()) {
    return std::nullopt;
  }
  std::string content(bytes->begin(), bytes->end());
  // A C string carries a trailing NUL: it is not part of the content, but it
  // is part of the literal, and the blob keeps it.
  if (!content.empty() && content.back() == '\0') {
    content.pop_back();
  }
  return content;
}

[[nodiscard]] auto string_gate(llvm::GlobalVariable& literal, const Config& config,
                               const RecordedSelection& selection)
    -> std::optional<StringSkipReason> {
  // The address of the literal in a global initializer: a pointer to a
  // decoded buffer cannot exist before the program runs.
  if (constant_escapes(literal)) {
    return StringSkipReason::escapes_to_initializer;
  }

  // The shape: a `ConstantDataSequential` of `i8`, in a global the pass may
  // replace.
  if (!is_protectable_linkage(literal) || !literal.isConstant()) {
    return StringSkipReason::unsupported_shape;
  }
  const auto content = string_content(literal);
  if (!content.has_value()) {
    return StringSkipReason::unsupported_shape;
  }
  if (content->empty()) {
    return StringSkipReason::empty;
  }

  // The minimum length: a three-byte string carries too little to be worth
  // a decode.
  const std::size_t minimum =
      config.policy.has_value() ? config.policy->strings_min_length : std::size_t{4};
  if (content->size() < minimum) {
    return StringSkipReason::too_short;
  }

  // The users: every one of them has to be a plain pointer use in a function
  // the pass may rewrite.
  const llvm::DataLayout& layout = literal.getParent()->getDataLayout();
  for (const UseSite& site : use_sites(literal, layout)) {
    llvm::Function* function = site.function;
    if (site.unsupported || function == nullptr) {
      // A use the pass cannot rewrite: the literal is refused, and it is
      // left exactly as it is.
      return StringSkipReason::unsupported_use;
    }
    if (function->hasFnAttribute(llvm::Attribute::OptimizeNone)) {
      return StringSkipReason::optnone_function;
    }
    if (is_virtualized(*function, selection)) {
      return StringSkipReason::virtualized_function;
    }
    if (has_eh_pads(*function)) {
      return StringSkipReason::eh_use;
    }
    if (!is_plain_pointer_use(site)) {
      return StringSkipReason::unsupported_use;
    }
  }
  return std::nullopt;
}

[[nodiscard]] auto transform_strings(llvm::Module& module, const Config& config,
                                     const RecordedSelection& selection) -> StringResult {
  StringResult result;
  if (!config.policy.has_value()) {
    return result;
  }
  const Policy& policy = *config.policy;
  if (policy.strings.empty()) {
    // Nothing is protected unless the policy says so.
    return result;
  }

  llvm::LLVMContext& context = module.getContext();
  llvm::Type* i8_type = llvm::Type::getInt8Ty(context);
  llvm::Type* i32_type = llvm::Type::getInt32Ty(context);
  llvm::Type* i64_type = llvm::Type::getInt64Ty(context);
  const llvm::DataLayout& layout = module.getDataLayout();

  // The candidates: the local constants of the module.
  std::vector<llvm::GlobalVariable*> candidates;
  for (llvm::GlobalVariable& global : module.globals()) {
    if (!is_protectable_linkage(global) || !global.isConstant()) {
      continue;
    }
    candidates.push_back(&global);
  }

  // The selected literals, in a stable order: the index of a literal is its
  // position in this list, and it is baked into the decode, so it has to be
  // the same in the build and in the image.
  struct Candidate {
    llvm::GlobalVariable* global{nullptr};
    std::string content;
  };
  std::vector<Candidate> selected;
  for (llvm::GlobalVariable* global : candidates) {
    const auto content = string_content(*global);
    if (!content.has_value()) {
      // No content: the policy cannot select it, so it is not even looked at.
      continue;
    }
    if (!string_selected(policy, *content)) {
      continue;
    }
    selected.push_back(Candidate{global, *content});
  }
  std::ranges::sort(selected, [](const Candidate& first, const Candidate& second) {
    if (first.content != second.content) {
      return first.content < second.content;
    }
    return first.global->getName() < second.global->getName();
  });

  // The gate: every selected literal is protected or refused, and the
  // verdict is reported.
  std::vector<std::optional<StringSkipReason>> verdicts;
  verdicts.reserve(selected.size());
  for (const Candidate& candidate : selected) {
    verdicts.push_back(string_gate(*candidate.global, config, selection));
  }

  // The seeds of the module: the build seed, split in two constants.
  std::uint64_t state = module_seed(config, module.getSourceFileName());
  const std::uint64_t seed0 = splitmix64(state);
  const std::uint64_t seed1 = splitmix64(state);

  Decoder decoder;
  std::uint32_t index = 0;

  for (std::size_t position = 0; position < selected.size(); ++position) {
    const Candidate& candidate = selected[position];
    StringLiteral literal;
    literal.content = candidate.content;
    literal.length = candidate.content.size();
    if (verdicts[position].has_value()) {
      // Refused: the literal is left exactly as it is.
      literal.reason = *verdicts[position];
      ++result.refused_count;
      result.literals.push_back(std::move(literal));
      continue;
    }

    // The bytes of the literal, with the trailing NUL of a C string.
    const auto bytes = literal_bytes(*candidate.global);
    if (!bytes.has_value()) {
      // The gate passed, so the literal has bytes: this cannot happen.
      literal.reason = StringSkipReason::unsupported_shape;
      ++result.refused_count;
      result.literals.push_back(std::move(literal));
      continue;
    }
    const std::size_t size = bytes->size();

    // The ciphertext: the plaintext XORed with the key stream.
    const std::uint64_t key = build_key(seed0, seed1, index);
    std::vector<std::uint8_t> cipher;
    cipher.reserve(size);
    for (std::size_t byte = 0; byte < size; ++byte) {
      cipher.push_back(static_cast<std::uint8_t>((*bytes)[byte] ^ stream_byte(key, byte)));
    }

    if (decoder.function == nullptr) {
      decoder = make_decoder(module, seed0, seed1);
    }

    // The blob: the same type as the literal it replaces, so that the
    // pointer arithmetic of the users does not change.
    llvm::ArrayType* blob_type = llvm::ArrayType::get(i8_type, size);
    llvm::Constant* blob_initializer = llvm::ConstantDataArray::getRaw(
        llvm::StringRef(reinterpret_cast<const char*>(cipher.data()), cipher.size()), size, i8_type);
    llvm::GlobalVariable* blob = new llvm::GlobalVariable(
        module, blob_type, /*isConstant=*/true, llvm::GlobalValue::PrivateLinkage, blob_initializer,
        std::string(k_blob_prefix) + std::to_string(index));
    blob->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

    // The users, grouped by function: a literal used by several functions
    // gets one decode per function, each with its own buffer.
    const std::vector<UseSite> sites = use_sites(*candidate.global, layout);
    std::vector<llvm::Function*> functions;
    std::vector<std::vector<UseSite>> sites_of_function;
    for (const UseSite& site : sites) {
      auto found = std::find(functions.begin(), functions.end(), site.function);
      if (found == functions.end()) {
        functions.push_back(site.function);
        sites_of_function.emplace_back();
        found = functions.end() - 1;
      }
      const auto offset = static_cast<std::size_t>(found - functions.begin());
      sites_of_function[offset].push_back(site);
    }

    for (std::size_t group = 0; group < functions.size(); ++group) {
      llvm::Function* function = functions[group];
      const std::vector<UseSite>& group_sites = sites_of_function[group];

      // The buffer: an `alloca` in the entry block of the function.
      llvm::BasicBlock& entry_block = function->getEntryBlock();
      llvm::IRBuilder<> entry_builder(&*entry_block.getFirstInsertionPt());
      llvm::AllocaInst* buffer = entry_builder.CreateAlloca(blob_type, nullptr, "cs2.protect.buffer");
      buffer->setAlignment(llvm::Align(1));
      llvm::Value* buffer_value = buffer;

      // The decode: it has to run before **every** use of the literal in
      // this function, so it goes at the top of the block that dominates
      // them all. When that block is the entry block, the decode goes right
      // after the buffer.
      llvm::BasicBlock* target = decode_block(*function, group_sites);
      llvm::Instruction* insertion = nullptr;
      if (target == &entry_block) {
        insertion = buffer->getNextNode();
        if (insertion == nullptr) {
          insertion = entry_block.getTerminator();
        }
      } else {
        insertion = &*target->getFirstInsertionPt();
      }
      llvm::IRBuilder<> call_builder(insertion);
      llvm::Value* arguments[] = {static_cast<llvm::Value*>(blob),
                                  llvm::ConstantInt::get(i32_type, size),
                                  llvm::ConstantInt::get(i32_type, index), buffer_value};
      call_builder.CreateCall(decoder.function, arguments);
      ++result.decodes;

      // The users now point at the buffer.
      for (const UseSite& site : group_sites) {
        if (!site.through_expression) {
          site.instruction->setOperand(site.operand, buffer_value);
          continue;
        }
        llvm::IRBuilder<> builder(site.instruction);
        llvm::Value* offset_value = llvm::ConstantInt::get(i64_type, site.offset);
        llvm::Value* address = site.in_bounds
                                   ? builder.CreateInBoundsGEP(i8_type, buffer_value, offset_value)
                                   : builder.CreateGEP(i8_type, buffer_value, offset_value);
        site.instruction->setOperand(site.operand, address);
      }

      literal.uses += group_sites.size();
      ++literal.functions;
    }

    // The plaintext leaves the image: the literal is deleted.
    candidate.global->eraseFromParent();

    literal.protected_literal = true;
    ++result.protected_count;
    result.plaintext_bytes += literal.length;
    result.blob_bytes += size;
    result.literals.push_back(std::move(literal));
    ++index;
  }

  return result;
}

}  // namespace cs2::protect
