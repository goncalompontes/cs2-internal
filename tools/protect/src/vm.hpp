#pragma once

// The front end of the virtualizer: the ISA, the eligibility gate, the
// lowering and the encoder.
//
// This is the *front end*: it decides whether a function can be virtualized,
// lowers its optimised IR to the bytecode of a virtual machine, and encodes
// that bytecode. It does not rewrite the function and it does not contain
// the interpreter: the rewrite and the interpreter are the next task, and
// they read this file (and `isa.def`) for the contract.
//
// Three things are contracts, and they are written down here because the two
// sides of the virtual machine have to agree on them:
//
//   * the ISA (`isa.def`): the opcodes, their canonical numbers, and the
//     shape of an instruction;
//   * the frame layout: the cells of the frame and what each region holds;
//   * the width model: what a slot holds, and how an operation truncates.
//
// Everything here is a *value*: the pass must never throw out into the
// driver, which is built without exceptions. A function that cannot be
// virtualized is a *refusal* with a reason, never an error.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cipher.hpp"
#include "config.hpp"

namespace llvm {
class Constant;
class Function;
class Module;
}  // namespace llvm

namespace cs2::protect {

// --- The ISA -----------------------------------------------------------------

/// The opcodes of the virtual machine.
///
/// The numbers are the *canonical* ones: they are dense and 0-based, and
/// they are what the interpreter's switch uses. The number that appears in
/// the bytecode is the permuted one (see `permuted_opcode`), so the
/// canonical number never appears in the image.
enum class VmOpcode : std::uint8_t {
#define CS2_VM_OPCODE(name, number, comment) name = static_cast<std::uint8_t>(number),
#include "isa.def"
#undef CS2_VM_OPCODE
};

/// The number of opcodes: the modulus of the permutation.
inline constexpr std::size_t k_opcode_count = [] {
  std::size_t count = 0;
#define CS2_VM_OPCODE(name, number, comment) ++count;
#include "isa.def"
#undef CS2_VM_OPCODE
  return count;
}();

/// The comparison kinds of `Cmp` (the `aux` of the instruction).
enum class VmCompare : std::uint8_t {
  eq,
  ne,
  ugt,
  uge,
  ult,
  ule,
  sgt,
  sge,
  slt,
  sle,
};

/// The cast kinds of `Cast` (the low byte of the `aux` of the instruction).
enum class VmCast : std::uint8_t {
  trunc,    ///< the low bits of the source.
  bitcast,  ///< the same bits, in a type of the same width.
  zext,     ///< the source, zero-extended.
  sext,     ///< the source, sign-extended.
};

/// The `nsw` flag: the operation may not overflow.
inline constexpr std::uint8_t k_flag_nsw = 1U << 0U;
/// The `nuw` flag: the operation may not wrap.
inline constexpr std::uint8_t k_flag_nuw = 1U << 1U;
/// The `exact` flag: the division may not have a remainder, the shift may not
/// lose a bit.
inline constexpr std::uint8_t k_flag_exact = 1U << 2U;

/// The slot index that means "there is no slot".
///
/// The budget is 256 slots, so 0xFFFF cannot be a slot: the encoding has a
/// value for "no destination" and for "no source", and the two do not need
/// a special case in the interpreter.
inline constexpr std::uint16_t k_no_slot = 0xFFFFU;

/// The size of an instruction, in bytes.
inline constexpr std::size_t k_instruction_bytes = 24;

/// The size of the header, in bytes.
inline constexpr std::size_t k_header_bytes = 32;

/// The magic of the container: 'C', 'S', '2', 'V'.
inline constexpr std::uint32_t k_vm_magic = 0x56325343U;

/// The version of the container.
inline constexpr std::uint32_t k_vm_version = 1;

/// The header's flag: the container is encrypted.
inline constexpr std::uint32_t k_vm_flag_encrypted = 1U << 0U;
/// The header's flag: the opcodes are permuted.
inline constexpr std::uint32_t k_vm_flag_permuted = 1U << 1U;

// --- The budgets and the frame -----------------------------------------------

/// The number of value slots a function may use.
///
/// A function that needs more is refused, not truncated: a truncated
/// function is a miscompile.
inline constexpr std::size_t k_max_slots = 256;

/// The number of arguments a call may have.
inline constexpr std::size_t k_max_call_arguments = 64;

/// The number of cells of the value region of the frame.
inline constexpr std::size_t k_frame_value_slots = k_max_slots;

/// The number of cells of the argument-stack region of the frame.
///
/// The region holds the values that were pushed for a call, and the values of
/// a parallel copy (a PHI's edge assignments): the deepest stack is a call
/// with the most arguments, or a parallel copy with the most moves.
inline constexpr std::size_t k_frame_argument_slots = k_max_call_arguments;

/// The number of cells of the call-scratch region of the frame.
///
/// The region holds the arguments of the call in flight: the interpreter
/// copies them out of the argument stack and hands them to the thunk.
inline constexpr std::size_t k_frame_call_slots = k_max_call_arguments;

/// The number of cells of the frame.
///
/// The frame is a constant: the rewritten function allocates exactly this,
/// so the interpreter's frame is always the same shape and the slot indices
/// are absolute. The layout is
///
///     [0, slot_count)                  the value slots
///     [slot_count, +local_cells)       the locals
///     [slot_count + local_cells, +arg_slots)         the argument stack
///     [slot_count + local_cells + arg_slots, +call)  the call scratch
///
/// and the header of the bytecode carries the sizes, so the interpreter
/// reads the layout out of the header instead of assuming it.
inline constexpr std::size_t k_frame_cells =
    k_frame_value_slots + k_frame_argument_slots + k_frame_call_slots;

/// The size of a cell of the frame, in bytes.
inline constexpr std::size_t k_frame_cell_bytes = 8;

/// The size of the frame, in bytes.
inline constexpr std::size_t k_frame_bytes = k_frame_cells * k_frame_cell_bytes;

// --- The refusal reasons ------------------------------------------------------

/// Why a function cannot be virtualized.
///
/// The gate fails *closed*: a function that cannot be lowered completely is
/// left completely untouched, and the reason is reported. There is no "best
/// effort" path: a half-lowered function computes garbage, and in a manually
/// mapped payload with no exception tables that kills the game.
enum class VmRefusal : std::uint8_t {
  eh_pad,                   ///< the function has an EH pad, an `invoke`, a
                            ///< `landingpad` or a `resume`: the interpreter cannot
                            ///< model unwinding.
  variadic_function,        ///< the function itself is variadic.
  variadic_callee,          ///< it calls a variadic callee: the indirect call
                            ///< cannot reproduce the variadic ABI.
  indirect_call,            ///< it makes an indirect call: there is no relocatable
                            ///< address for the pool.
  indirect_parameter,       ///< a callee has a `byval` / `sret` / `inalloca` /
                            ///< `preallocated` parameter: Windows x64 rejects these
                            ///< in an indirect signature.
  musttail,                 ///< the tail-call contract does not survive the rewrite.
  volatile_access,          ///< a volatile access: the ordering guarantees are not
                            ///< modelled.
  atomic_access,            ///< an atomic access: the ordering guarantees are not
                            ///< modelled.
  dynamic_memcpy,           ///< a `memcpy` / `memset` with a non-constant length:
                            ///< it would lower to a CRT call.
  unsupported_type,         ///< a float, a vector, an aggregate or an address
                            ///< wider than 64 bits: this task handles integers of
                            ///< at most 64 bits and pointers only.
  local_variable,           ///< the function has a local variable that the
                            ///< frame cannot hold: the frame is a constant, so
                            ///< the local gets a byte offset into the frame
                            ///< arena, and a local that does not fit is
                            ///< refused.
  address_taken,            ///< the address of the function is taken (`&F`): the
                            ///< rewrite changes what the address means.
  too_many_slots,           ///< the function needs more slots than the budget.
  too_many_arguments,       ///< a call needs more arguments than the budget.
  recursion,                ///< the function calls itself: the frame would multiply
                            ///< by the recursion depth.
  virtualized_callee,       ///< the function calls a function that is itself
                            ///< selected at the `virtualized` level: the
                            ///< interpreter would have to call into another
                            ///< interpreter.
  unsupported_instruction,  ///< the catch-all: an IR instruction the lowering
                            ///< does not model. The lowering has a default branch
                            ///< that refuses.
  vanished,                 ///< the function did not survive to this phase:
                            ///< there is nothing to virtualize. The transform
                            ///< reports the same thing the same way.
};

[[nodiscard]] auto to_string(VmRefusal reason) noexcept -> const char*;

/// A refusal and what it is about.
struct VmFailure {
  VmRefusal reason{VmRefusal::unsupported_instruction};
  std::string detail;  ///< what the refusal is about ("the return type is a
                       ///< struct", "the memcpy length is not constant", ...).
};

// --- The bytecode -------------------------------------------------------------

/// One instruction of the virtual machine.
///
/// The encoding is 24 bytes:
///
///     u8 opcode | u8 width | u8 flags | u8 pad | u16 dst | u16 src1 |
///     u16 src2 | u16 src3 | u32 aux | u64 imm
///
/// * `opcode` is the *permuted* opcode (the canonical one never appears in
///   the image);
/// * `width` is the width of the operation, in bytes (1, 2, 4 or 8);
/// * `flags` carries `nsw` (bit 0), `nuw` (bit 1) and `exact` (bit 2);
/// * `dst`, `src1`, `src2` and `src3` are slot indices (`k_no_slot` when the
///   operand is absent);
/// * `aux` carries the sub-opcode: the kind of a comparison, the kind and
///   the source width of a cast, the target of a branch, the size of a
///   memory access;
/// * `imm` carries the immediate (and the index of the pool for `Call` and
///   `Pool`).
struct VmInstruction {
  VmOpcode opcode{VmOpcode::Const};
  std::uint8_t width{0};
  std::uint8_t flags{0};
  std::uint16_t dst{k_no_slot};
  std::uint16_t src1{k_no_slot};
  std::uint16_t src2{k_no_slot};
  std::uint16_t src3{k_no_slot};
  std::uint32_t aux{0};
  std::uint64_t imm{0};
};

/// The header of the container.
struct VmHeader {
  std::uint32_t magic{k_vm_magic};
  std::uint32_t version{k_vm_version};
  std::uint32_t slot_count{0};  ///< the number of value slots.
  std::uint32_t arg_slots{0};   ///< the size of the argument region.
  std::uint32_t call_slots{0};  ///< the size of the call scratch.
  std::uint32_t instruction_count{0};
  std::uint32_t flags{0};
  std::uint32_t local_cells{0};  ///< the number of cells the locals take: they
                                 ///< are the cells above the value slots.
};

/// One entry of the pool.
///
/// The pool is a *relocatable* global: the mapper applies the DIR64
/// fixups, so the entries are the real addresses at run time. The bytecode
/// holds the *index* of an entry, never an address.
struct VmPoolEntry {
  std::string symbol;    ///< the name of the callee or of the global.
  bool is_callee{true};  ///< true for the callee of a call site.
  /// The constant the entry holds: the address of the callee, of the global
  /// or of the constant expression the global is reached through.
  ///
  /// The next task puts these into the pool global; the linker folds the
  /// constant expressions into the relocations of the image.
  const llvm::Constant* value{nullptr};
};

/// One call site of the lowered function.
///
/// The thunk of the call site (the next task) needs the callee's signature:
/// the interpreter calls through a uniform signature (`u64 thunk(u64*
/// scratch)`), and the thunk adapts the real callee to it.
struct VmCallSite {
  std::string callee;                       ///< the symbol of the callee.
  std::vector<std::string> argument_types;  ///< the type of every argument.
  std::string return_type;                  ///< the type of the return value.
  std::size_t pool_index{0};                ///< the entry of the pool.
  std::size_t argument_count{0};
};

// --- The verdict ---------------------------------------------------------------

/// What the analysis decided about one function.
struct VmFunction {
  std::string symbol;
  std::string qualified;
  bool virtualizable{false};
  VmFailure failure{};                   ///< the refusal, when the function is refused.
  std::size_t slots{0};                  ///< the number of value slots.
  std::size_t local_cells{0};            ///< the number of cells the locals take.
  std::size_t frame_bytes{0};            ///< the size of the frame, in bytes.
  std::size_t instructions{0};           ///< the number of bytecode instructions.
  std::size_t calls{0};                  ///< the number of call sites.
  std::size_t bytecode_bytes{0};         ///< the size of the encoded container.
  VmHeader header{};                     ///< the header of the container.
  std::vector<VmInstruction> code{};     ///< the instructions, in the clear.
  std::vector<VmPoolEntry> pool{};       ///< the entries of the pool.
  std::vector<VmCallSite> call_sites{};  ///< the call sites.
  std::array<std::uint64_t, k_key_parts> key_parts{};  ///< the parts of the key.
  std::uint64_t key{0};                                ///< the key the container is encrypted with.
  std::uint64_t perm_a{1};                             ///< the multiplier of the permutation.
  std::uint64_t perm_b{0};                             ///< the offset of the permutation.
  std::vector<std::uint8_t> blob{};                    ///< the encoded container, encrypted.
};

/// What the analysis needs besides the function itself.
struct VmOptions {
  /// The seed of the module: the key and the permutation come from it, so
  /// two builds of the same source differ and the interpreter of the next
  /// task derives the same constants.
  std::uint64_t seed{0};
  /// The symbols of the functions that are selected at the `virtualized`
  /// level: a call to one of them is refused, because the interpreter would
  /// have to call into another interpreter.
  std::span<const std::string> virtualized_symbols{};
};

// --- The API --------------------------------------------------------------------

/// The opcode as it appears in the bytecode: the canonical number, permuted.
[[nodiscard]] auto permuted_opcode(VmOpcode opcode, std::uint64_t a, std::uint64_t b) noexcept
    -> std::uint8_t;

/// The canonical opcode of a byte of the bytecode: the inverse of the
/// permutation.
[[nodiscard]] auto canonical_opcode(std::uint8_t permuted, std::uint64_t a,
                                    std::uint64_t b) noexcept -> VmOpcode;

/// The permutation of the opcodes: the multiplier (odd, and coprime with the
/// number of opcodes) and the offset, both from the module seed.
[[nodiscard]] auto opcode_permutation(std::uint64_t seed) noexcept
    -> std::pair<std::uint64_t, std::uint64_t>;

/// Analyses one function: the eligibility gate, the lowering and the
/// encoding.
///
/// The function is never modified: the analysis is a verdict, and the
/// rewrite is the next task. A function that cannot be virtualized comes
/// back with a refusal and a reason.
[[nodiscard]] auto analyze_function(llvm::Function& function, const VmOptions& options)
    -> VmFunction;

/// Analyses the selected functions of a module.
[[nodiscard]] auto analyze_module(llvm::Module& module, const Config& config,
                                  std::span<const std::string> symbols,
                                  std::span<const std::string> virtualized_symbols)
    -> std::vector<VmFunction>;

}  // namespace cs2::protect
