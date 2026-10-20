#include "config.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/SHA256.h"
#include "llvm/Support/raw_ostream.h"

namespace cs2::protect {

namespace {

// --- The policy reader ------------------------------------------------------
// The reader is the SDK's (`llvm::json::parse`): the LLVM SDK ships a JSON
// parser, `LLVMSupport` is linked anyway, and a hand-rolled reader is a
// parser to maintain (and to get wrong: the old one accepted "1.2.3" as a
// number and died with an access violation on a deeply nested file).
//
// Two checks sit on top of the SDK's parser, because the SDK's parser alone
// is not enough:
//
//   * the nesting depth — the SDK's parser recurses, so a policy file that
//     nests thousands of levels deep overflows the compiler's stack and
//     kills the *compiler* with no message at all. The depth is therefore
//     bounded before the text reaches the parser;
//   * duplicate keys — the SDK's parser keeps the first of two equal keys,
//     so a duplicated key would be silently ignored while the checker (which
//     reads the file with Python's `json`) would see the last one. The
//     duplicate is rejected instead, on both sides.

/// The deepest nesting a policy file may have.
constexpr int MAX_POLICY_DEPTH = 64;

/// The UTF-8 byte order mark. Some Windows editors write one, and the SDK's
/// parser rejects it.
constexpr std::string_view k_utf8_bom = "\xEF\xBB\xBF";

/// What the pre-scan found.
struct ScanResult {
  int depth{0};
};

/// Scans the policy text for the two things the SDK's parser does not check.
///
/// The scan is iterative: it must not be the thing that overflows the stack
/// when the file is pathological.
[[nodiscard]] auto scan_policy(std::string_view text) -> std::expected<ScanResult, std::string> {
  // One frame per open container. An object remembers the keys it has seen,
  // so a duplicate can be named; an array has no keys.
  struct Frame {
    bool is_object{false};
    bool expects_key{false};
    std::set<std::string> keys;
  };

  ScanResult result;
  std::vector<Frame> stack;
  std::string token;
  bool in_string{false};
  bool escaped{false};

  for (const char c : text) {
    if (in_string) {
      if (escaped) {
        escaped = false;
        token.push_back(c);
      } else if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        in_string = false;
        if (!stack.empty() && stack.back().is_object && stack.back().expects_key) {
          stack.back().expects_key = false;
          if (!stack.back().keys.insert(token).second) {
            return std::unexpected(std::string("the policy file has a duplicate key '") + token +
                                   "'");
          }
        }
        token.clear();
      } else {
        token.push_back(c);
      }
      continue;
    }
    switch (c) {
      case '"':
        in_string = true;
        token.clear();
        break;
      case '{':
        stack.push_back(Frame{.is_object = true, .expects_key = true});
        break;
      case '[':
        stack.push_back(Frame{.is_object = false, .expects_key = false});
        break;
      case '}':
      case ']':
        if (!stack.empty()) {
          stack.pop_back();
        }
        break;
      case ',':
        if (!stack.empty() && stack.back().is_object) {
          stack.back().expects_key = true;
        }
        break;
      default:
        break;
    }
    result.depth = std::max(result.depth, static_cast<int>(stack.size()));
  }
  return result;
}

/// Trims the white space around an environment value.
///
/// `set VAR=value && something` in cmd.exe leaves a trailing space in the
/// value, and a value that a user wrote by hand may carry a newline.
[[nodiscard]] auto trim(std::string_view text) noexcept -> std::string_view {
  const auto is_space = [](char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
  };
  while (!text.empty() && is_space(text.front())) {
    text.remove_prefix(1);
  }
  while (!text.empty() && is_space(text.back())) {
    text.remove_suffix(1);
  }
  return text;
}

/// Parses a hexadecimal seed ("0xA0..." or "A0...").
///
/// The seed is 64 bits. A longer seed keeps its **low 64 bits** — the
/// example policy of the spec carries a 128-bit value — and the truncation
/// is reported (see `seed_is_truncated`).
[[nodiscard]] auto parse_seed(std::string_view text) -> std::expected<std::uint64_t, std::string> {
  std::string_view digits = text;
  if (digits.size() > 2 && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X')) {
    digits.remove_prefix(2);
  }
  if (digits.empty()) {
    return std::unexpected(std::string("the seed is empty"));
  }
  std::uint64_t value = 0;
  for (const char c : digits) {
    value <<= 4U;
    if (c >= '0' && c <= '9') {
      value |= static_cast<std::uint64_t>(c - '0');
    } else if (c >= 'a' && c <= 'f') {
      value |= static_cast<std::uint64_t>(c - 'a' + 10);
    } else if (c >= 'A' && c <= 'F') {
      value |= static_cast<std::uint64_t>(c - 'A' + 10);
    } else {
      return std::unexpected(std::string("the seed is not hexadecimal: '") + std::string(text) +
                             "'");
    }
  }
  return value;
}

/// Whether the seed text has more digits than the 64-bit seed holds.
[[nodiscard]] auto seed_is_truncated(std::string_view text) noexcept -> bool {
  std::string_view digits = text;
  if (digits.size() > 2 && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X')) {
    digits.remove_prefix(2);
  }
  return digits.size() > 16;
}

/// Reports a seed that did not fit into the 64 bits of the seed.
void report_truncated_seed(std::string_view origin, std::string_view text) {
  llvm::errs() << "[cs2-protect] " << origin << ": the seed is 64 bits; the high bits of '" << text
               << "' are dropped\n";
}

[[nodiscard]] auto parse_level(std::string_view text)
    -> std::expected<ProtectionLevel, std::string> {
  if (text == "call") {
    return ProtectionLevel::call;
  }
  if (text == "virtualized") {
    return ProtectionLevel::virtualized;
  }
  return std::unexpected(std::string("unknown protection level: '") + std::string(text) +
                         "' (expected 'call' or 'virtualized')");
}

/// The keys of a JSON object, sorted.
///
/// The SDK's object is a hash map, so the order of its keys is not
/// specified: sorting them makes the error message the same on every run,
/// and the same as the one the checker prints.
[[nodiscard]] auto sorted_keys(const llvm::json::Object& object) -> std::vector<std::string> {
  std::vector<std::string> keys;
  keys.reserve(object.size());
  for (const auto& entry : object) {
    keys.emplace_back(entry.first.str());
  }
  std::ranges::sort(keys);
  return keys;
}

/// Reads one `protect` entry: a bare pattern, or an object with the pattern
/// and the optional module scope and level.
[[nodiscard]] auto parse_protect_entry(const llvm::json::Value& value)
    -> std::expected<PolicyEntry, std::string> {
  PolicyEntry entry;
  if (const auto pattern = value.getAsString(); pattern.has_value()) {
    entry.pattern = std::string(*pattern);
    return entry;
  }
  const llvm::json::Object* object = value.getAsObject();
  if (object == nullptr) {
    return std::unexpected(std::string("a 'protect' entry must be a string or an object"));
  }
  const auto pattern = object->getString("symbol");
  if (!pattern.has_value()) {
    return std::unexpected(std::string("a 'protect' object needs a 'symbol' string"));
  }
  entry.pattern = std::string(*pattern);
  if (const auto module = object->getString("module"); module.has_value()) {
    entry.module = std::string(*module);
  } else if (object->get("module") != nullptr) {
    return std::unexpected(std::string("the 'module' of a 'protect' entry must be a string"));
  }
  if (const auto level = object->getString("level"); level.has_value()) {
    auto parsed = parse_level(*level);
    if (!parsed) {
      return std::unexpected(parsed.error());
    }
    entry.level = *parsed;
  } else if (object->get("level") != nullptr) {
    return std::unexpected(std::string("the 'level' of a 'protect' entry must be a string"));
  }
  for (const std::string& key : sorted_keys(*object)) {
    if (key != "symbol" && key != "module" && key != "level") {
      return std::unexpected(std::string("unknown key in a 'protect' entry: '") + key + "'");
    }
  }
  return entry;
}

[[nodiscard]] auto parse_policy(const llvm::json::Value& root)
    -> std::expected<Policy, std::string> {
  const llvm::json::Object* object = root.getAsObject();
  if (object == nullptr) {
    return std::unexpected(std::string("the policy file must contain a JSON object"));
  }
  for (const std::string& key : sorted_keys(*object)) {
    if (key != "protect" && key != "deny" && key != "seed") {
      return std::unexpected(std::string("unknown key in the policy file: '") + key +
                             "' (expected 'protect', 'deny' or 'seed')");
    }
  }

  Policy policy;
  if (const llvm::json::Array* protect = object->getArray("protect"); protect != nullptr) {
    for (const llvm::json::Value& item : *protect) {
      auto entry = parse_protect_entry(item);
      if (!entry) {
        return std::unexpected(entry.error());
      }
      policy.protect.emplace_back(std::move(*entry));
    }
  } else if (object->get("protect") != nullptr) {
    return std::unexpected(std::string("'protect' must be an array"));
  }

  if (const llvm::json::Array* deny = object->getArray("deny"); deny != nullptr) {
    for (const llvm::json::Value& item : *deny) {
      const auto pattern = item.getAsString();
      if (!pattern.has_value()) {
        return std::unexpected(std::string("a 'deny' entry must be a string"));
      }
      policy.deny.emplace_back(*pattern);
    }
  } else if (object->get("deny") != nullptr) {
    return std::unexpected(std::string("'deny' must be an array"));
  }

  if (const auto seed = object->getString("seed"); seed.has_value()) {
    auto parsed = parse_seed(*seed);
    if (!parsed) {
      return std::unexpected(parsed.error());
    }
    if (seed_is_truncated(*seed)) {
      report_truncated_seed("the policy's seed", *seed);
    }
    policy.seed = *parsed;
  } else if (object->get("seed") != nullptr) {
    return std::unexpected(std::string("'seed' must be a hexadecimal string"));
  }
  return policy;
}

/// The directory the per-module coverage goes to.
///
/// `CS2_PROTECT_COVERAGE` names it outright (the escape hatch for a CI
/// build); otherwise it lives under the temp directory, keyed by the policy
/// path, so that a policy file in the source tree stays clean.
/// tools/protect/check_policy.py computes the same path.
[[nodiscard]] auto default_coverage_dir(std::string_view policy_path) -> std::filesystem::path {
  if (const char* custom = std::getenv("CS2_PROTECT_COVERAGE"); custom != nullptr) {
    const std::string_view directory = trim(custom);
    if (!directory.empty()) {
      return {std::string(directory)};
    }
  }
  std::filesystem::path root = ".";
  if (const char* temp = std::getenv("TEMP"); temp != nullptr && *temp != '\0') {
    root = temp;
  } else if (const char* temp = std::getenv("TMP"); temp != nullptr && *temp != '\0') {
    root = temp;
  }
  llvm::SHA256 hasher;
  hasher.update(policy_path);
  const std::string digest = llvm::toHex(hasher.final());
  return root / "cs2-protect-coverage" / digest.substr(0, 16);
}

}  // namespace

