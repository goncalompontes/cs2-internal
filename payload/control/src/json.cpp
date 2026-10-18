#include "cs2/control/json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace cs2::control {
namespace {

// Append one code point as UTF-8.
void append_utf8(std::string& out, std::uint32_t code_point) noexcept {
  if (code_point <= 0x7f) {
    out.push_back(static_cast<char>(code_point));
  } else if (code_point <= 0x7ff) {
    out.push_back(static_cast<char>(0xc0 | (code_point >> 6)));
    out.push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
  } else if (code_point <= 0xffff) {
    out.push_back(static_cast<char>(0xe0 | (code_point >> 12)));
    out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
  } else {
    out.push_back(static_cast<char>(0xf0 | (code_point >> 18)));
    out.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
  }
}

// Recursive-descent parser over one JSON value. `depth` bounds nesting so
// hostile input cannot blow the stack.
class Parser {
 public:
  explicit Parser(std::string_view text) noexcept : text_(text) {}

  auto parse_document() -> std::optional<Json> {
    skip_whitespace();
    std::optional<Json> value = parse_value();
    if (!value.has_value()) {
      return std::nullopt;
    }
    skip_whitespace();
    if (pos_ != text_.size()) {
      return std::nullopt;  // trailing garbage
    }
    return value;
  }

 private:
  static constexpr int k_max_depth = 64;

  auto at_end() const noexcept -> bool {
    return pos_ >= text_.size();
  }
  auto peek() const noexcept -> char {
    return at_end() ? '\0' : text_[pos_];
  }

  void skip_whitespace() noexcept {
    while (!at_end()) {
      const char c = text_[pos_];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        ++pos_;
      } else {
        break;
      }
    }
  }

  auto consume(std::string_view word) noexcept -> bool {
    if (text_.compare(pos_, word.size(), word) == 0) {
      pos_ += word.size();
      return true;
    }
    return false;
  }

  auto parse_value() -> std::optional<Json> {
    if (++depth_ > k_max_depth) {
      return std::nullopt;
    }
    skip_whitespace();
    std::optional<Json> result;
    switch (peek()) {
      case '{':
        result = parse_object();
        break;
      case '[':
        result = parse_array();
        break;
      case '"':
        result = parse_string_value();
        break;
      case 't':
        if (consume("true")) result = Json::boolean(true);
        break;
      case 'f':
        if (consume("false")) result = Json::boolean(false);
        break;
      case 'n':
        if (consume("null")) result = Json();
        break;
      default:
        result = parse_number();
        break;
    }
    --depth_;
    return result;
  }

  auto parse_object() -> std::optional<Json> {
    ++pos_;  // '{'
    Json object = Json::object();
    skip_whitespace();
    if (peek() == '}') {
      ++pos_;
      return object;
    }
    for (;;) {
      skip_whitespace();
      if (peek() != '"') {
        return std::nullopt;
      }
      std::optional<std::string> key = parse_string();
      if (!key.has_value()) {
        return std::nullopt;
      }
      skip_whitespace();
      if (peek() != ':') {
        return std::nullopt;
      }
      ++pos_;
      std::optional<Json> value = parse_value();
      if (!value.has_value()) {
        return std::nullopt;
      }
      object.set(std::move(*key), std::move(*value));
      skip_whitespace();
      const char c = peek();
      if (c == ',') {
        ++pos_;
        continue;
      }
      if (c == '}') {
        ++pos_;
        return object;
      }
      return std::nullopt;
    }
  }

  auto parse_array() -> std::optional<Json> {
    ++pos_;  // '['
    Json array = Json::array();
    skip_whitespace();
    if (peek() == ']') {
      ++pos_;
      return array;
    }
    for (;;) {
      std::optional<Json> value = parse_value();
      if (!value.has_value()) {
        return std::nullopt;
      }
      array.push_back(std::move(*value));
      skip_whitespace();
      const char c = peek();
      if (c == ',') {
        ++pos_;
        continue;
      }
      if (c == ']') {
        ++pos_;
        return array;
      }
      return std::nullopt;
    }
  }

