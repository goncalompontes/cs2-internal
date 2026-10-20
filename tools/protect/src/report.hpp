#pragma once

// What the pass reports.
//
// The report is the acceptance evidence for the protection pass: it names
// every selected function, and — with CS2_PROTECT_VERBOSE=1 — every function
// that was skipped and why.

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "config.hpp"
#include "selection.hpp"

namespace llvm {
class Module;
}  // namespace llvm

namespace cs2::protect {

/// Prints the selection report of one module to stderr.
void write_report(const llvm::Module& module, const SelectionResult& result, const Config& config);

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
                                  const SelectionResult& result) -> std::optional<std::string>;

}  // namespace cs2::protect
