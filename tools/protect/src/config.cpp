#include "config.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <utility>

#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/SHA256.h"

namespace cs2::protect {

namespace {

// --- A minimal JSON reader --------------------------------------------------
// The policy file is a handful of strings. A JSON library would be more
// dependency than the tool needs, and the plugin may only use what the LLVM
// SDK ships.

class JsonValue;
class JsonMember;

class JsonValue {
 public:
  enum class Kind { null_value, boolean, number, string, array, object };

  using Array = std::vector<JsonValue>;

  JsonValue() = default;

  [[nodiscard]] static auto make_string(std::string text) -> JsonValue;
  [[nodiscard]] static auto make_array(Array items) -> JsonValue;
  [[nodiscard]] static auto make_object(std::vector<JsonMember> members) -> JsonValue;

  [[nodiscard]] auto kind() const noexcept -> Kind {
    return kind_;
  }
  [[nodiscard]] auto is_object() const noexcept -> bool {
    return kind_ == Kind::object;
  }
  [[nodiscard]] auto is_array() const noexcept -> bool {
    return kind_ == Kind::array;
  }
  [[nodiscard]] auto is_string() const noexcept -> bool {
    return kind_ == Kind::string;
  }

  [[nodiscard]] auto text() const noexcept -> std::string_view {
    return text_;
  }
  [[nodiscard]] auto items() const noexcept -> const Array& {
    return items_;
  }

  /// The member with that key, or nullptr. The object is tiny, so a linear
  /// scan is cheaper than a map.
  [[nodiscard]] auto member(std::string_view key) const noexcept -> const JsonValue*;
  [[nodiscard]] auto member_keys() const noexcept -> std::vector<std::string>;

 private:
  Kind kind_{Kind::null_value};
  std::string text_;
  Array items_;
  std::vector<JsonMember> members_;
};

class JsonMember {
 public:
  std::string key;
  JsonValue value;
};

inline auto JsonValue::make_string(std::string text) -> JsonValue {
  JsonValue value;
  value.kind_ = Kind::string;
  value.text_ = std::move(text);
  return value;
}

inline auto JsonValue::make_array(Array items) -> JsonValue {
  JsonValue value;
  value.kind_ = Kind::array;
  value.items_ = std::move(items);
  return value;
}

inline auto JsonValue::make_object(std::vector<JsonMember> members) -> JsonValue {
  JsonValue value;
  value.kind_ = Kind::object;
  value.members_ = std::move(members);
  return value;
}

inline auto JsonValue::member(std::string_view key) const noexcept -> const JsonValue* {
  for (const JsonMember& entry : members_) {
    if (entry.key == key) {
      return &entry.value;
    }
  }
  return nullptr;
}

inline auto JsonValue::member_keys() const noexcept -> std::vector<std::string> {
  std::vector<std::string> keys;
  keys.reserve(members_.size());
  for (const JsonMember& entry : members_) {
    keys.push_back(entry.key);
  }
  return keys;
}

/// A recursive-descent JSON parser. It reports the first problem it finds as
/// a message; the caller turns that into a configuration error.
class JsonParser {
 public:
  explicit JsonParser(std::string_view text) noexcept : text_(text) {}

  [[nodiscard]] auto parse() -> std::expected<JsonValue, std::string> {
    skip_space();
    auto value = parse_value();
    if (!value) {
      return value;
    }
    skip_space();
    if (pos_ != text_.size()) {
      return std::unexpected(std::string("trailing characters after the JSON value"));
    }
    return value;
  }

 private:
  [[nodiscard]] auto parse_value() -> std::expected<JsonValue, std::string> {
    if (pos_ >= text_.size()) {
      return std::unexpected(std::string("unexpected end of input"));
    }
    switch (text_[pos_]) {
      case '{':
        return parse_object();
      case '[':
        return parse_array();
      case '"': {
        auto text = parse_string();
        if (!text) {
          return std::unexpected(text.error());
        }
        return JsonValue::make_string(std::move(*text));
      }
      case 't':
        return parse_literal("true");
      case 'f':
        return parse_literal("false");
      case 'n':
        return parse_literal("null");
      default:
        return parse_number();
    }
  }

  [[nodiscard]] auto parse_object() -> std::expected<JsonValue, std::string> {
    ++pos_;  // '{'
    std::vector<JsonMember> members;
    skip_space();
    if (pos_ < text_.size() && text_[pos_] == '}') {
      ++pos_;
      return JsonValue::make_object(std::move(members));
    }
    while (true) {
      skip_space();
      auto key = parse_string();
      if (!key) {
        return std::unexpected(key.error());
      }
      skip_space();
      if (pos_ >= text_.size() || text_[pos_] != ':') {
        return std::unexpected(std::string("expected ':' after the object key"));
      }
      ++pos_;
      skip_space();
      auto value = parse_value();
      if (!value) {
        return std::unexpected(value.error());
      }
      members.push_back(JsonMember{std::move(*key), std::move(*value)});
      skip_space();
      if (pos_ >= text_.size()) {
        return std::unexpected(std::string("unterminated object"));
      }
      if (text_[pos_] == ',') {
        ++pos_;
        continue;
      }
      if (text_[pos_] == '}') {
        ++pos_;
        break;
      }
      return std::unexpected(std::string("expected ',' or '}' in the object"));
    }
    return JsonValue::make_object(std::move(members));
  }

