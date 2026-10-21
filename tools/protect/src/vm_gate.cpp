// The eligibility gate of the front end of the virtualizer.
//
// The gate decides whether a function **can** be virtualized, and it
// fails **closed**: a function that cannot be lowered completely is left
// completely untouched, and the reason is reported. There is no "best
// effort" path -- a half-lowered function computes garbage, and in a
// manually mapped payload with no exception tables that kills the game.
//
// The checks run in the order of the specification's table, and the first
// one that holds is the reason that is reported: one reason per function.
//
// The rules of the calls are the rules of the transform (`skip_reason` in
// `transform.hpp`): they are reused, not written a second time, so that
// the two phases cannot drift apart.

#include "vm_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/Casting.h"

#include "transform.hpp"

namespace cs2::protect::detail {

namespace {
// --- The checks ----------------------------------------------------------------

/// Whether the function has an EH edge.
///
/// The interpreter cannot model unwinding: the frame is a flat array of
/// cells, and there is no place to put a landing pad.
[[nodiscard]] auto has_eh(const llvm::Function& function) -> bool {
  for (const llvm::BasicBlock& block : function) {
    for (const llvm::Instruction& instruction : block) {
      if (llvm::isa<llvm::InvokeInst>(instruction) ||
          llvm::isa<llvm::LandingPadInst>(instruction) ||
          llvm::isa<llvm::CatchSwitchInst>(instruction) ||
          llvm::isa<llvm::CleanupReturnInst>(instruction) ||
          llvm::isa<llvm::CatchReturnInst>(instruction) ||
          llvm::isa<llvm::ResumeInst>(instruction)) {
        return true;
      }
    }
  }
  return false;
}

/// The calls of a function, in order.
[[nodiscard]] auto calls_of(llvm::Function& function) -> std::vector<llvm::CallBase*> {
  std::vector<llvm::CallBase*> calls;
  for (llvm::BasicBlock& block : function) {
    for (llvm::Instruction& instruction : block) {
      if (auto* call = llvm::dyn_cast<llvm::CallBase>(&instruction)) {
        calls.push_back(call);
      }
    }
  }
  return calls;
}

/// The refusal of the virtual machine for a call that the transform's
/// rules skip.
///
/// The rule that holds for a call is the transform's: the `byval`, `sret`,
/// `inalloca` and `preallocated` parameters, the variadic callee, the
/// `musttail` call and the indirect call are the same rules in the two
/// phases, and the same reasons.
[[nodiscard]] auto vm_failure(CallSkipReason reason, const llvm::CallBase& call)
    -> std::expected<void, VmFailure> {
  const auto* callee = call.getCalledFunction();
  const std::string name = callee != nullptr ? callee->getName().str() : std::string();
  switch (reason) {
    case CallSkipReason::variadic_callee:
      return std::unexpected(
          VmFailure{VmRefusal::variadic_callee, "the callee '" + name + "' is variadic"});
    case CallSkipReason::indirect_parameter:
      return std::unexpected(VmFailure{
          VmRefusal::indirect_parameter,
          "the callee '" + name + "' has a byval, sret, inalloca or preallocated parameter"});
    case CallSkipReason::musttail:
      return std::unexpected(VmFailure{VmRefusal::musttail, "the call is a musttail call"});
    case CallSkipReason::indirect_callee:
      return std::unexpected(
          VmFailure{VmRefusal::indirect_call, "the call goes through a pointer"});
    case CallSkipReason::external_callee:
    case CallSkipReason::intrinsic_callee:
    case CallSkipReason::invoke:
    case CallSkipReason::optnone_callee:
      // The transform refuses these for its own reasons: the virtual
      // machine calls an import through the pool, and the other three are
      // refused elsewhere (an `invoke` is an EH edge, an intrinsic is
      // lowered or refused by the lowering, and an `optnone` callee is
      // an ordinary callee here).
      break;
  }
  return {};
}

/// The rules of the calls that the virtual machine refuses, in the order
/// of the transform's rules: the first rule that holds is the reason that
/// is reported.
constexpr std::array<CallSkipReason, 4> k_call_rules = {
    CallSkipReason::variadic_callee,
    CallSkipReason::indirect_parameter,
    CallSkipReason::musttail,
    CallSkipReason::indirect_callee,
};

/// Whether the address of the function is taken.
///
/// A rewrite changes what the address of the function means: the address
/// would be the address of the thunk, and every user of the address would
/// reach the interpreter.
[[nodiscard]] auto address_taken(const llvm::Function& function) -> bool {
  for (const llvm::User* user : function.users()) {
    const auto* call = llvm::dyn_cast<llvm::CallBase>(user);
    if (call != nullptr && call->getCalledFunction() == &function) {
      continue;  // a direct call is not the address
    }
    return true;
  }
  return false;
}

}  // namespace

/// Whether the symbol is one of the functions that are themselves selected
/// at the `virtualized` level.
[[nodiscard]] auto is_virtualized(llvm::StringRef symbol, const VmOptions& options) -> bool {
  for (const std::string& name : options.virtualized_symbols) {
    if (name == symbol) {
      return true;
    }
  }
  return false;
}
/// The eligibility gate.
///
/// The checks run in the order of the specification's table, and the first
/// one that holds is the reason that is reported: one reason per function,
/// and a function that fails one is left completely untouched.
[[nodiscard]] auto gate(llvm::Function& function, const VmOptions& options)
    -> std::expected<void, VmFailure> {
  if (function.isDeclaration()) {
    return std::unexpected(
        VmFailure{VmRefusal::unsupported_instruction, "the function has no body"});
  }
  if (has_eh(function)) {
    return std::unexpected(VmFailure{VmRefusal::eh_pad, "the function has an EH edge"});
  }
  if (function.isVarArg()) {
    return std::unexpected(VmFailure{VmRefusal::variadic_function, "the function is variadic"});
  }

  const std::vector<llvm::CallBase*> calls = calls_of(function);
  // The rules of the calls are the rules of the transform: they are
  // reused, not written a second time. The order is the transform's
  // order, so the two phases report the same reason for the same call.
  std::vector<std::optional<CallSkipReason>> reasons;
  reasons.reserve(calls.size());
  for (const llvm::CallBase* call : calls) {
    reasons.push_back(skip_reason(*call));
  }
  for (const CallSkipReason rule : k_call_rules) {
    for (std::size_t index = 0; index < calls.size(); ++index) {
      if (!reasons[index].has_value() || *reasons[index] != rule) {
        continue;
      }
      if (auto failure = vm_failure(rule, *calls[index]); !failure.has_value()) {
        return std::unexpected(failure.error());
      }
    }
  }

  for (llvm::BasicBlock& block : function) {
    for (llvm::Instruction& instruction : block) {
      if (const auto* load = llvm::dyn_cast<llvm::LoadInst>(&instruction)) {
        if (load->isAtomic()) {
          return std::unexpected(VmFailure{VmRefusal::atomic_access, "the load is atomic"});
        }
        if (load->isVolatile()) {
          return std::unexpected(VmFailure{VmRefusal::volatile_access, "the load is volatile"});
        }
      }
      if (const auto* store = llvm::dyn_cast<llvm::StoreInst>(&instruction)) {
        if (store->isAtomic()) {
          return std::unexpected(VmFailure{VmRefusal::atomic_access, "the store is atomic"});
        }
        if (store->isVolatile()) {
          return std::unexpected(VmFailure{VmRefusal::volatile_access, "the store is volatile"});
        }
      }
      if (llvm::isa<llvm::AtomicRMWInst>(instruction) ||
          llvm::isa<llvm::AtomicCmpXchgInst>(instruction) ||
          llvm::isa<llvm::FenceInst>(instruction)) {
        return std::unexpected(VmFailure{VmRefusal::atomic_access, "the instruction is atomic"});
      }
    }
  }

  for (const llvm::CallBase* call : calls) {
    const auto* intrinsic = llvm::dyn_cast<llvm::IntrinsicInst>(call);
    if (intrinsic == nullptr) {
      continue;
    }
    const auto id = intrinsic->getIntrinsicID();
    if (id != llvm::Intrinsic::memcpy && id != llvm::Intrinsic::memset) {
      continue;
    }
    if (const auto* copy = llvm::dyn_cast<llvm::MemCpyInst>(intrinsic)) {
      if (copy->isVolatile()) {
        return std::unexpected(VmFailure{VmRefusal::volatile_access, "the memcpy is volatile"});
      }
    }
    if (const auto* set = llvm::dyn_cast<llvm::MemSetInst>(intrinsic)) {
      if (set->isVolatile()) {
        return std::unexpected(VmFailure{VmRefusal::volatile_access, "the memset is volatile"});
      }
    }
    if (!constant_length(*call).has_value()) {
      return std::unexpected(VmFailure{VmRefusal::dynamic_memcpy,
                                       "the length of the " +
                                           std::string(intrinsic->getCalledFunction()->getName()) +
                                           " is not a constant"});
    }
  }

  if (!supported_result_type(function.getReturnType())) {
    return std::unexpected(
        VmFailure{VmRefusal::unsupported_type,
                  "the return type is not an integer of at most 64 bits nor a pointer"});
  }
  for (const llvm::Argument& argument : function.args()) {
    if (!supported_type(argument.getType())) {
      return std::unexpected(VmFailure{VmRefusal::unsupported_type,
                                       "the type of the argument '" + argument.getName().str() +
                                           "' is not an integer of at most 64 bits nor a pointer"});
    }
  }
  for (const llvm::CallBase* call : calls) {
    const auto* callee = call->getCalledFunction();
    if (callee == nullptr || callee->isIntrinsic()) {
      continue;
    }
    if (!supported_result_type(callee->getReturnType())) {
      return std::unexpected(
          VmFailure{VmRefusal::unsupported_type,
                    "the callee '" + callee->getName().str() +
                        "' returns something that is not an integer nor a pointer"});
    }
    for (const llvm::Argument& argument : callee->args()) {
      if (!supported_type(argument.getType())) {
        return std::unexpected(
            VmFailure{VmRefusal::unsupported_type,
                      "the callee '" + callee->getName().str() +
                          "' takes a parameter that is not an integer nor a pointer"});
      }
    }
  }

  if (address_taken(function)) {
    return std::unexpected(
        VmFailure{VmRefusal::address_taken, "the address of the function is taken"});
  }

  // The budgets. The argument count is the number of the arguments of a
  // call; the slot count is the number of the values the function holds,
  // and it is known once the slots are assigned.
  for (const llvm::CallBase* call : calls) {
    if (call->arg_size() > k_max_call_arguments) {
      return std::unexpected(
          VmFailure{VmRefusal::too_many_arguments,
                    "a call has " + std::to_string(call->arg_size()) + " arguments"});
    }
  }

  for (const llvm::CallBase* call : calls) {
    if (call->getCalledFunction() == &function) {
      return std::unexpected(VmFailure{VmRefusal::recursion, "the function calls itself"});
    }
  }
  for (const llvm::CallBase* call : calls) {
    const auto* callee = call->getCalledFunction();
    if (callee != nullptr && is_virtualized(callee->getName(), options)) {
      return std::unexpected(VmFailure{VmRefusal::virtualized_callee,
                                       "the callee '" + callee->getName().str() +
                                           "' is itself selected at the virtualized level"});
    }
  }

  return {};
}

}  // namespace cs2::protect::detail

