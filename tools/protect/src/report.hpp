#pragma once

// What the pass reports.
//
// The report is the acceptance evidence for the protection pass: it names
// every selected function, and — with CS2_PROTECT_VERBOSE=1 — every function
// that was skipped and why. The transform phase adds its own lines: the
// number of calls it rewrote, the number it skipped per reason, and the
// size of the table and of the descriptor it added to the module.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "config.hpp"
#include "selection.hpp"
#include "transform.hpp"
#include "vm.hpp"

namespace llvm {
class Module;
}  // namespace llvm

namespace cs2::protect {

/// The state of a policy entry, as the coverage records it.
enum class EntryState : std::uint8_t {
  matched,        ///< the entry named a function that is protected.
  unmatched,      ///< the entry named no function in this module.
  blocked,        ///< the entry named a function that cannot be protected.
  unimplemented,  ///< the entry named a function whose level is not implemented.
  vanished,       ///< the entry named a selected function that did not survive
                  ///< to the transform phase.
};

[[nodiscard]] auto to_string(EntryState state) noexcept -> const char*;

/// One entry of the coverage file.
struct CoverageEntry {
  std::string pattern;
  EntryState state{EntryState::unmatched};
  std::string reason;  ///< the blocker or the unimplemented level ("" if none).
};

/// What the transform did to one selected function.
struct CoverageFunction {
  std::string symbol;
  std::size_t rewritten{0};
  std::size_t skipped{0};
  std::size_t cross{0};  ///< the rewrites that were cross-TU (routed).
};

/// What the transform put into the module.
///
/// The payload check needs it to decode the descriptor of the *image* and to
/// compare the resolved address with the table entry: the check has to prove
/// that the dispatched callee is the right one, not only that the number of
/// the dispatches is right.
struct CoverageProtect {
  std::size_t table_size{0};
  std::size_t descriptor_size{0};
  /// The parts of the key, in the order the call site combines them.
  std::vector<std::uint64_t> key_parts;
  /// The multiplier of the mixer, as the call site uses it.
  std::uint64_t mix_multiplier{0};
  /// The xor constant of the mixer, as the call site uses it.
  std::uint64_t mix_xor{0};
};

/// What the virtualizer's analysis decided about one function.
///
/// The analysis is read-only: the function is not rewritten yet, so the
/// numbers are what the virtualized form *would* cost. The operator needs
/// them before paying for the 5-50x.
struct CoverageVm {
  std::string symbol;
  bool virtualizable{false};
  std::string reason;  ///< the refusal reason ("" when the function is
                       ///< virtualizable).
  std::string detail;  ///< what the refusal is about.
  std::size_t slots{0};
  std::size_t local_cells{0};  ///< the number of cells the locals take.
  std::size_t frame_bytes{0};
  std::size_t bytecode_bytes{0};
  std::size_t instructions{0};
  std::size_t calls{0};
};

/// Prints the selection report of one module to stderr.
void write_report(const llvm::Module& module, const SelectionResult& result, const Config& config);

/// Prints the transform report of one module to stderr.
///
/// The report names the functions that were transformed, the number of calls
/// that were rewritten and the number that were skipped per reason. With
/// CS2_PROTECT_VERBOSE=1 it names every skipped call.
void write_transform_report(const llvm::Module& module, const TransformResult& result,
                            const Config& config);

/// Prints the report of the virtualizer's analysis to stderr.
///
/// The analysis is read-only: it says, for every selected function, whether
/// it *could* be virtualized and what the virtualized form would cost. A
/// function that cannot be virtualized is reported with the reason: the
/// gate fails closed, and a refusal that is not reported is a refusal
/// nobody can act on.
void write_vm_report(const llvm::Module& module, const std::vector<VmFunction>& functions,
                     const Config& config);

/// The coverage entries of the virtualizer's analysis.
[[nodiscard]] auto coverage_vm(const std::vector<VmFunction>& functions) -> std::vector<CoverageVm>;

/// The coverage entries of a selection result.
[[nodiscard]] auto coverage_entries(const SelectionResult& result) -> std::vector<CoverageEntry>;

/// The coverage entries of a recorded selection.
///
/// An entry whose selected function did not survive to the transform phase is
/// recorded as *vanished*: the protection would be silently lost, and the
/// whole-build check has to fail for it.
[[nodiscard]] auto coverage_entries(const RecordedSelection& selection, std::string_view module_id,
                                    const std::vector<RecordedFunction>& vanished)
    -> std::vector<CoverageEntry>;

/// Writes the machine-readable coverage of one module.
///
/// A pass sees one translation unit at a time, so it cannot know whether a
/// policy entry matched *somewhere* in the build. The coverage file records
/// what each module decided; tools/protect/check_policy.py reads them all
/// after the build and fails it when an entry matched nothing.
///
/// The file name carries the policy digest and the module's identity *inside
/// the source tree* (its path relative to the root the policy and the source
/// share), so that the coverage of one policy is never mistaken for the
/// coverage of another, and so that two sources never write the same file.
/// The name does not depend on the build directory: two worktrees of one
/// tree write the *same* file, so a rebuild overwrites the coverage instead
/// of leaving a stale copy of it behind (a stale copy would make the
/// payload check count the rewrites twice).
///
/// The file also carries the digest of the source file and a digest of its
/// own content, so that a reader can tell a coverage file of *this* build
/// from one of another build or from a file that was edited by hand.
///
/// The `vm` lines are *new*: a coverage file of an older build has none, and
/// a reader that does not know them still reads the file.
///
/// The `cross` and the `direct` lines are new in the same sense: they
/// record the cross-module routing (the state of every `cross_module` entry
/// and the cross-TU callees that stayed direct). A coverage file of an older
/// build has none, and a reader that does not know them still reads the
/// file.
[[nodiscard]] auto write_coverage(const Config& config, const llvm::Module& module,
                                  const std::vector<CoverageEntry>& entries,
                                  const std::vector<CoverageFunction>& functions,
                                  const CoverageProtect& protect, std::span<const CoverageVm> vm,
                                  const CrossModuleState& cross) -> std::optional<std::string>;

/// Writes the machine-readable coverage of one module from a selection.
[[nodiscard]] auto write_coverage(const Config& config, const llvm::Module& module,
                                  const SelectionResult& result) -> std::optional<std::string>;

}  // namespace cs2::protect