  auto parse_string_value() -> std::optional<Json> {
    std::optional<std::string> value = parse_string();
    if (!value.has_value()) {
      return std::nullopt;
    }
    return Json::string(std::move(*value));
  }

  auto parse_string() -> std::optional<std::string> {
    ++pos_;  // opening quote
    std::string out;
    while (!at_end()) {
      const char c = text_[pos_++];
      if (c == '"') {
        return out;
      }
      if (c != '\\') {
        if (static_cast<unsigned char>(c) < 0x20) {
          return std::nullopt;  // raw control character
        }
        out.push_back(c);
        continue;
      }
      if (at_end()) {
        return std::nullopt;
      }
      const char esc = text_[pos_++];
      switch (esc) {
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
          std::optional<std::uint32_t> unit = parse_hex4();
          if (!unit.has_value()) {
            return std::nullopt;
          }
          std::uint32_t code_point = *unit;
          if (code_point >= 0xd800 && code_point <= 0xdbff) {
            // High surrogate: require a following low surrogate.
            if (text_.compare(pos_, 2, "\\u") != 0) {
              return std::nullopt;
            }
            pos_ += 2;
            std::optional<std::uint32_t> low = parse_hex4();
            if (!low.has_value() || *low < 0xdc00 || *low > 0xdfff) {
              return std::nullopt;
            }
            code_point = 0x10000 + ((code_point - 0xd800) << 10) + (*low - 0xdc00);
          } else if (code_point >= 0xdc00 && code_point <= 0xdfff) {
            return std::nullopt;  // lone low surrogate
          }
          append_utf8(out, code_point);
          break;
        }
        default:
          return std::nullopt;
      }
    }
    return std::nullopt;  // unterminated string
  }

  auto parse_hex4() -> std::optional<std::uint32_t> {
    if (text_.size() - pos_ < 4) {
      return std::nullopt;
    }
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
      const char c = text_[pos_++];
      value <<= 4;
      if (c >= '0' && c <= '9') {
        value |= static_cast<std::uint32_t>(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        value |= static_cast<std::uint32_t>(c - 'a' + 10);
      } else if (c >= 'A' && c <= 'F') {
        value |= static_cast<std::uint32_t>(c - 'A' + 10);
      } else {
        return std::nullopt;
      }
    }
    return value;
  }

  auto parse_number() -> std::optional<Json> {
    const std::size_t start = pos_;
    if (peek() == '-') {
      ++pos_;
    }
    if (at_end() || peek() < '0' || peek() > '9') {
      return std::nullopt;
    }
    if (peek() == '0') {
      ++pos_;  // a leading zero must not be followed by another digit
      if (!at_end() && peek() >= '0' && peek() <= '9') {
        return std::nullopt;
      }
    } else {
      while (!at_end() && peek() >= '0' && peek() <= '9') {
        ++pos_;
      }
    }
    if (peek() == '.') {
      ++pos_;
      if (at_end() || peek() < '0' || peek() > '9') {
        return std::nullopt;
      }
      while (!at_end() && peek() >= '0' && peek() <= '9') {
        ++pos_;
      }
    }
    if (peek() == 'e' || peek() == 'E') {
      ++pos_;
      if (peek() == '+' || peek() == '-') {
        ++pos_;
      }
      if (at_end() || peek() < '0' || peek() > '9') {
        return std::nullopt;
      }
      while (!at_end() && peek() >= '0' && peek() <= '9') {
        ++pos_;
      }
    }
    const std::string token(text_.substr(start, pos_ - start));
    return Json::number(std::strtod(token.c_str(), nullptr));
  }

  std::string_view text_;
  std::size_t pos_ = 0;
  int depth_ = 0;
};