[[nodiscard]] auto to_string(ProtectionLevel level) noexcept -> const char* {
  switch (level) {
    case ProtectionLevel::virtualized:
      return "virtualized";
    case ProtectionLevel::call:
    default:
      return "call";
  }
}

[[nodiscard]] auto to_string(SelectionSource source) noexcept -> const char* {
  switch (source) {
    case SelectionSource::annotation:
      return "annotation";
    case SelectionSource::policy:
    default:
      return "policy";
  }
}

auto parse_policy_text(std::string_view text) -> std::expected<Policy, ConfigError> {
  if (text.starts_with(k_utf8_bom)) {
    text.remove_prefix(k_utf8_bom.size());
  }

  // The depth is checked first: the SDK's parser recurses, and a file that
  // nests deeper than the limit would kill the compiler before the parser
  // could report anything.
  auto scan = scan_policy(text);
  if (!scan) {
    return std::unexpected(ConfigError{scan.error()});
  }
  if (scan->depth > MAX_POLICY_DEPTH) {
    return std::unexpected(ConfigError{std::string("the policy file is nested too deeply (") +
                                       std::to_string(scan->depth) + " levels, the limit is " +
                                       std::to_string(MAX_POLICY_DEPTH) + ")"});
  }

  auto parsed = llvm::json::parse(text);
  if (!parsed) {
    return std::unexpected(ConfigError{llvm::toString(parsed.takeError())});
  }
  auto policy = parse_policy(*parsed);
  if (!policy) {
    return std::unexpected(ConfigError{policy.error()});
  }
  return *policy;
}

