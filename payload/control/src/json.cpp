#include "cs2/control/json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

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

  [[nodiscard]] std::optional<Json> parse_document() {
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

  [[nodiscard]] bool at_end() const noexcept { return pos_ >= text_.size(); }
  [[nodiscard]] char peek() const noexcept { return at_end() ? '\0' : text_[pos_]; }

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

  [[nodiscard]] bool consume(std::string_view word) noexcept {
    if (text_.compare(pos_, word.size(), word) == 0) {
      pos_ += word.size();
      return true;
    }
    return false;
  }

  [[nodiscard]] std::optional<Json> parse_value() {
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

  [[nodiscard]] std::optional<Json> parse_object() {
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

  [[nodiscard]] std::optional<Json> parse_array() {
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

  [[nodiscard]] std::optional<Json> parse_string_value() {
    std::optional<std::string> value = parse_string();
    if (!value.has_value()) {
      return std::nullopt;
    }
    return Json::string(std::move(*value));
  }

  [[nodiscard]] std::optional<std::string> parse_string() {
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

  [[nodiscard]] std::optional<std::uint32_t> parse_hex4() {
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

  [[nodiscard]] std::optional<Json> parse_number() {
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
    case Json::Type::kNull:
      out += "null";
      return;
    case Json::Type::kBool:
      out += value.as_bool() ? "true" : "false";
      return;
    case Json::Type::kNumber: {
      const double number = value.as_number();
      if (std::isfinite(number) && std::floor(number) == number &&
          std::abs(number) < 9.0e15) {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%lld",
                      static_cast<long long>(number));
        out += buffer;
      } else {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.17g", number);
        out += buffer;
      }
      return;
    }
    case Json::Type::kString: {
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
    case Json::Type::kArray: {
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
    case Json::Type::kObject: {
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

Json Json::boolean(bool value) {
  Json json;
  json.type_ = Type::kBool;
  json.bool_ = value;
  return json;
}

Json Json::number(double value) {
  Json json;
  json.type_ = Type::kNumber;
  json.number_ = value;
  return json;
}

Json Json::integer(std::int64_t value) { return number(static_cast<double>(value)); }

Json Json::string(std::string value) {
  Json json;
  json.type_ = Type::kString;
  json.string_ = std::move(value);
  return json;
}

Json Json::array() {
  Json json;
  json.type_ = Type::kArray;
  return json;
}

Json Json::object() {
  Json json;
  json.type_ = Type::kObject;
  return json;
}

bool Json::as_bool(bool fallback) const noexcept {
  return type_ == Type::kBool ? bool_ : fallback;
}

double Json::as_number(double fallback) const noexcept {
  return type_ == Type::kNumber ? number_ : fallback;
}

std::int64_t Json::as_int(std::int64_t fallback) const noexcept {
  if (type_ != Type::kNumber) {
    return fallback;
  }
  return static_cast<std::int64_t>(number_);
}

std::string_view Json::as_string(std::string_view fallback) const noexcept {
  return type_ == Type::kString ? std::string_view(string_) : fallback;
}

const Json* Json::find(std::string_view key) const noexcept {
  if (type_ != Type::kObject) {
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
  if (type_ != Type::kObject) {
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
  if (type_ == Type::kArray) {
    array_.push_back(std::move(value));
  }
}

std::string Json::dump() const {
  std::string out;
  dump_into(*this, out);
  return out;
}

std::optional<Json> Json::parse(std::string_view text) {
  Parser parser(text);
  return parser.parse_document();
}

}  // namespace cs2::control
