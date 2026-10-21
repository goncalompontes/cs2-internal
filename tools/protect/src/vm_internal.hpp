#pragma once

// The internal contract of the front end of the virtualizer.
//
// The front end is three translation units: the gate (the eligibility),
// the lowering (the bytecode of a function) and the encoder (the
// container). This header is what they share: the type and the width
// model, and the two entry points they call each other through.
//
// It is **internal**: the contract with the rest of the world is
// `vm.hpp` and `isa.def`.

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <vector>

#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"

#include "vm.hpp"

namespace cs2::protect::detail {

// --- The type and the width model ---------------------------------------------

/// The width of a type in bytes, or 0 when the virtual machine does not model
/// the type.
///
/// The widths of the ISA are 1, 2, 4 and 8 bytes, so an integer of 8, 16, 32
/// or 64 bits is modelled and nothing else is. An `i1` lives in a byte: the
/// value of an `i1` is its low bit, and the lowering masks every operation
/// whose result is an `i1` (see `mask_if_bool`).
[[nodiscard]] inline auto width_of(llvm::Type* type) -> std::uint8_t {
  if (type->isIntegerTy()) {
    switch (type->getIntegerBitWidth()) {
      case 1:
      case 8:
        return 1;
      case 16:
        return 2;
      case 32:
        return 4;
      case 64:
        return 8;
      default:
        return 0;
    }
  }
  if (type->isPointerTy()) {
    // Only the default address space: the manual mapper knows one address
    // space, and a pointer into another one is not a pointer it can map.
    if (llvm::cast<llvm::PointerType>(type)->getAddressSpace() != 0) {
      return 0;
    }
    return 8;
  }
  return 0;
}

/// Whether the virtual machine models the type.
[[nodiscard]] inline auto supported_type(llvm::Type* type) -> bool {
  return width_of(type) != 0;
}

/// Whether the virtual machine models the type as the type of a *result*.
///
/// A function that returns nothing is modelled: the interpreter returns
/// zero.
[[nodiscard]] inline auto supported_result_type(llvm::Type* type) -> bool {
  return type->isVoidTy() || supported_type(type);
}

/// Whether the type is an `i1`.
[[nodiscard]] inline auto is_bool(llvm::Type* type) -> bool {
  return type->isIntegerTy() && type->getIntegerBitWidth() == 1;
}

/// The length of a `memcpy` or a `memset`, when it is a constant.
[[nodiscard]] inline auto constant_length(const llvm::CallBase& call)
    -> std::optional<std::uint64_t> {
  const auto* intrinsic = llvm::dyn_cast<llvm::MemIntrinsic>(&call);
  if (intrinsic == nullptr) {
    return std::nullopt;
  }
  const auto* length = llvm::dyn_cast<llvm::ConstantInt>(intrinsic->getLength());
  if (length == nullptr) {
    return std::nullopt;
  }
  return length->getZExtValue();
}

// --- The gate and the lowering -------------------------------------------------

/// The eligibility gate: the error is the reason the function cannot be
/// virtualized.
[[nodiscard]] auto gate(llvm::Function& function, const VmOptions& options)
    -> std::expected<void, VmFailure>;

/// What the lowering of a function produced.
struct Lowered {
  std::vector<VmInstruction> code{};
  std::vector<VmPoolEntry> pool{};
  std::vector<VmCallSite> call_sites{};
  std::size_t slots{0};
  std::size_t local_cells{0};
};

/// Lowers the function: the error is the refusal.
[[nodiscard]] auto lower(llvm::Function& function, const VmOptions& options)
    -> std::expected<Lowered, VmFailure>;

}  // namespace cs2::protect::detail
