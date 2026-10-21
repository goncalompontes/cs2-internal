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
/// The file name carries the policy digest and the module identifier, so
/// that the coverage of one policy is never mistaken for the coverage of
/// another, and so that two sources never write the same file. The name does
/// not depend on the build directory, so a rebuild overwrites the file
/// instead of adding to the directory.
[[nodiscard]] auto write_coverage(const Config& config, const llvm::Module& module,
                                  const std::vector<CoverageEntry>& entries,
                                  const std::vector<CoverageFunction>& functions)
    -> std::optional<std::string>;

/// Writes the machine-readable coverage of one module from a selection.
[[nodiscard]] auto write_coverage(const Config& config, const llvm::Module& module,
                                  const SelectionResult& result) -> std::optional<std::string>;

}  // namespace cs2::protect
