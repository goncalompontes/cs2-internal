#include "transform.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
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

namespace cs2::protect {

namespace {

// --- The cipher -------------------------------------------------------------
//
// The cipher is deliberately trivial: the value that hides the call graph is
// not the secret -- the *shape* of the image is. What the cipher buys is
// that a scanner cannot grep for the key, and that the table does not have
// the same shape in two builds. An analyst who recognises the mixer recovers
// the key in minutes; this is obfuscation, not cryptography.

/// The multiplier of the mixer (the bits of the golden ratio).
constexpr std::uint64_t k_mix_multiplier = 0x9E3779B97F4A7C15ULL;
/// The xor constant of the mixer.
constexpr std::uint64_t k_mix_xor = 0xBF58476D1CE4E5B9ULL;
/// How far the mixer rotates its result.
constexpr unsigned k_mix_rotate = 31;

/// The number of constants the key is split into.
constexpr std::size_t k_key_parts = 3;

/// The names of the globals the transform adds to the module.
constexpr std::string_view k_table_name = "cs2.protect.table";
constexpr std::string_view k_descriptor_name = "cs2.protect.desc";
constexpr std::string_view k_key_names[k_key_parts] = {"cs2.protect.key0", "cs2.protect.key1",
                                                       "cs2.protect.key2"};

/// The named metadata the early phase records the selection in.
constexpr std::string_view k_functions_metadata = "cs2.protect.functions";
constexpr std::string_view k_entries_metadata = "cs2.protect.entries";

[[nodiscard]] auto rotate_left(std::uint64_t value, unsigned amount) noexcept -> std::uint64_t {
  return (value << amount) | (value >> (64U - amount));
}

/// The mixer. The IR that the transform emits computes exactly this, so the
/// value the pass encrypts with is the value the call site decrypts with.
[[nodiscard]] auto mix64(std::uint64_t value) noexcept -> std::uint64_t {
  value *= k_mix_multiplier;
  value ^= k_mix_xor;
  return rotate_left(value, k_mix_rotate);
}

/// The next value of a seeded stream (splitmix64).
[[nodiscard]] auto splitmix64(std::uint64_t& state) noexcept -> std::uint64_t {
  state += 0x9E3779B97F4A7C15ULL;
  std::uint64_t value = state;
  value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
  value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
  return value ^ (value >> 31U);
}

/// The seed of one module: the build seed mixed with the module id, so that
/// two modules of one build do not share a key and two builds do not either.
[[nodiscard]] auto module_seed(const Config& config, std::string_view module_id) noexcept
    -> std::uint64_t {
  std::uint64_t base = 0;
  if (config.seed.has_value()) {
    base = *config.seed;
  } else if (config.policy.has_value() && config.policy->seed.has_value()) {
    base = *config.policy->seed;
  } else {
    base = derive_seed(module_id, "cs2.protect");
  }
  std::uint64_t state = base ^ derive_seed(module_id, "cs2.protect.module");
  return splitmix64(state);
}

/// The constants the key is split into.
///
/// The three parts must differ: two equal parts cancel each other in the xor,
/// and the split would buy nothing.
[[nodiscard]] auto key_parts(std::uint64_t seed) noexcept
    -> std::array<std::uint64_t, k_key_parts> {
  std::uint64_t state = seed;
  std::array<std::uint64_t, k_key_parts> parts{};
  for (std::size_t i = 0; i < parts.size(); ++i) {
    parts[i] = splitmix64(state);
    if (parts[i] == 0) {
      parts[i] = 1;  // a zero part would drop out of the xor
    }
  }
  for (std::size_t i = 0; i < parts.size(); ++i) {
    for (std::size_t j = i + 1; j < parts.size(); ++j) {
      while (parts[i] == parts[j]) {
        parts[j] = splitmix64(state);
        if (parts[j] == 0) {
          parts[j] = 1;
        }
      }
    }
  }
  return parts;
}

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

/// Why a call is not rewritten, if it is not.
///
/// The order of the rules is the order of the specification's table: the
/// first rule that holds is the reason that is reported.
[[nodiscard]] auto skip_reason(const llvm::CallBase& call) -> std::optional<CallSkipReason> {
  const llvm::Function* callee = call.getCalledFunction();

  if (callee != nullptr && callee->isVarArg()) {
    return CallSkipReason::variadic_callee;
  }
  if (callee != nullptr) {
    for (const llvm::Argument& argument : callee->args()) {
      if (argument.hasByValAttr() || argument.hasStructRetAttr() || argument.hasInAllocaAttr() ||
          argument.hasPreallocatedAttr()) {
        return CallSkipReason::indirect_parameter;
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
  if (callee->isDeclaration()) {
    // An import or an intrinsic: it is in the import table anyway.
    return CallSkipReason::external_callee;
  }
  if (llvm::isa<llvm::InvokeInst>(call)) {
    return CallSkipReason::invoke;
  }
  if (callee->hasFnAttribute(llvm::Attribute::OptimizeNone)) {
    return CallSkipReason::optnone_callee;
  }
  return std::nullopt;
}

/// The metadata string of a node, or "" when the node is not a string.
[[nodiscard]] auto md_string(const llvm::Metadata* metadata) -> std::string {
  const auto* text = llvm::dyn_cast_or_null<llvm::MDString>(metadata);
  if (text == nullptr) {
    return {};
  }
  return text->getString().str();
}

}  // namespace

[[nodiscard]] auto to_string(CallSkipReason reason) noexcept -> const char* {
  switch (reason) {
    case CallSkipReason::variadic_callee:
      return "variadic_callee";
    case CallSkipReason::indirect_parameter:
      return "indirect_parameter";
    case CallSkipReason::musttail:
      return "musttail";
    case CallSkipReason::external_callee:
      return "external_callee";
    case CallSkipReason::optnone_callee:
      return "optnone_callee";
    case CallSkipReason::invoke:
      return "invoke";
    case CallSkipReason::indirect_callee:
      return "indirect_callee";
    case CallSkipReason::unselected_caller:
      return "unselected_caller";
    default:
      return "external_callee";
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
      reason = to_string(match.level);
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

  // The call sites that are rewritten, and the callees they name.
  struct Site {
    llvm::CallInst* call{nullptr};
    llvm::Function* callee{nullptr};
    std::size_t function{0};  ///< the index of the caller in `result.functions`.
    std::size_t slot{0};
  };
  std::vector<Site> sites;
  std::vector<llvm::Function*> callees;

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
        const auto reason = skip_reason(*call);
        if (reason.has_value()) {
          transform.skipped.push_back(*reason);
          transform.skipped_callees.push_back(callee_name(*call));
          continue;
        }
        auto* direct = llvm::dyn_cast<llvm::CallInst>(call);
        if (direct == nullptr) {
          continue;
        }
        llvm::Function* callee = direct->getCalledFunction();
        if (callee == nullptr) {
          continue;
        }
        if (std::find(callees.begin(), callees.end(), callee) == callees.end()) {
          callees.push_back(callee);
        }
        sites.push_back(Site{direct, callee, function_index, 0});
      }
    }
    result.functions.push_back(std::move(transform));
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

  // The key: three constants, mixed at the call site.
  const std::array<std::uint64_t, k_key_parts> parts = key_parts(seed);
  const std::uint64_t key = mix64(parts[0] ^ parts[1] ^ parts[2]);

  // The table and the descriptor.
  llvm::ArrayType* table_type = llvm::ArrayType::get(pointer_type, callees.size());
  llvm::ArrayType* descriptor_type = llvm::ArrayType::get(i64_type, sites.size());

  std::vector<llvm::Constant*> table_entries;
  table_entries.reserve(callees.size());
  for (llvm::Function* callee : callees) {
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

  // The rewrite. The decode is inline arithmetic: a call to a shared helper
  // would add a stack frame to every call site.
  //
  // The loads of the key and of the descriptor are *volatile*, and that is
  // not a detail: the whole dispatch is a compile-time constant (the key is
  // a compile-time key, the descriptor is a compile-time blob), so the
  // compiler folds it away -- the index becomes a constant, the load from the
  // table folds to the callee's address, and the indirect call becomes a
  // direct one again. That is not theoretical: without the barrier the
  // optimiser reverts the rewrite in most of the payload's modules, and the
  // protection is void. A volatile load is the cheapest barrier that keeps
  // the image read-only (a writable table would be patched at run time).
  for (std::size_t slot = 0; slot < sites.size(); ++slot) {
    Site& site = sites[slot];
    llvm::IRBuilder<> builder(site.call);

    llvm::Value* mixed = builder.CreateLoad(i64_type, key_globals[0], /*isVolatile=*/true);
    mixed =
        builder.CreateXor(mixed, builder.CreateLoad(i64_type, key_globals[1], /*isVolatile=*/true));
    mixed =
        builder.CreateXor(mixed, builder.CreateLoad(i64_type, key_globals[2], /*isVolatile=*/true));
    mixed = builder.CreateMul(mixed, llvm::ConstantInt::get(i64_type, k_mix_multiplier));
    mixed = builder.CreateXor(mixed, llvm::ConstantInt::get(i64_type, k_mix_xor));
    llvm::Value* low = builder.CreateShl(mixed, k_mix_rotate);
    llvm::Value* high = builder.CreateLShr(mixed, 64U - k_mix_rotate);
    mixed = builder.CreateOr(low, high);

    llvm::Value* slot_address = builder.CreateInBoundsGEP(
        descriptor_type, descriptor,
        {llvm::ConstantInt::get(i64_type, 0), llvm::ConstantInt::get(i64_type, slot)});
    llvm::Value* cipher = builder.CreateLoad(i64_type, slot_address, /*isVolatile=*/true);
    llvm::Value* index = builder.CreateXor(cipher, mixed);

    llvm::Value* entry_address = builder.CreateInBoundsGEP(pointer_type, table, index);
    llvm::Value* callee = builder.CreateLoad(pointer_type, entry_address);

    // The call keeps its function type, its attributes and its calling
    // convention: only the callee becomes a value.
    site.call->setCalledOperand(callee);
  }

  // The per-function counters.
  for (const Site& site : sites) {
    ++result.functions[site.function].rewritten;
  }

  result.table_size = callees.size();
  result.descriptor_size = sites.size();
  return result;
}

}  // namespace cs2::protect
