#include "transform.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Casting.h"
#include "llvm/TargetParser/Triple.h"

namespace cs2::protect {

namespace {

// The cipher itself (the mixer, the key and the module seed) lives in
// cipher.hpp: the virtualizer encrypts its bytecode with the same
// primitives, and a copy of the mixer in two places is a mixer that drifts.

/// The names of the globals the transform adds to the module.
constexpr std::string_view k_table_name = "cs2.protect.table";
constexpr std::string_view k_descriptor_name = "cs2.protect.desc";
constexpr std::string_view k_key_names[k_key_parts] = {"cs2.protect.key0", "cs2.protect.key1",
                                                       "cs2.protect.key2"};

/// The names of the globals that hold the constants of the mixer.
///
/// The mixer constants do **not** go into the instruction stream as 64-bit
/// immediates. The X86 backend of the SDK's LLVM 22.1.8 mis-materialises a
/// 64-bit constant in this shape: `xor i64 %mul, 0xBF58476D1CE4E5B9` becomes
/// `movabsq $0xBF58476C1CE4E5B9` -- one bit of the constant is lost. The
/// dispatch still reached the right callee, but only because the lost bit
/// happened to be the sign bit of the index and the table's stride is 8
/// bytes: three coincidences, and the next constant would not have been so
/// lucky. A constant in a global is written by the object writer, so it
/// reaches the image bit for bit.
constexpr std::string_view k_multiplier_name = "cs2.protect.mixmul";
constexpr std::string_view k_xor_name = "cs2.protect.mixxor";

/// The named metadata the early phase records the selection in.
constexpr std::string_view k_functions_metadata = "cs2.protect.functions";
constexpr std::string_view k_entries_metadata = "cs2.protect.entries";
/// The named metadata the early phase records the `cross_module` matches in.
constexpr std::string_view k_cross_metadata = "cs2.protect.cross";

/// Permutes the table, so that the index in the descriptor is not a
/// predictable table position and the table does not have the same shape in
/// two builds.
void permute(std::vector<llvm::Function*>& callees, std::uint64_t seed) {
  std::uint64_t state = seed;
  for (std::size_t i = callees.size(); i > 1; --i) {
    const auto pick = static_cast<std::size_t>(splitmix64(state) % i);
    std::swap(callees[i - 1], callees[pick]);
  }
}

/// The name of the callee of a call, for the report.
[[nodiscard]] auto callee_name(const llvm::CallBase& call) -> std::string {
  const llvm::Function* callee = call.getCalledFunction();
  if (callee != nullptr) {
    return callee->getName().str();
  }
  return "<indirect>";
}

/// The metadata string of a node, or "" when the node is not a string.
[[nodiscard]] auto md_string(const llvm::Metadata* metadata) -> std::string {
  const auto* text = llvm::dyn_cast_or_null<llvm::MDString>(metadata);
  if (text == nullptr) {
    return {};
  }
  return text->getString().str();
}

/// The parameter that carries the `sret` attribute, if the callee has one.
///
/// The hidden pointer is **not** necessarily the first parameter: a member
/// function takes the `this` pointer first, and the hidden pointer after it.
/// The attribute is what says which parameter it is, and the call site
/// carries the same attribute, so the two agree.
[[nodiscard]] auto sret_parameter(const llvm::Function& callee) -> const llvm::Argument* {
  for (const llvm::Argument& argument : callee.args()) {
    if (argument.hasStructRetAttr()) {
      return &argument;
    }
  }
  return nullptr;
}

/// Why a call is not rewritten, if it is not.
///
/// The order of the rules is the order of the specification's table: the
/// first rule that holds is the reason that is reported.
///
/// The `routable` flag is the one rule that depends on the *policy* rather
/// than on the call: the `external_callee` rule does not hold for a
/// declaration the policy's `cross_module` list names, because the caller
/// routes it through its own table as a relocatable pointer to the external
/// symbol.
///
/// The `sret_routable` flag is the one rule that depends on the *target*:
/// the `sret` parameter of a callee is routable only where the narrowing was
/// measured (see `k_sret_target`).
[[nodiscard]] auto skip_reason_impl(const llvm::CallBase& call, bool routable, bool sret_routable)
    -> std::optional<CallSkipReason> {
  const llvm::Function* callee = call.getCalledFunction();
  if (callee != nullptr && callee->isVarArg()) {
    return CallSkipReason::variadic_callee;
  }
  if (callee != nullptr) {
    // The parameters that make an indirect call impossible. The `sret`
    // parameter is **not** one of them: the backend accepts an indirect
    // `sret` call on the target the narrowing was measured on, with the
    // same register placement as the direct call. The other three stay
    // refused, and the refusal names the attribute: a refusal that says
    // "byval" is actionable, a refusal that says "indirect_parameter" is
    // not.
    for (const llvm::Argument& argument : callee->args()) {
      if (argument.hasByValAttr()) {
        return CallSkipReason::byval_parameter;
      }
      if (argument.hasInAllocaAttr()) {
        return CallSkipReason::inalloca_parameter;
      }
      if (argument.hasPreallocatedAttr()) {
        return CallSkipReason::preallocated_parameter;
      }
    }
    const llvm::Argument* hidden = sret_parameter(*callee);
    if (hidden != nullptr) {
      if (!sret_routable) {
        return CallSkipReason::sret_parameter;
      }
      if (!callee->getReturnType()->isVoidTy()) {
        // A callee that returns a value *and* takes an `sret` is not a
        // thing in the x64 ABI: the result is in the caller's temporary.
        return CallSkipReason::sret_result;
      }
    }
  }
  if (call.isMustTailCall()) {
    return CallSkipReason::musttail;
  }
  if (callee == nullptr) {
    // The call already goes through a pointer: there is no direct call left
    // to hide.
    return CallSkipReason::indirect_callee;
  }
  if (callee->isIntrinsic()) {
    // An intrinsic has no body to call: the backend expands it.
    return CallSkipReason::intrinsic_callee;
  }
  if (callee->isDeclarationForLinker()) {
    if (!routable) {
      // An import, an intrinsic or an `available_externally` function: none of
      // them has code in this module, so a table entry for it would resolve to
      // zero and the dispatch would call address zero.
      return CallSkipReason::external_callee;
    }
    // The declaration is one the policy named: it has no code in this module
    // either, but the caller routes it through its own table.
    //
    // The `invoke` rule does not hold here either. The rule exists because
    // the pass does not model the EH edges, and the rewrite does not model
    // them either: it replaces the *callee* and leaves the landing pad and
    // the unwind edge exactly as they are. The parent's measurement of the
    // tree counted the cross-TU calls that are EH edges among the ordinary
    // ones (the rule order reported them as `external_callee`, so the
    // measurement could not tell them apart), and the cross-TU graph cannot
    // be closed while they stay direct.
    if (callee->hasFnAttribute(llvm::Attribute::OptimizeNone)) {
      return CallSkipReason::optnone_callee;
    }
    return std::nullopt;
  }
  if (llvm::isa<llvm::InvokeInst>(call)) {
    return CallSkipReason::invoke;
  }
  if (callee->hasFnAttribute(llvm::Attribute::OptimizeNone)) {
    return CallSkipReason::optnone_callee;
  }
  return std::nullopt;
}

/// The globals the decode reads.
///
/// The decode is the same everywhere it is emitted -- at a call site and in
/// the thunk of an `sret` call site -- so the globals it reads are passed
/// around as one value instead of as eight parameters.
struct Dispatch {
  llvm::Type* pointer_type{nullptr};
  llvm::ArrayType* descriptor_type{nullptr};
  llvm::GlobalVariable* descriptor{nullptr};
  llvm::GlobalVariable* table{nullptr};
  llvm::GlobalVariable* key[k_key_parts]{};
  llvm::GlobalVariable* multiplier{nullptr};
  llvm::GlobalVariable* xor_constant{nullptr};
  unsigned rotate{0};
};

/// The decode of one dispatch: the callee the descriptor names.
///
/// The decode is inline arithmetic: a call to a shared helper would add a
/// stack frame to every call site.
///
/// The loads of the key and of the descriptor are *volatile*, and that is
/// not a detail: the whole dispatch is a compile-time constant (the key is
/// a compile-time key, the descriptor is a compile-time blob), so the
/// compiler folds it away -- the index becomes a constant, the load from the
/// table folds to the callee's address, and the indirect call becomes a
/// direct one again. That is not theoretical: without the barrier the
/// optimiser reverts the rewrite in most of the payload's modules, and the
/// protection is void. A volatile load is the cheapest barrier that keeps
/// the image read-only (a writable table would be patched at run time).
///
/// The two constants of the mixer are read the same way, for a different
/// reason: a volatile load is never folded into an immediate, so the value
/// that the call site uses is the value in the image and not a value the
/// backend may have mis-materialised.
[[nodiscard]] auto emit_dispatch(llvm::IRBuilder<>& builder, std::size_t slot,
                                 const Dispatch& dispatch) -> llvm::Value* {
  llvm::Type* i64_type = builder.getInt64Ty();

  llvm::Value* mixed = builder.CreateLoad(i64_type, dispatch.key[0], /*isVolatile=*/true);
  mixed =
      builder.CreateXor(mixed, builder.CreateLoad(i64_type, dispatch.key[1], /*isVolatile=*/true));
  mixed =
      builder.CreateXor(mixed, builder.CreateLoad(i64_type, dispatch.key[2], /*isVolatile=*/true));
  llvm::Value* factor = builder.CreateLoad(i64_type, dispatch.multiplier, /*isVolatile=*/true);
  mixed = builder.CreateMul(mixed, factor);
  llvm::Value* constant = builder.CreateLoad(i64_type, dispatch.xor_constant, /*isVolatile=*/true);
  mixed = builder.CreateXor(mixed, constant);
  llvm::Value* low = builder.CreateShl(mixed, dispatch.rotate);
  llvm::Value* high = builder.CreateLShr(mixed, 64U - dispatch.rotate);
  mixed = builder.CreateOr(low, high);

  llvm::Value* slot_address = builder.CreateInBoundsGEP(
      dispatch.descriptor_type, dispatch.descriptor,
      {llvm::ConstantInt::get(i64_type, 0), llvm::ConstantInt::get(i64_type, slot)});
  llvm::Value* cipher = builder.CreateLoad(i64_type, slot_address, /*isVolatile=*/true);
  llvm::Value* index = builder.CreateXor(cipher, mixed);

  // The GEP into the table is *not* `inbounds`: the index is a value the
  // image computes at run time, and the pass has no right to promise that
  // it is inside the table. An out-of-range index would then be poison by
  // the letter of the reference, and the call through it would be
  // undefined behaviour; without the promise it is a wrong address, which
  // is a bug the payload check can see.
  llvm::Value* entry_address = builder.CreateGEP(dispatch.pointer_type, dispatch.table, index);
  return builder.CreateLoad(dispatch.pointer_type, entry_address);
}

/// The name of the thunk of a call site.
[[nodiscard]] auto thunk_name(std::size_t slot) -> std::string {
  return "cs2.protect.thunk." + std::to_string(slot);
}

/// The scratch of a call site: the arguments of the call, in the call's order.
///
/// The hidden pointer of an `sret` call is the **first** cell: in LLVM IR the
/// hidden pointer is an explicit argument of the call, so the scratch already
/// holds it in the right place, and the thunk passes it unchanged.
[[nodiscard]] auto scratch_type(llvm::LLVMContext& context, const llvm::CallBase& call)
    -> llvm::StructType* {
  std::vector<llvm::Type*> elements;
  elements.reserve(call.arg_size());
  for (const llvm::Use& argument : call.args()) {
    elements.push_back(argument->getType());
  }
  return llvm::StructType::get(context, elements);
}

/// The pointer to one cell of the scratch.
///
/// The first cell is the scratch itself: the hidden pointer of an `sret` call
/// is at offset zero.
[[nodiscard]] auto scratch_cell(llvm::IRBuilder<>& builder, llvm::StructType* scratch,
                                llvm::Value* base, std::size_t index) -> llvm::Value* {
  if (index == 0) {
    return base;
  }
  llvm::Type* i32_type = builder.getInt32Ty();
  return builder.CreateGEP(
      scratch, base,
      {llvm::ConstantInt::get(i32_type, 0), llvm::ConstantInt::get(i32_type, index)});
}

/// The attributes the call to the thunk keeps.
///
/// The thunk is not the callee: it reads the scratch and the table, so the
/// `memory(...)` and the `speculatable` of the callee are not true of it,
/// and the parameter attributes do not describe the thunk's parameter at all.
///
/// The one attribute that has to survive is `nounwind`: the call site is in
/// the caller's EH region, and a call that may throw there would need the EH
/// pads that the early passes have already emitted.
[[nodiscard]] auto thunk_attributes(llvm::LLVMContext& context, const llvm::CallBase& call)
    -> llvm::AttributeList {
  llvm::AttributeList attributes;
  if (call.hasFnAttr(llvm::Attribute::NoUnwind)) {
    attributes = attributes.addFnAttribute(context, llvm::Attribute::NoUnwind);
  }
  return attributes;
}

/// Emits the thunk of an `sret` call site, and rewrites the call to it.
///
/// The thunk adapts the struct-returning call to the **uniform signature**
/// (`u64 thunk(ptr scratch)`): the scratch holds the arguments of the call in
/// the call's order, the hidden pointer is the first cell, and the thunk
/// loads the arguments out of the scratch and calls the callee **with the
/// `sret` attribute**.
///
/// The callee is reached through the caller's own table, so the edge stays
/// hidden: a thunk that called the callee by name would leave the edge in the
/// image, which is the thing this pass exists to remove.
///
/// The **temporary is the caller's**: the hidden pointer is a pointer into the
/// caller's frame (the scratch lives there), and the callee writes through
/// it. The interpreter's frame is not involved.
void emit_sret_thunk(llvm::Module& module, llvm::Function* caller, llvm::CallBase* call,
                     std::size_t slot, const Dispatch& dispatch) {
  llvm::LLVMContext& context = module.getContext();
  llvm::Type* i64_type = llvm::Type::getInt64Ty(context);
  llvm::Type* pointer_type = dispatch.pointer_type;

  // The scratch: the arguments of the call, in the call's order.
  llvm::StructType* scratch = scratch_type(context, *call);

  // The scratch lives in the caller's frame.
  llvm::IRBuilder<> at_call(call);
  llvm::AllocaInst* cells = at_call.CreateAlloca(scratch, nullptr, "cs2.protect.scratch");
  // The alloca goes to the entry block of the caller: an alloca in the body
  // of a loop would grow the stack on every iteration.
  cells->moveBefore(caller->getEntryBlock().getFirstInsertionPt());

  // The stores: the arguments of the call, in the call's order. The hidden
  // pointer is the first one, and it is stored **unchanged**.
  for (std::size_t index = 0; index < call->arg_size(); ++index) {
    llvm::Value* cell = scratch_cell(at_call, scratch, cells, index);
    at_call.CreateStore(call->getArgOperand(index), cell);
  }

  // The thunk: the uniform signature.
  llvm::Function* thunk =
      llvm::Function::Create(llvm::FunctionType::get(i64_type, {pointer_type}, /*isVarArg=*/false),
                             llvm::GlobalValue::InternalLinkage, thunk_name(slot), module);
  // The thunk is not inlined away: it is the adapter of the call, and an
  // inlined thunk would put the callee's name back into the caller.
  thunk->addFnAttr(llvm::Attribute::NoInline);
  // The thunk does not throw when the callee does not: the call to the
  // callee is the only thing in it that can.
  if (call->hasFnAttr(llvm::Attribute::NoUnwind)) {
    thunk->addFnAttr(llvm::Attribute::NoUnwind);
  }
  // The personality of the caller: the thunk sits in the middle of an EH
  // edge, and a frame that can throw needs the unwind information of the
  // personality that reads it.
  if (caller->hasPersonalityFn()) {
    thunk->setPersonalityFn(caller->getPersonalityFn());
  }

  llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", thunk);
  llvm::IRBuilder<> builder(entry);
  llvm::Value* scratch_pointer = thunk->getArg(0);

  // The arguments, out of the scratch.
  std::vector<llvm::Value*> arguments;
  arguments.reserve(call->arg_size());
  for (std::size_t index = 0; index < call->arg_size(); ++index) {
    llvm::Value* cell = scratch_cell(builder, scratch, scratch_pointer, index);
    arguments.push_back(builder.CreateLoad(scratch->getElementType(index), cell));
  }

  // The callee: the dispatch, in the thunk.
  llvm::Value* callee = emit_dispatch(builder, slot, dispatch);

  // The call: the same function type, the same attributes and the same
  // calling convention as the call the thunk replaces -- the `sret`
  // attribute of the hidden pointer included.
  llvm::CallInst* thunk_call = builder.CreateCall(call->getFunctionType(), callee, arguments);
  thunk_call->setAttributes(call->getAttributes());
  thunk_call->setCallingConv(call->getCallingConv());

  // The callee returns nothing: the result is in the caller's temporary. The
  // thunk returns 0.
  builder.CreateRet(llvm::ConstantInt::get(i64_type, 0));

  // The call site: the thunk, with the scratch.
  if (auto* invoke = llvm::dyn_cast<llvm::InvokeInst>(call)) {
    auto* replacement =
        llvm::InvokeInst::Create(thunk->getFunctionType(), thunk, invoke->getNormalDest(),
                                 invoke->getUnwindDest(), {cells}, "", invoke->getIterator());
    replacement->setAttributes(thunk_attributes(context, *invoke));
    replacement->setCallingConv(invoke->getCallingConv());
    replacement->setDebugLoc(invoke->getDebugLoc());
    invoke->replaceAllUsesWith(replacement);
    invoke->eraseFromParent();
    return;
  }
  auto* replacement =
      llvm::CallInst::Create(thunk->getFunctionType(), thunk, {cells}, "", call->getIterator());
  replacement->setAttributes(thunk_attributes(context, *call));
  replacement->setCallingConv(call->getCallingConv());
  replacement->setDebugLoc(call->getDebugLoc());
  call->replaceAllUsesWith(replacement);
  call->eraseFromParent();
}

}  // namespace

auto sret_routable(const llvm::Module& module) -> bool {
  // The triple of the module is the target the code is compiled for. The
  // narrowing was measured on one of them, and the rule stays closed on
  // every other one.
  //
  // The comparison is on the components and not on the string: a build's
  // triple carries the version of the toolchain
  // ("x86_64-pc-windows-msvc19.51.36252"), and the string is not the
  // target the measurement was made on.
  const llvm::Triple triple(module.getTargetTriple().str());
  if (!triple.isX86_64() || !triple.isOSWindows() || !triple.isWindowsMSVCEnvironment()) {
    return false;
  }
  return triple.getTriple().starts_with(k_sret_target);
}

auto skip_reason(const llvm::CallBase& call, bool sret_routable) -> std::optional<CallSkipReason> {
  return skip_reason_impl(call, false, sret_routable);
}

auto skip_reason_routed(const llvm::CallBase& call, bool sret_routable)
    -> std::optional<CallSkipReason> {
  return skip_reason_impl(call, true, sret_routable);
}

[[nodiscard]] auto to_string(CallSkipReason reason) noexcept -> const char* {
  switch (reason) {
    case CallSkipReason::variadic_callee:
      return "variadic_callee";
    case CallSkipReason::byval_parameter:
      return "byval_parameter";
    case CallSkipReason::inalloca_parameter:
      return "inalloca_parameter";
    case CallSkipReason::preallocated_parameter:
      return "preallocated_parameter";
    case CallSkipReason::sret_parameter:
      return "sret_parameter";
    case CallSkipReason::sret_result:
      return "sret_result";
    case CallSkipReason::musttail:
      return "musttail";
    case CallSkipReason::external_callee:
      return "external_callee";
    case CallSkipReason::intrinsic_callee:
      return "intrinsic_callee";
    case CallSkipReason::optnone_callee:
      return "optnone_callee";
    case CallSkipReason::invoke:
      return "invoke";
    case CallSkipReason::indirect_callee:
      return "indirect_callee";
    default:
      // An enumerator that this build does not know: say so, do not report a
      // reason that is not the one.
      return "unknown";
  }
}

[[nodiscard]] auto to_string(CrossState state) noexcept -> const char* {
  switch (state) {
    case CrossState::matched:
      return "matched";
    case CrossState::blocked:
      return "blocked";
    case CrossState::unmatched:
    default:
      return "unmatched";
  }
}

void record_selection(llvm::Module& module, const SelectionResult& result) {
  llvm::LLVMContext& context = module.getContext();

  // A module that is processed twice would otherwise carry the record twice.
  if (llvm::NamedMDNode* previous = module.getNamedMetadata(k_functions_metadata)) {
    module.eraseNamedMetadata(previous);
  }
  if (llvm::NamedMDNode* previous = module.getNamedMetadata(k_entries_metadata)) {
    module.eraseNamedMetadata(previous);
  }
  if (llvm::NamedMDNode* previous = module.getNamedMetadata(k_cross_metadata)) {
    module.eraseNamedMetadata(previous);
  }

  llvm::NamedMDNode* functions = module.getOrInsertNamedMetadata(k_functions_metadata);
  for (const Selection& selection : result.selected) {
    llvm::MDNode* node = llvm::MDNode::get(
        context,
        llvm::ArrayRef<llvm::Metadata*>{llvm::MDString::get(context, selection.symbol),
                                        llvm::MDString::get(context, selection.qualified),
                                        llvm::MDString::get(context, to_string(selection.level))});
    functions->addOperand(node);
  }

  llvm::NamedMDNode* entries = module.getOrInsertNamedMetadata(k_entries_metadata);
  for (const PolicyMatch& match : result.policy_matches) {
    std::string state = "unmatched";
    std::string reason;
    if (match.matched) {
      state = "matched";
    } else if (match.unimplemented) {
      state = "unimplemented";
      // The level that was applied to the function, not the level of the
      // entry: the entry did not get its own level.
      reason = to_string(match.applied_level);
    } else if (match.blocked) {
      state = "blocked";
      if (match.blocked_reason.has_value()) {
        reason = to_string(*match.blocked_reason);
      }
    }
    llvm::MDNode* node = llvm::MDNode::get(
        context,
        llvm::ArrayRef<llvm::Metadata*>{
            llvm::MDString::get(context, match.pattern), llvm::MDString::get(context, match.module),
            llvm::MDString::get(context, state), llvm::MDString::get(context, reason),
            llvm::MDString::get(context, to_string(match.level))});
    entries->addOperand(node);
  }

  // The `cross_module` matches: the declarations the policy's list named in
  // this module. The late phase routes the calls to them, and it cannot know
  // them on its own: it does not read the policy's list.
  llvm::NamedMDNode* cross = module.getOrInsertNamedMetadata(k_cross_metadata);
  for (const CrossModuleMatch& match : result.cross_matches) {
    llvm::MDNode* node = llvm::MDNode::get(
        context, llvm::ArrayRef<llvm::Metadata*>{llvm::MDString::get(context, match.pattern),
                                                 llvm::MDString::get(context, match.symbol),
                                                 llvm::MDString::get(context, match.qualified)});
    cross->addOperand(node);
  }
}

[[nodiscard]] auto read_selection(const llvm::Module& module) -> RecordedSelection {
  RecordedSelection selection;

  const llvm::NamedMDNode* functions = module.getNamedMetadata(k_functions_metadata);
  if (functions != nullptr) {
    for (const llvm::MDNode* node : functions->operands()) {
      if (node->getNumOperands() < 3) {
        continue;
      }
      RecordedFunction function;
      function.symbol = md_string(node->getOperand(0));
      function.qualified = md_string(node->getOperand(1));
      const std::string level = md_string(node->getOperand(2));
      function.level =
          level == "virtualized" ? ProtectionLevel::virtualized : ProtectionLevel::call;
      selection.functions.push_back(std::move(function));
    }
  }

  const llvm::NamedMDNode* entries = module.getNamedMetadata(k_entries_metadata);
  if (entries != nullptr) {
    for (const llvm::MDNode* node : entries->operands()) {
      if (node->getNumOperands() < 5) {
        continue;
      }
      RecordedEntry entry;
      entry.pattern = md_string(node->getOperand(0));
      entry.module = md_string(node->getOperand(1));
      entry.state = md_string(node->getOperand(2));
      entry.reason = md_string(node->getOperand(3));
      const std::string level = md_string(node->getOperand(4));
      entry.level = level == "virtualized" ? ProtectionLevel::virtualized : ProtectionLevel::call;
      selection.entries.push_back(std::move(entry));
    }
  }

  const llvm::NamedMDNode* cross = module.getNamedMetadata(k_cross_metadata);
  if (cross != nullptr) {
    for (const llvm::MDNode* node : cross->operands()) {
      if (node->getNumOperands() < 3) {
        continue;
      }
      RecordedCross match;
      match.pattern = md_string(node->getOperand(0));
      match.symbol = md_string(node->getOperand(1));
      match.qualified = md_string(node->getOperand(2));
      selection.cross.push_back(std::move(match));
    }
  }

  return selection;
}

auto transform_module(llvm::Module& module, const Config& config,
                      const RecordedSelection& selection) -> TransformResult {
  TransformResult result;
  llvm::LLVMContext& context = module.getContext();
  llvm::Type* i64_type = llvm::Type::getInt64Ty(context);
  llvm::Type* pointer_type = llvm::PointerType::getUnqual(context);

  // The selected functions that are still here and that this task transforms.
  std::vector<const RecordedFunction*> alive;
  for (const RecordedFunction& function : selection.functions) {
    if (module.getFunction(function.symbol) == nullptr) {
      // The optimiser deleted it: the protection would be silently lost.
      result.vanished.push_back(function);
      continue;
    }
    if (function.level != ProtectionLevel::call) {
      // The level is not implemented yet (T20): the function is selected, but
      // this task does not transform it.
      result.unimplemented.push_back(function);
      continue;
    }
    alive.push_back(&function);
  }

  // The declarations the policy's `cross_module` list named in this module.
  // The key is the mangled symbol: the call site names the callee, and the
  // symbol is what the two phases agree on.
  std::set<std::string> routable;
  for (const RecordedCross& match : selection.cross) {
    routable.insert(match.symbol);
  }

  // The call sites that are rewritten, and the callees they name.
  struct Site {
    llvm::CallBase* call{nullptr};
    llvm::Function* callee{nullptr};
    llvm::Function* caller{nullptr};  ///< the function the call is in.
    std::size_t function{0};          ///< the index of the caller in `result.functions`.
    std::size_t slot{0};
    bool cross{false};  ///< the callee is a cross-TU declaration.
    bool sret{false};   ///< the callee takes an `sret` parameter: the call goes
                        ///< through a thunk.
  };
  std::vector<Site> sites;
  std::vector<llvm::Function*> callees;

  // The `sret` parameter of a callee is routable on the target the
  // narrowing was measured on, and nowhere else.
  const bool sret_ok = sret_routable(module);

  // The cross-TU callees that stayed direct, and the reason they stayed:
  // the symbol -> the reason. The first reason wins: a symbol is either
  // routable or not, and the reason of a refusal is a property of the
  // callee.
  std::map<std::string, std::string> direct;
  // The routable declarations whose call could not be routed: the symbol ->
  // the reason.
  std::map<std::string, std::string> blocked;

  for (const RecordedFunction* recorded : alive) {
    llvm::Function* function = module.getFunction(recorded->symbol);
    if (function == nullptr) {
      continue;
    }
    const auto function_index = result.functions.size();
    FunctionTransform transform;
    transform.symbol = recorded->symbol;
    transform.qualified = recorded->qualified;
    for (llvm::BasicBlock& block : *function) {
      for (llvm::Instruction& instruction : block) {
        auto* call = llvm::dyn_cast<llvm::CallBase>(&instruction);
        if (call == nullptr) {
          continue;
        }
        llvm::Function* callee = call->getCalledFunction();
        // A declaration is a function with no body in this module: an import
        // or a cross-TU callee. The pass cannot tell the two apart by
        // looking at the module, so the policy's `cross_module` list is what
        // says which ones may be routed.
        const bool declaration =
            callee != nullptr && callee->isDeclaration() && !callee->isIntrinsic();
        const bool cross = declaration && routable.count(callee->getName().str()) != 0;
        const auto reason =
            cross ? skip_reason_routed(*call, sret_ok) : skip_reason(*call, sret_ok);
        if (reason.has_value()) {
          transform.skipped.push_back(*reason);
          transform.skipped_callees.push_back(callee_name(*call));
          if (declaration) {
            direct.emplace(callee->getName().str(), to_string(*reason));
            if (cross) {
              blocked.emplace(callee->getName().str(), to_string(*reason));
            }
          }
          continue;
        }
        if (!llvm::isa<llvm::CallInst>(call) && !cross) {
          // Only a plain `call` is rewritten, unless the callee is a
          // declaration the policy named: an `invoke` to it keeps its
          // landing pad and its unwind edge, and only the callee becomes a
          // value.
          continue;
        }
        if (callee == nullptr) {
          continue;
        }
        if (std::find(callees.begin(), callees.end(), callee) == callees.end()) {
          callees.push_back(callee);
        }
        // A callee that takes an `sret` parameter is called through a
        // thunk: the thunk adapts the call to the uniform signature and
        // carries the `sret` attribute of the call it replaces.
        const bool sret = sret_parameter(*callee) != nullptr;
        sites.push_back(Site{call, callee, function, function_index, 0, cross, sret});
      }
    }
    result.functions.push_back(std::move(transform));
  }

  // The state of every `cross_module` entry of the policy in this module.
  //
  // An entry that named no declaration here is "unmatched": it may well
  // match in another translation unit, and the build as a whole decides.
  // An entry whose declaration was seen but not routed is "blocked": the
  // requirement of a `cross_module` entry is that the symbol is routed
  // *everywhere* it is called.
  if (config.policy.has_value()) {
    for (const std::string& pattern : config.policy->cross_module) {
      std::vector<const RecordedCross*> matches;
      for (const RecordedCross& match : selection.cross) {
        if (match.pattern == pattern) {
          matches.push_back(&match);
        }
      }
      if (matches.empty()) {
        result.cross.entries.push_back(CrossEntry{pattern, "", CrossState::unmatched, ""});
        continue;
      }
      for (const RecordedCross* match : matches) {
        CrossState state = CrossState::matched;
        std::string reason;
        const auto refused = blocked.find(match->symbol);
        if (refused != blocked.end()) {
          state = CrossState::blocked;
          reason = refused->second;
        }
        result.cross.entries.push_back(CrossEntry{pattern, match->symbol, state, reason});
      }
    }
  }
  for (const auto& [symbol, reason] : direct) {
    result.cross.direct.push_back(DirectCallee{symbol, reason});
  }

  if (sites.empty()) {
    // Nothing to hide: the module keeps no table and no descriptor.
    result.table_size = 0;
    result.descriptor_size = 0;
    return result;
  }

  // The table: the callees, in a permuted order, so that the index in the
  // descriptor is not a predictable table position.
  const std::uint64_t seed = module_seed(config, module.getSourceFileName());
  permute(callees, seed ^ 0x5045524D55544142ULL);

  // The mixer: per build, like the key. Its multiplier and its xor constant
  // go into the globals below, and its rotation is the one the decode emits.
  const Mixer mixer = mixer_for(seed);

  // The key: three constants, mixed at the call site.
  const std::array<std::uint64_t, k_key_parts> parts = key_parts(seed);
  const std::uint64_t key = mixer.mix64(parts[0] ^ parts[1] ^ parts[2]);

  // The table and the descriptor.
  llvm::ArrayType* table_type = llvm::ArrayType::get(pointer_type, callees.size());
  llvm::ArrayType* descriptor_type = llvm::ArrayType::get(i64_type, sites.size());

  std::vector<llvm::Constant*> table_entries;
  table_entries.reserve(callees.size());
  for (llvm::Function* callee : callees) {
    // The entry is the *symbol*, not a copy of its address: a callee that
    // is defined in another translation unit is a declaration here, and the
    // object writer emits an ADDR64 relocation against the external symbol.
    // The linker resolves it and the mapper applies the DIR64 fixup, so the
    // table holds a relocatable pointer and the image base is never baked
    // into anything.
    table_entries.push_back(callee);
  }

  std::vector<llvm::Constant*> descriptor_entries;
  descriptor_entries.reserve(sites.size());
  for (std::size_t slot = 0; slot < sites.size(); ++slot) {
    const auto index = static_cast<std::uint64_t>(
        std::find(callees.begin(), callees.end(), sites[slot].callee) - callees.begin());
    descriptor_entries.push_back(llvm::ConstantInt::get(i64_type, index ^ key));
  }

  llvm::GlobalVariable* table = new llvm::GlobalVariable(
      module, table_type, /*isConstant=*/true, llvm::GlobalValue::PrivateLinkage,
      llvm::Constant::getNullValue(table_type), k_table_name);
  table->setInitializer(llvm::ConstantArray::get(table_type, table_entries));
  table->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

  // The descriptor holds plain integers: a relocatable value inside the
  // cipher would be corrupted by the fixups of the manual mapper.
  llvm::GlobalVariable* descriptor = new llvm::GlobalVariable(
      module, descriptor_type, /*isConstant=*/true, llvm::GlobalValue::PrivateLinkage,
      llvm::Constant::getNullValue(descriptor_type), k_descriptor_name);
  descriptor->setInitializer(llvm::ConstantArray::get(descriptor_type, descriptor_entries));
  descriptor->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

  std::array<llvm::GlobalVariable*, k_key_parts> key_globals{};
  for (std::size_t i = 0; i < k_key_parts; ++i) {
    key_globals[i] = new llvm::GlobalVariable(
        module, i64_type, /*isConstant=*/true, llvm::GlobalValue::PrivateLinkage,
        llvm::ConstantInt::get(i64_type, parts[i]), k_key_names[i]);
    key_globals[i]->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
  }

  // The constants of the mixer live in globals as well, and not in the
  // instruction stream: the X86 backend of this LLVM mis-materialises a
  // 64-bit immediate in this shape (see k_multiplier_name). A global is
  // written by the object writer, so the value that reaches the image is the
  // value the pass encrypted the descriptor with. Their **values** are per
  // build: the mixer's, not a constant of the protection.
  llvm::GlobalVariable* multiplier = new llvm::GlobalVariable(
      module, i64_type, /*isConstant=*/true, llvm::GlobalValue::PrivateLinkage,
      llvm::ConstantInt::get(i64_type, mixer.multiplier), k_multiplier_name);
  multiplier->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
  llvm::GlobalVariable* xor_constant = new llvm::GlobalVariable(
      module, i64_type, /*isConstant=*/true, llvm::GlobalValue::PrivateLinkage,
      llvm::ConstantInt::get(i64_type, mixer.xor_constant), k_xor_name);
  xor_constant->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

  // The rewrite. The decode is inline arithmetic: a call to a shared helper
  // would add a stack frame to every call site.
  //
  // A call to a callee that takes an `sret` parameter goes through a
  // **thunk** instead: the thunk carries the `sret` attribute of the call and
  // the hidden pointer, and the decode lives in it. The edge is the same
  // edge -- the callee is reached through the caller's own table -- and the
  // call site keeps its landing pad and its unwind edge.
  Dispatch dispatch;
  dispatch.pointer_type = pointer_type;
  dispatch.descriptor_type = descriptor_type;
  dispatch.descriptor = descriptor;
  dispatch.table = table;
  dispatch.multiplier = multiplier;
  dispatch.xor_constant = xor_constant;
  dispatch.rotate = mixer.rotate;
  for (std::size_t i = 0; i < k_key_parts; ++i) {
    dispatch.key[i] = key_globals[i];
  }

  for (std::size_t slot = 0; slot < sites.size(); ++slot) {
    Site& site = sites[slot];
    if (site.sret) {
      emit_sret_thunk(module, site.caller, site.call, slot, dispatch);
      continue;
    }
    llvm::IRBuilder<> builder(site.call);
    llvm::Value* callee = emit_dispatch(builder, slot, dispatch);

    // The call keeps its function type, its attributes and its calling
    // convention: only the callee becomes a value.
    site.call->setCalledOperand(callee);
  }

  // The per-function counters.
  for (const Site& site : sites) {
    ++result.functions[site.function].rewritten;
    if (site.cross) {
      ++result.functions[site.function].cross;
    }
  }

  result.table_size = callees.size();
  result.descriptor_size = sites.size();
  result.key_parts = parts;
  result.mix_multiplier = mixer.multiplier;
  result.mix_xor = mixer.xor_constant;
  result.mix_rotate = mixer.rotate;
  return result;
}

}  // namespace cs2::protect
