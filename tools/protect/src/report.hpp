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
[[nodiscard]] auto write_coverage(const Config& config, std::string_view module_id,
                                  const SelectionResult& result) -> std::optional<std::string>;

}  // namespace cs2::protect
