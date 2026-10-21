#pragma once

// The call-encryption transform.
//
// Inside a *selected* function, a direct call to a target that is defined in
// the same module becomes an indirect dispatch through an encrypted
// descriptor, so the static call graph is no longer readable from the binary.
//
// The transform runs at the *optimizer last* extension point, so it sees the
// fully optimised IR. It has to run there: the optimiser's
// `SimplifyIndirectCalls` would rewrite the encrypted indirect call straight
// back into a direct one, and the inliner would have eaten the selected
// function before any late point could rewrite it.
//
// The two phases hand over through the module's metadata: the early phase
// (the pipeline start) selects the functions and records them, the late phase
// (the optimizer last) reads the record back and rewrites the calls. The
// record is also what lets the late phase notice a selected function that did
// not survive the optimiser.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "config.hpp"
#include "selection.hpp"

namespace llvm {
class Module;
}  // namespace llvm

namespace cs2::protect {

/// The number of constants the key is split into.
///
/// The key is split so that it does not appear as a single constant in the
/// image (the specification's design A).
inline constexpr std::size_t k_key_parts = 3;

// --- The cipher ---------------------------------------------------------------
//
// The cipher lives here, and not in the transform's implementation, because
// the string transform (T21) uses it as well: a second mixer is a mixer that
// can drift from the first one, and the payload check would then decode with
// a different mixer than the pass encrypted with.
//
// The mixer constants do **not** go into the instruction stream as 64-bit
// immediates: the X86 backend of the SDK's LLVM 22.1.8 mis-materialises a
// 64-bit constant in this shape (see `tools/protect/README.md` §12). Every
// 64-bit constant a call site needs lives in a global and is read with a
// volatile load.

/// The multiplier of the mixer (the bits of the golden ratio).
inline constexpr std::uint64_t k_mix_multiplier = 0x9E3779B97F4A7C15ULL;
/// The xor constant of the mixer.
inline constexpr std::uint64_t k_mix_xor = 0xBF58476D1CE4E5B9ULL;
/// How far the mixer rotates its result.
inline constexpr unsigned k_mix_rotate = 31;

/// Rotates a 64-bit value to the left.
[[nodiscard]] inline auto rotate_left(std::uint64_t value, unsigned amount) noexcept
    -> std::uint64_t {
  return (value << amount) | (value >> (64U - amount));
}

/// The mixer. The IR that the transform emits computes exactly this, so the
/// value the pass encrypts with is the value the call site decrypts with.
[[nodiscard]] inline auto mix64(std::uint64_t value) noexcept -> std::uint64_t {
  value *= k_mix_multiplier;
  value ^= k_mix_xor;
  return rotate_left(value, k_mix_rotate);
}

/// The next value of a seeded stream (splitmix64).
[[nodiscard]] inline auto splitmix64(std::uint64_t& state) noexcept -> std::uint64_t {
  state += 0x9E3779B97F4A7C15ULL;
  std::uint64_t value = state;
  value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
  value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
  return value ^ (value >> 31U);
}

/// The seed of one module: the build seed mixed with the module id, so that
/// two modules of one build do not share a key and two builds do not either.
[[nodiscard]] inline auto module_seed(const Config& config, std::string_view module_id) noexcept
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

/// Why a call was not rewritten.
enum class CallSkipReason : std::uint8_t {
  variadic_callee,     ///< the callee is variadic: the indirect call cannot
                       ///< reproduce the variadic ABI.
  indirect_parameter,  ///< the callee has a `byval` / `sret` / `inalloca` /
                       ///< `preallocated` parameter: Windows x64 rejects
                       ///< these in an indirect signature.
  musttail,            ///< the call is a `musttail` call: a tail call cannot
                       ///< become an indirect call.
  external_callee,     ///< the callee is not defined in this module: it is an
                       ///< import, and it is in the import table anyway.
  intrinsic_callee,    ///< the callee is a compiler intrinsic.
  optnone_callee,      ///< the callee is `optnone`: rewriting it would defeat
                       ///< the `optnone` intent.
  invoke,              ///< the call is an `invoke`: the pass does not model the
                       ///< EH edges yet.
  indirect_callee,     ///< the call is already indirect: there is nothing to
                       ///< hide.
};

[[nodiscard]] auto to_string(CallSkipReason reason) noexcept -> const char*;

/// One selected function, as the early phase recorded it.
struct RecordedFunction {
  std::string symbol;
  std::string qualified;
  ProtectionLevel level{ProtectionLevel::call};
};

/// The state of one policy entry, as the early phase recorded it.
///
/// The state is one of "matched", "unmatched", "blocked" and
/// "unimplemented" -- the same words the coverage file uses.
struct RecordedEntry {
  std::string pattern;
  std::string module;  ///< the scope of the entry ("" = any module).
  std::string state;
  std::string reason;  ///< the blocker, or the unimplemented level.
  ProtectionLevel level{ProtectionLevel::call};
};

/// What the early phase recorded for the late phase.
struct RecordedSelection {
  std::vector<RecordedFunction> functions;
  std::vector<RecordedEntry> entries;
};

/// What the transform did to one selected function.
struct FunctionTransform {
  std::string symbol;
  std::string qualified;
  std::size_t rewritten{0};
  std::vector<CallSkipReason> skipped;       ///< one per skipped call.
  std::vector<std::string> skipped_callees;  ///< the callee of each skipped call.
};

/// The outcome of the transform phase for one module.
struct TransformResult {
  std::vector<FunctionTransform> functions;
  std::vector<RecordedFunction> vanished;       ///< the selected functions that did
                                                ///< not survive to the transform.
  std::vector<RecordedFunction> unimplemented;  ///< the selected functions whose
                                                ///< level is not implemented.
  std::size_t table_size{0};
  std::size_t descriptor_size{0};
  /// The parts of the key, in the order the call site combines them.
  ///
  /// The coverage records them, so that the payload check can decode the
  /// descriptor of the image and compare the resolved address with the table
  /// entry: the check must prove that the *dispatched* callee is the right
  /// one, not only that the number of the dispatches is right.
  std::array<std::uint64_t, k_key_parts> key_parts{};
  /// The multiplier of the mixer, as the call site uses it.
  std::uint64_t mix_multiplier{0};
  /// The xor constant of the mixer, as the call site uses it.
  std::uint64_t mix_xor{0};
};

/// Records the selection of the early phase in the module's metadata.
///
/// The metadata is the hand-off between the two phases: it survives every
/// pass between them, and it is the only way the late phase learns which
/// functions the early phase selected.
void record_selection(llvm::Module& module, const SelectionResult& result);

/// Reads the recorded selection back. An empty record means that the early
/// phase did not run (or did not select anything).
[[nodiscard]] auto read_selection(const llvm::Module& module) -> RecordedSelection;

/// Rewrites the calls of the selected functions of one module.
///
/// The module is changed in place: the calls become indirect, and the table,
/// the descriptor and the key constants are added to the module.
[[nodiscard]] auto transform_module(llvm::Module& module, const Config& config,
                                    const RecordedSelection& selection) -> TransformResult;

}  // namespace cs2::protect
