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
