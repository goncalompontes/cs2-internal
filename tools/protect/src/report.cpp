#include "report.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <system_error>
#include <utility>

#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

namespace cs2::protect {

namespace {

/// The file name of the source file, for the report prefix.
[[nodiscard]] auto module_tag(std::string_view module_id) noexcept -> std::string_view {
  const auto slash = module_id.find_last_of("/\\");
  return slash == std::string_view::npos ? module_id : module_id.substr(slash + 1);
}

/// The seed as a fixed-width hexadecimal number.
[[nodiscard]] auto hex_seed(std::uint64_t seed) -> std::string {
  char buffer[19];
  std::snprintf(buffer, sizeof(buffer), "0x%016llx", static_cast<unsigned long long>(seed));
  return std::string(buffer);
}

/// The state of a policy entry in one module.
[[nodiscard]] auto entry_state(const PolicyMatch& match) -> std::string_view {
  if (match.matched) {
    return "matched";
  }
  if (match.unimplemented) {
    return "unimplemented";
  }
  if (match.blocked) {
    return "blocked";
  }
  return "unmatched";
}

/// The state of a recorded policy entry.
[[nodiscard]] auto entry_state(std::string_view state) -> EntryState {
  if (state == "matched") {
    return EntryState::matched;
  }
  if (state == "blocked") {
    return EntryState::blocked;
  }
  if (state == "unimplemented") {
    return EntryState::unimplemented;
  }
  return EntryState::unmatched;
}

/// The number of skipped calls per reason.
[[nodiscard]] auto skip_counts(const FunctionTransform& transform)
    -> std::map<CallSkipReason, std::size_t> {
  std::map<CallSkipReason, std::size_t> counts;
  for (const CallSkipReason reason : transform.skipped) {
    ++counts[reason];
  }
  return counts;
}

}  // namespace

[[nodiscard]] auto to_string(EntryState state) noexcept -> const char* {
  switch (state) {
    case EntryState::matched:
      return "matched";
    case EntryState::blocked:
      return "blocked";
    case EntryState::unimplemented:
      return "unimplemented";
    case EntryState::vanished:
      return "vanished";
    case EntryState::unmatched:
    default:
      return "unmatched";
  }
}

void write_report(const llvm::Module& module, const SelectionResult& result, const Config& config) {
  const std::string module_id = module.getSourceFileName();
  const std::string_view tag = module_tag(module_id);

  for (const Selection& selection : result.selected) {
    llvm::errs() << "[cs2-protect] " << tag << ": protect " << selection.symbol;
    if (!selection.qualified.empty()) {
      llvm::errs() << " (" << selection.qualified << ")";
    }
    llvm::errs() << " source=" << to_string(selection.source)
                 << " level=" << to_string(selection.level) << " seed=" << hex_seed(selection.seed);
    if (selection.marked_noinline) {
      // The function is marked `noinline`, so that it survives to the late
      // point where its calls are rewritten. The marking changes the code,
      // so the report has to name it.
      llvm::errs() << " noinline=added";
    }
    if (selection.level != ProtectionLevel::call) {
      llvm::errs() << " level-not-implemented";
    }
    llvm::errs() << "\n";
  }

  if (!config.verbose) {
    return;
  }

  for (const Skipped& skipped : result.skipped) {
    llvm::errs() << "[cs2-protect] " << tag << ": skip " << skipped.symbol;
    if (!skipped.qualified.empty()) {
      llvm::errs() << " (" << skipped.qualified << ")";
    }
    llvm::errs() << " reason=" << to_string(skipped.reason) << "\n";
  }

  for (const PolicyMatch& match : result.policy_matches) {
    llvm::errs() << "[cs2-protect] " << tag << ": policy " << match.pattern << " "
                 << entry_state(match);
    if (match.blocked && match.blocked_reason.has_value()) {
      llvm::errs() << " reason=" << to_string(*match.blocked_reason);
    }
    if (match.unimplemented) {
      llvm::errs() << " reason=" << to_string(match.level);
    }
    if (!match.module.empty()) {
      llvm::errs() << " module=" << match.module;
    }
    llvm::errs() << "\n";
  }

  llvm::errs() << "[cs2-protect] " << tag << ": " << result.selected.size() << " selected, "
               << result.skipped.size() << " skipped\n";
}

void write_transform_report(const llvm::Module& module, const TransformResult& result,
                            const Config& config) {
  const std::string module_id = module.getSourceFileName();
  const std::string_view tag = module_tag(module_id);

  for (const FunctionTransform& transform : result.functions) {
    llvm::errs() << "[cs2-protect] " << tag << ": transform " << transform.symbol;
    if (!transform.qualified.empty()) {
      llvm::errs() << " (" << transform.qualified << ")";
    }
    llvm::errs() << " rewritten=" << transform.rewritten << " skipped=" << transform.skipped.size();
    const auto counts = skip_counts(transform);
    for (const auto& [reason, count] : counts) {
      llvm::errs() << " " << to_string(reason) << "=" << count;
    }
    llvm::errs() << "\n";
  }

  for (const RecordedFunction& function : result.vanished) {
    llvm::errs() << "[cs2-protect] " << tag << ": transform " << function.symbol;
    if (!function.qualified.empty()) {
      llvm::errs() << " (" << function.qualified << ")";
    }
    llvm::errs() << " vanished: the selected function did not survive to the transform phase\n";
  }

  for (const RecordedFunction& function : result.unimplemented) {
    llvm::errs() << "[cs2-protect] " << tag << ": transform " << function.symbol;
    if (!function.qualified.empty()) {
      llvm::errs() << " (" << function.qualified << ")";
    }
    llvm::errs() << " level=" << to_string(function.level)
                 << " not-implemented: the level is not implemented yet (T20)\n";
  }

  if (config.verbose) {
    for (const FunctionTransform& transform : result.functions) {
      for (std::size_t index = 0; index < transform.skipped.size(); ++index) {
        llvm::errs() << "[cs2-protect] " << tag << ": transform " << transform.symbol
                     << " skip reason=" << to_string(transform.skipped[index])
                     << " callee=" << transform.skipped_callees[index] << "\n";
      }
    }
  }

  llvm::errs() << "[cs2-protect] " << tag << ": transform table=" << result.table_size
               << " descriptor=" << result.descriptor_size << "\n";
}

auto coverage_entries(const SelectionResult& result) -> std::vector<CoverageEntry> {
  std::vector<CoverageEntry> entries;
  entries.reserve(result.policy_matches.size());
  for (const PolicyMatch& match : result.policy_matches) {
    CoverageEntry entry;
    entry.pattern = match.pattern;
    if (match.matched) {
      entry.state = EntryState::matched;
    } else if (match.unimplemented) {
      entry.state = EntryState::unimplemented;
      entry.reason = to_string(match.level);
    } else if (match.blocked) {
      entry.state = EntryState::blocked;
      if (match.blocked_reason.has_value()) {
        entry.reason = to_string(*match.blocked_reason);
      }
    } else {
      entry.state = EntryState::unmatched;
    }
    entries.push_back(std::move(entry));
  }
  return entries;
}

auto coverage_entries(const RecordedSelection& selection, std::string_view module_id,
                      const std::vector<RecordedFunction>& vanished) -> std::vector<CoverageEntry> {
  std::vector<CoverageEntry> entries;
  entries.reserve(selection.entries.size());
  for (const RecordedEntry& recorded : selection.entries) {
    CoverageEntry entry;
    entry.pattern = recorded.pattern;
    entry.state = entry_state(recorded.state);
    entry.reason = recorded.reason;
    if (entry.state == EntryState::matched || entry.state == EntryState::unimplemented) {
      // A selected function that did not survive to the transform phase: the
      // protection would be silently lost, so the entry is recorded as
      // vanished and the whole-build check fails for it.
      for (const RecordedFunction& function : vanished) {
        if (!module_applies(recorded.module, module_id)) {
          continue;
        }
        if (glob_match(recorded.pattern, function.symbol) ||
            (!function.qualified.empty() && glob_match(recorded.pattern, function.qualified))) {
          entry.state = EntryState::vanished;
          entry.reason.clear();
          break;
        }
      }
    }
    entries.push_back(std::move(entry));
  }
  return entries;
}

auto write_coverage(const Config& config, const llvm::Module& module,
                    const std::vector<CoverageEntry>& entries,
                    const std::vector<CoverageFunction>& functions) -> std::optional<std::string> {
  if (!config.policy) {
    return std::nullopt;
  }

  std::error_code error;
  std::filesystem::create_directories(config.coverage_dir, error);
  if (error) {
    return "cannot create the coverage directory '" + config.coverage_dir.string() +
           "': " + error.message();
  }

  // One file per (policy, module): the policy digest keeps the coverage of
  // two policies apart, and the module identifier keeps two sources apart.
  // The name does not depend on the build directory, so a rebuild in
  // another build directory overwrites the file instead of adding to the
  // directory.
  const std::string name =
      hex_seed(derive_seed(config.policy_digest, module.getModuleIdentifier())) + ".cov";
  const std::filesystem::path path = config.coverage_dir / name;

  // The whole coverage in one buffer, written in one go: a source that is
  // compiled into two targets of the same build writes the same file, and
  // the content does not depend on the target -- but the write must not be
  // able to interleave with another one.
  std::string text;
  text += "policy-sha256 " + config.policy_digest + "\n";
  text += "module " + module.getSourceFileName() + "\n";
  for (const CoverageEntry& entry : entries) {
    text += "entry ";
    text += to_string(entry.state);
    if (!entry.reason.empty()) {
      text += " ";
      text += entry.reason;
    }
    text += " ";
    text += entry.pattern;
    text += "\n";
  }
  // What the transform did: the checker does not read it, but the payload
  // check does -- the number of the encrypted dispatches in the binary has
  // to be the number the pass reports here.
  for (const CoverageFunction& function : functions) {
    text += "fn ";
    text += function.symbol;
    text += " rewritten=";
    text += std::to_string(function.rewritten);
    text += " skipped=";
    text += std::to_string(function.skipped);
    text += "\n";
  }

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return "cannot write the coverage file '" + path.string() + "'";
  }
  out.write(text.data(), static_cast<std::streamsize>(text.size()));
  out.flush();
  if (!out) {
    return "cannot write the coverage file '" + path.string() + "'";
  }
  return std::nullopt;
}

auto write_coverage(const Config& config, const llvm::Module& module, const SelectionResult& result)
    -> std::optional<std::string> {
  return write_coverage(config, module, coverage_entries(result), {});
}

}  // namespace cs2::protect
