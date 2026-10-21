#include "report.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <system_error>
#include <utility>

#include "cipher.hpp"
#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/SHA256.h"
#include "llvm/Support/raw_ostream.h"

namespace cs2::protect {

namespace {

/// The file name of the module, for the report prefix.
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

/// The digest of a string, in hexadecimal.
[[nodiscard]] auto digest_of(std::string_view text) -> std::string {
  llvm::SHA256 hasher;
  hasher.update(text);
  return llvm::toHex(hasher.final());
}

/// The digest of a file, in hexadecimal ("" when it cannot be read).
[[nodiscard]] auto file_digest(const std::filesystem::path& path) -> std::string {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return {};
  }
  const std::string content((std::istreambuf_iterator<char>(input)),
                            std::istreambuf_iterator<char>());
  if (input.bad()) {
    return {};
  }
  return digest_of(content);
}

/// The absolute path of the source file of a module.
///
/// The compiler may be given a relative path -- the build directory is the
/// working directory of the compile -- and the coverage has to name a file
/// that a reader can find: the path is resolved against the working
/// directory and normalised.
[[nodiscard]] auto absolute_source(const llvm::Module& module) -> std::filesystem::path {
  const std::filesystem::path source(module.getSourceFileName());
  if (source.is_absolute()) {
    return source.lexically_normal();
  }
  std::error_code error;
  const std::filesystem::path current = std::filesystem::current_path(error);
  if (error) {
    return source.lexically_normal();
  }
  return (current / source).lexically_normal();
}

/// The longest directory prefix that two paths share.
[[nodiscard]] auto common_root(const std::filesystem::path& first,
                               const std::filesystem::path& second) -> std::filesystem::path {
  std::filesystem::path root;
  auto left = first.begin();
  auto right = second.begin();
  while (left != first.end() && right != second.end() && *left == *right) {
    root /= *left;
    ++left;
    ++right;
  }
  return root;
}

/// The identity of the module inside the source tree.
///
/// The absolute path of the source file is *not* usable as the identity: two
/// worktrees of one tree (or a rebuild after a directory was renamed) would
/// write two coverage files for one module, and the stale one would make the
/// payload check count the rewrites twice. The path relative to the root the
/// policy file and the source file share is the same in every worktree.
///
/// The policy file normally lives in the tree (`tools/protect/`), so the root
/// is the root of the repository. When the policy lives outside the tree, the
/// absolute path is the fallback: the identity then names the worktree, and
/// two worktrees that share the policy write two files for one module into
/// the same coverage directory. The readers therefore only count the coverage
/// files whose source belongs to the tree that is being built (`--source-root`
/// of the checkers), and the coverage of another worktree is never counted.
[[nodiscard]] auto module_identity(const Config& config, const llvm::Module& module)
    -> std::string {
  // The source file name is the path the compiler was given, resolved to an
  // absolute path. A bare name (a module that was built without a source
  // file -- a test fixture, for instance) names nothing, and the module
  // identifier names it instead.
  const std::filesystem::path raw(module.getSourceFileName());
  std::filesystem::path source = raw.parent_path().empty()
                                     ? std::filesystem::path(module.getModuleIdentifier())
                                     : absolute_source(module);
  if (!config.policy_path.empty()) {
    const std::filesystem::path root =
        common_root(config.policy_path.parent_path(), source.parent_path());
    const std::filesystem::path relative = source.lexically_relative(root);
    if (!relative.empty() && relative.is_relative()) {
      return relative.generic_string();
    }
  }
  return source.generic_string();
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

/// A text that goes into the coverage file, without the whitespace.
///
/// The coverage file is a line-based format: a value with a space in it
/// would look like two values.
[[nodiscard]] auto sanitize(std::string_view text) -> std::string {
  std::string result;
  result.reserve(text.size());
  for (const char character : text) {
    if (character == ' ' || character == '\t' || character == '\n' || character == '\r') {
      result.push_back('_');
      continue;
    }
    result.push_back(character);
  }
  return result;
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
      llvm::errs() << " reason=" << to_string(match.applied_level);
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

void write_vm_report(const llvm::Module& module, const std::vector<VmFunction>& functions,
                     const Config& config) {
  const std::string module_id = module.getSourceFileName();
  const std::string_view tag = module_tag(module_id);

  // The seed of the module: the key of the container and the opcode
  // permutation come from it, so the operator needs it next to the
  // verdicts.
  llvm::errs() << "[cs2-protect] " << tag << ": vm seed="
               << module_seed(config, module_id) << "\n";

  for (const VmFunction& function : functions) {
    llvm::errs() << "[cs2-protect] " << tag << ": vm " << function.symbol;
    if (!function.qualified.empty()) {
      llvm::errs() << " (" << function.qualified << ")";
    }
    if (function.virtualizable) {
      llvm::errs() << " virtualizable"
                   << " slots=" << function.slots << " frame=" << function.frame_bytes
                   << " bytecode=" << function.bytecode_bytes
                   << " instructions=" << function.instructions << " calls=" << function.calls;
    } else {
      llvm::errs() << " refused reason=" << to_string(function.failure.reason);
      if (!function.failure.detail.empty()) {
        llvm::errs() << " detail=" << function.failure.detail;
      }
    }
    llvm::errs() << "\n";
  }
}

auto coverage_vm(const std::vector<VmFunction>& functions) -> std::vector<CoverageVm> {
  std::vector<CoverageVm> result;
  result.reserve(functions.size());
  for (const VmFunction& function : functions) {
    CoverageVm entry;
    entry.symbol = function.symbol;
    entry.virtualizable = function.virtualizable;
    if (!function.virtualizable) {
      entry.reason = to_string(function.failure.reason);
      entry.detail = function.failure.detail;
    }
    entry.slots = function.slots;
    entry.frame_bytes = function.frame_bytes;
    entry.bytecode_bytes = function.bytecode_bytes;
    entry.instructions = function.instructions;
    entry.calls = function.calls;
    result.push_back(std::move(entry));
  }
  return result;
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
      // The level that was *applied* to the function, not the level of the
      // entry: the entry did not get its own level.
      entry.reason = to_string(match.applied_level);
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
                    const std::vector<CoverageFunction>& functions, const CoverageProtect& protect,
                    std::span<const CoverageVm> vm) -> std::optional<std::string> {
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
  // two policies apart, and the module's identity inside the source tree
  // keeps two sources apart. The identity does not depend on the build
  // directory, so two worktrees of one tree write the same file and a
  // rebuild overwrites it instead of leaving a stale copy behind.
  const std::string name =
      hex_seed(derive_seed(config.policy_digest, module_identity(config, module))) + ".cov";
  const std::filesystem::path path = config.coverage_dir / name;

  // The whole coverage in one buffer, written in one go: a source that is
  // compiled into two targets of the same build writes the same file, and
  // the content does not depend on the target -- but the write must not be
  // able to interleave with another one.
  std::string text;
  text += "policy-sha256 " + config.policy_digest + "\n";
  text += "module " + module.getSourceFileName() + "\n";
  // The path and the digest of the source file: a reader can tell a coverage
  // file of this build from one of an earlier state of the same source, and
  // it can find the file the digest belongs to.
  const std::filesystem::path source = absolute_source(module);
  const std::string source_digest = file_digest(source);
  if (!source_digest.empty()) {
    text += "source-path " + source.generic_string() + "\n";
    text += "source-sha256 " + source_digest + "\n";
  }
  if (protect.table_size > 0) {
    // What the call site decodes with. The payload check reads it to decode
    // the descriptor of the image and to compare the resolved address with
    // the table entry.
    text += "protect table=" + std::to_string(protect.table_size);
    text += " descriptor=" + std::to_string(protect.descriptor_size);
    text += " multiplier=" + hex_seed(protect.mix_multiplier);
    text += " xor=" + hex_seed(protect.mix_xor);
    for (const std::uint64_t part : protect.key_parts) {
      text += " key=" + hex_seed(part);
    }
    text += "\n";
  }
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
  // What the virtualizer's analysis decided. The lines are new: a coverage
  // file of an older build has none, and a reader that does not know them
  // still reads the file.
  for (const CoverageVm& function : vm) {
    text += "vm ";
    text += function.symbol;
    text += " verdict=";
    text += function.virtualizable ? "virtualizable" : "refused";
    if (!function.reason.empty()) {
      text += " reason=";
      text += function.reason;
    }
    if (!function.detail.empty()) {
      text += " detail=";
      text += sanitize(function.detail);
    }
    text += " slots=";
    text += std::to_string(function.slots);
    text += " frame=";
    text += std::to_string(function.frame_bytes);
    text += " bytecode=";
    text += std::to_string(function.bytecode_bytes);
    text += " instructions=";
    text += std::to_string(function.instructions);
    text += " calls=";
    text += std::to_string(function.calls);
    text += "\n";
  }
  // The digest of everything above: a file that was edited by hand (or
  // truncated) does not describe the build, and a reader that trusts it
  // would count a rewrite that never happened.
  text += "content-sha256 " + digest_of(text) + "\n";

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
  return write_coverage(config, module, coverage_entries(result), {}, CoverageProtect{}, {});
}

}  // namespace cs2::protect