  [[nodiscard]] auto parse_array() -> std::expected<JsonValue, std::string> {
    ++pos_;  // '['
    JsonValue::Array items;
    skip_space();
    if (pos_ < text_.size() && text_[pos_] == ']') {
      ++pos_;
      return JsonValue::make_array(std::move(items));
    }
    while (true) {
      skip_space();
      auto value = parse_value();
      if (!value) {
        return std::unexpected(value.error());
      }
      items.push_back(std::move(*value));
      skip_space();
      if (pos_ >= text_.size()) {
        return std::unexpected(std::string("unterminated array"));
      }
      if (text_[pos_] == ',') {
        ++pos_;
        continue;
      }
      if (text_[pos_] == ']') {
        ++pos_;
        break;
      }
      return std::unexpected(std::string("expected ',' or ']' in the array"));
    }
    return JsonValue::make_array(std::move(items));
  }

  [[nodiscard]] auto parse_string() -> std::expected<std::string, std::string> {
    if (pos_ >= text_.size() || text_[pos_] != '"') {
      return std::unexpected(std::string("expected a string"));
    }
    ++pos_;
    std::string out;
    while (pos_ < text_.size()) {
      const char c = text_[pos_++];
      if (c == '"') {
        return out;
      }
      if (c != '\\') {
        out.push_back(c);
        continue;
      }
      if (pos_ >= text_.size()) {
        break;
      }
      const char escape = text_[pos_++];
      switch (escape) {
        case '"':
          out.push_back('"');
          break;
        case '\\':
          out.push_back('\\');
          break;
        case '/':
          out.push_back('/');
          break;
        case 'b':
          out.push_back('\b');
          break;
        case 'f':
          out.push_back('\f');
          break;
        case 'n':
          out.push_back('\n');
          break;
        case 'r':
          out.push_back('\r');
          break;
        case 't':
          out.push_back('\t');
          break;
        case 'u': {
          if (pos_ + 4 > text_.size()) {
            return std::unexpected(std::string("truncated \\u escape"));
          }
          unsigned code = 0;
          for (int i = 0; i < 4; ++i) {
            const char digit = text_[pos_++];
            code <<= 4U;
            if (digit >= '0' && digit <= '9') {
              code |= static_cast<unsigned>(digit - '0');
            } else if (digit >= 'a' && digit <= 'f') {
              code |= static_cast<unsigned>(digit - 'a' + 10);
            } else if (digit >= 'A' && digit <= 'F') {
              code |= static_cast<unsigned>(digit - 'A' + 10);
            } else {
              return std::unexpected(std::string("bad hex digit in a \\u escape"));
            }
          }
          // The policy file is ASCII; anything else is passed through as '?'.
          out.push_back(code < 0x80U ? static_cast<char>(code) : '?');
          break;
        }
        default:
          return std::unexpected(std::string("unknown escape sequence"));
      }
    }
    return std::unexpected(std::string("unterminated string"));
  }

  [[nodiscard]] auto parse_literal(std::string_view literal)
      -> std::expected<JsonValue, std::string> {
    if (text_.compare(pos_, literal.size(), literal) != 0) {
      return std::unexpected(std::string("expected '") + std::string(literal) + "'");
    }
    pos_ += literal.size();
    return JsonValue{};
  }

  [[nodiscard]] auto parse_number() -> std::expected<JsonValue, std::string> {
    const std::size_t start = pos_;
    while (pos_ < text_.size() &&
           (std::isdigit(static_cast<unsigned char>(text_[pos_])) != 0 || text_[pos_] == '-' ||
            text_[pos_] == '+' || text_[pos_] == '.' || text_[pos_] == 'e' || text_[pos_] == 'E')) {
      ++pos_;
    }
    if (start == pos_) {
      return std::unexpected(std::string("expected a value"));
    }
    return JsonValue{};
  }

  void skip_space() noexcept {
    while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t' ||
                                   text_[pos_] == '\n' || text_[pos_] == '\r')) {
      ++pos_;
    }
  }

  std::string_view text_;
  std::size_t pos_{0};
};

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