auto load_config() -> std::expected<Config, ConfigError> {
  Config config;

  if (const char* verbose = std::getenv("CS2_PROTECT_VERBOSE"); verbose != nullptr) {
    config.verbose = trim(verbose) == "1";
  }

  if (const char* seed = std::getenv("CS2_PROTECT_SEED"); seed != nullptr && *seed != '\0') {
    const std::string_view value = trim(seed);
    auto parsed = parse_seed(value);
    if (!parsed) {
      return std::unexpected(ConfigError{"CS2_PROTECT_SEED: " + parsed.error()});
    }
    if (seed_is_truncated(value)) {
      report_truncated_seed("CS2_PROTECT_SEED", value);
    }
    config.seed = *parsed;
  }

  const char* policy_path = std::getenv("CS2_PROTECT_CONFIG");
  if (policy_path == nullptr || *policy_path == '\0') {
    return config;
  }
  const std::string policy_file = std::string(trim(policy_path));
  if (policy_file.empty()) {
    return config;
  }

  const std::ifstream file(policy_file, std::ios::binary);
  if (!file) {
    return std::unexpected(ConfigError{
        std::string("CS2_PROTECT_CONFIG: cannot open the policy file '") + policy_file + "'"});
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  const std::string text = buffer.str();

  llvm::SHA256 hasher;
  hasher.update(text);
  config.policy_digest = llvm::toHex(hasher.final());
  config.policy_path = policy_file;
  config.coverage_dir = default_coverage_dir(policy_file);

  auto policy = parse_policy_text(text);
  if (!policy) {
    return std::unexpected(
        ConfigError{"CS2_PROTECT_CONFIG: " + policy_file + ": " + policy.error().message});
  }
  config.policy = std::move(*policy);
  return config;
}

}  // namespace cs2::protect