void dump_into(const Json& value, std::string& out) {
  switch (value.type()) {
    case Json::Type::null_value:
      out += "null";
      return;
    case Json::Type::bool_value:
      out += value.as_bool() ? "true" : "false";
      return;
    case Json::Type::number: {
      const double number = value.as_number();
      if (std::isfinite(number) && std::floor(number) == number && std::abs(number) < 9.0e15) {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(number));
        out += buffer;
      } else {
        // 9 significant digits, not 17: the harness needs ~7 (float) digits, and a
        // heatmap response carries thousands of chart coordinates — `%.17g` would
        // print the float noise and roughly double the wire size for no gain.
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.9g", number);
        out += buffer;
      }
      return;
    }
    case Json::Type::string_value: {
      out.push_back('"');
      for (const char c : value.as_string()) {
        switch (c) {
          case '"':
            out += "\\\"";
            break;
          case '\\':
            out += "\\\\";
            break;
          case '\b':
            out += "\\b";
            break;
          case '\f':
            out += "\\f";
            break;
          case '\n':
            out += "\\n";
            break;
          case '\r':
            out += "\\r";
            break;
          case '\t':
            out += "\\t";
            break;
          default:
            if (static_cast<unsigned char>(c) < 0x20) {
              char buffer[8];
              std::snprintf(buffer, sizeof(buffer), "\\u%04x",
                            static_cast<unsigned>(static_cast<unsigned char>(c)));
              out += buffer;
            } else {
              out.push_back(c);
            }
            break;
        }
      }
      out.push_back('"');
      return;
    }
    case Json::Type::array: {
      out.push_back('[');
      bool first = true;
      for (const Json& item : value.items()) {
        if (!first) {
          out.push_back(',');
        }
        first = false;
        dump_into(item, out);
      }
      out.push_back(']');
      return;
    }
    case Json::Type::object: {
      out.push_back('{');
      bool first = true;
      for (const auto& [name, member] : value.members()) {
        if (!first) {
          out.push_back(',');
        }
        first = false;
        dump_into(Json::string(name), out);
        out.push_back(':');
        dump_into(member, out);
      }
      out.push_back('}');
      return;
    }
  }
}

}  // namespace

auto Json::boolean(bool value) -> Json {
  Json json;
  json.type_ = Type::bool_value;
  json.bool_ = value;
  return json;
}

auto Json::number(double value) -> Json {
  Json json;
  json.type_ = Type::number;
  json.number_ = value;
  return json;
}

auto Json::integer(std::int64_t value) -> Json {
  return number(static_cast<double>(value));
}

auto Json::string(std::string value) -> Json {
  Json json;
  json.type_ = Type::string_value;
  json.string_ = std::move(value);
  return json;
}

auto Json::array() -> Json {
  Json json;
  json.type_ = Type::array;
  return json;
}

auto Json::object() -> Json {
  Json json;
  json.type_ = Type::object;
  return json;
}

auto Json::as_bool(bool fallback) const noexcept -> bool {
  return type_ == Type::bool_value ? bool_ : fallback;
}

auto Json::as_number(double fallback) const noexcept -> double {
  return type_ == Type::number ? number_ : fallback;
}

auto Json::as_int(std::int64_t fallback) const noexcept -> std::int64_t {
  if (type_ != Type::number) {
    return fallback;
  }
  return static_cast<std::int64_t>(number_);
}

auto Json::as_string(std::string_view fallback) const noexcept -> std::string_view {
  return type_ == Type::string_value ? std::string_view(string_) : fallback;
}

auto Json::find(std::string_view key) const noexcept -> const Json* {
  if (type_ != Type::object) {
    return nullptr;
  }
  for (const auto& [name, value] : object_) {
    if (name == key) {
      return &value;
    }
  }
  return nullptr;
}

void Json::set(std::string key, Json value) {
  if (type_ != Type::object) {
    return;
  }
  for (auto& [name, existing] : object_) {
    if (name == key) {
      existing = std::move(value);
      return;
    }
  }
  object_.emplace_back(std::move(key), std::move(value));
}

void Json::push_back(Json value) {
  if (type_ == Type::array) {
    array_.push_back(std::move(value));
  }
}

auto Json::dump() const -> std::string {
  std::string out;
  dump_into(*this, out);
  return out;
}

auto Json::parse(std::string_view text) -> std::optional<Json> {
  Parser parser(text);
  return parser.parse_document();
}

}  // namespace cs2::control
