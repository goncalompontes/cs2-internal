#include "report.hpp"

#include <cstdio>
#include <fstream>
#include <system_error>

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
  if (match.blocked) {
    return "blocked";
  }
  return "unmatched";
}

}  // namespace

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
    if (selection.may_inline) {
      llvm::errs() << " note=noinline-missing";
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
    if (!match.module.empty()) {
      llvm::errs() << " module=" << match.module;
    }
    llvm::errs() << "\n";
  }

  llvm::errs() << "[cs2-protect] " << tag << ": " << result.selected.size() << " selected, "
               << result.skipped.size() << " skipped\n";
}

auto write_coverage(const Config& config, const llvm::Module& module, const SelectionResult& result)
    -> std::optional<std::string> {
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
  for (const PolicyMatch& match : result.policy_matches) {
    text += "entry ";
    text += entry_state(match);
    if (match.blocked && match.blocked_reason.has_value()) {
      text += " ";
      text += to_string(*match.blocked_reason);
    }
    text += " ";
    text += match.pattern;
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

}  // namespace cs2::protect
