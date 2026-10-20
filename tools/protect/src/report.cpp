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
                 << (match.matched ? "matched" : "unmatched");
    if (!match.module.empty()) {
      llvm::errs() << " module=" << match.module;
    }
    llvm::errs() << "\n";
  }

  llvm::errs() << "[cs2-protect] " << tag << ": " << result.selected.size() << " selected, "
               << result.skipped.size() << " skipped\n";
}

auto write_coverage(const Config& config, std::string_view module_id, const SelectionResult& result)
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

  // One file per module: two translation units never write the same file, so
  // the parallel build does not need a lock.
  const std::string name = hex_seed(derive_seed(module_id, "")) + ".cov";
  const std::filesystem::path path = config.coverage_dir / name;

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return "cannot write the coverage file '" + path.string() + "'";
  }
  out << "policy-sha256 " << config.policy_digest << "\n";
  out << "module " << module_id << "\n";
  for (const PolicyMatch& match : result.policy_matches) {
    out << "entry " << (match.matched ? "matched" : "unmatched") << " " << match.pattern << "\n";
  }
  out.flush();
  if (!out) {
    return "cannot write the coverage file '" + path.string() + "'";
  }
  return std::nullopt;
}

}  // namespace cs2::protect