/// Reads one `protect` entry: a bare pattern, or an object with the pattern
/// and the optional module scope and level.
[[nodiscard]] auto parse_protect_entry(const JsonValue& value)
    -> std::expected<PolicyEntry, std::string> {
  PolicyEntry entry;
  if (value.is_string()) {
    entry.pattern = std::string(value.text());
    return entry;
  }
  if (!value.is_object()) {
    return std::unexpected(std::string("a 'protect' entry must be a string or an object"));
  }
  const JsonValue* pattern = value.member("symbol");
  if (pattern == nullptr || !pattern->is_string()) {
    return std::unexpected(std::string("a 'protect' object needs a 'symbol' string"));
  }
  entry.pattern = std::string(pattern->text());
  if (const JsonValue* module = value.member("module"); module != nullptr) {
    if (!module->is_string()) {
      return std::unexpected(std::string("the 'module' of a 'protect' entry must be a string"));
    }
    entry.module = std::string(module->text());
  }
  if (const JsonValue* level = value.member("level"); level != nullptr) {
    if (!level->is_string()) {
      return std::unexpected(std::string("the 'level' of a 'protect' entry must be a string"));
    }
    auto parsed = parse_level(level->text());
    if (!parsed) {
      return std::unexpected(parsed.error());
    }
    entry.level = *parsed;
  }
  for (const std::string& key : value.member_keys()) {
    if (key != "symbol" && key != "module" && key != "level") {
      return std::unexpected(std::string("unknown key in a 'protect' entry: '") + key + "'");
    }
  }
  return entry;
}

[[nodiscard]] auto parse_policy(const JsonValue& root) -> std::expected<Policy, std::string> {
  if (!root.is_object()) {
    return std::unexpected(std::string("the policy file must contain a JSON object"));
  }
  Policy policy;
  for (const std::string& key : root.member_keys()) {
    if (key != "protect" && key != "deny" && key != "seed") {
      return std::unexpected(std::string("unknown key in the policy file: '") + key +
                             "' (expected 'protect', 'deny' or 'seed')");
    }
  }
  if (const JsonValue* protect = root.member("protect"); protect != nullptr) {
    if (!protect->is_array()) {
      return std::unexpected(std::string("'protect' must be an array"));
    }
    for (const JsonValue& item : protect->items()) {
      auto entry = parse_protect_entry(item);
      if (!entry) {
        return std::unexpected(entry.error());
      }
      policy.protect.push_back(std::move(*entry));
    }
  }
  if (const JsonValue* deny = root.member("deny"); deny != nullptr) {
    if (!deny->is_array()) {
      return std::unexpected(std::string("'deny' must be an array"));
    }
    for (const JsonValue& item : deny->items()) {
      if (!item.is_string()) {
        return std::unexpected(std::string("a 'deny' entry must be a string"));
      }
      policy.deny.push_back(std::string(item.text()));
    }
  }
  if (const JsonValue* seed = root.member("seed"); seed != nullptr) {
    if (!seed->is_string()) {
      return std::unexpected(std::string("'seed' must be a hexadecimal string"));
    }
    auto parsed = parse_seed(seed->text());
    if (!parsed) {
      return std::unexpected(parsed.error());
    }
    policy.seed = *parsed;
  }
  return policy;
}

/// The directory the per-module coverage goes to. It lives under the temp
/// directory by default so that a policy file in the source tree stays clean;
/// tools/protect/check_policy.py computes the same path.
[[nodiscard]] auto default_coverage_dir(std::string_view policy_path) -> std::filesystem::path {
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

[[nodiscard]] auto to_string(ProtectionLevel level) noexcept -> std::string_view {
  switch (level) {
    case ProtectionLevel::call:
      return "call";
    case ProtectionLevel::virtualized:
      return "virtualized";
  }
  return "call";
}

[[nodiscard]] auto to_string(SelectionSource source) noexcept -> std::string_view {
  switch (source) {
    case SelectionSource::annotation:
      return "annotation";
    case SelectionSource::policy:
      return "policy";
  }
  return "policy";
}

auto load_config() -> std::expected<Config, ConfigError> {
  Config config;

  if (const char* verbose = std::getenv("CS2_PROTECT_VERBOSE"); verbose != nullptr) {
    config.verbose = trim(verbose) == "1";
  }

  if (const char* seed = std::getenv("CS2_PROTECT_SEED"); seed != nullptr && *seed != '\0') {
    auto parsed = parse_seed(trim(seed));
    if (!parsed) {
      return std::unexpected(ConfigError{"CS2_PROTECT_SEED: " + parsed.error()});
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

  std::ifstream file(policy_file, std::ios::binary);
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

  JsonParser parser(text);
  auto root = parser.parse();
  if (!root) {
    return std::unexpected(
        ConfigError{std::string("CS2_PROTECT_CONFIG: ") + policy_file + ": " + root.error()});
  }
  auto policy = parse_policy(*root);
  if (!policy) {
    return std::unexpected(
        ConfigError{std::string("CS2_PROTECT_CONFIG: ") + policy_file + ": " + policy.error()});
  }
  config.policy = std::move(*policy);
  return config;
}

}  // namespace cs2::protect