namespace cs2::protect {
auto to_string(VmRefusal reason) noexcept -> const char* {
  switch (reason) {
    case VmRefusal::eh_pad:
      return "eh_pad";
    case VmRefusal::variadic_function:
      return "variadic_function";
    case VmRefusal::variadic_callee:
      return "variadic_callee";
    case VmRefusal::indirect_call:
      return "indirect_call";
    case VmRefusal::indirect_parameter:
      return "indirect_parameter";
    case VmRefusal::musttail:
      return "musttail";
    case VmRefusal::volatile_access:
      return "volatile_access";
    case VmRefusal::atomic_access:
      return "atomic_access";
    case VmRefusal::dynamic_memcpy:
      return "dynamic_memcpy";
    case VmRefusal::unsupported_type:
      return "unsupported_type";
    case VmRefusal::local_variable:
      return "local_variable";
    case VmRefusal::address_taken:
      return "address_taken";
    case VmRefusal::too_many_slots:
      return "too_many_slots";
    case VmRefusal::too_many_arguments:
      return "too_many_arguments";
    case VmRefusal::recursion:
      return "recursion";
    case VmRefusal::virtualized_callee:
      return "virtualized_callee";
    case VmRefusal::unsupported_instruction:
      return "unsupported_instruction";
    case VmRefusal::vanished:
      return "vanished";
    default:
      return "unknown";
  }
}

}  // namespace cs2::protect
