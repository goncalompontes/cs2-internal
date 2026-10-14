#pragma once

// A minimal JSON value for the control channel (ADR-0020).
//
// The command/response protocol is newline-delimited JSON (`docs/control-harness.md`),
// so the payload only needs objects, arrays, strings, numbers, booleans and
// null. Parsing never throws and never reads out of bounds: malformed input is
// reported by `parse` returning `std::nullopt`.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cs2::control {

class Json {
 public:
  enum class Type { kNull, kBool, kNumber, kString, kArray, kObject };

  Json() = default;

  [[nodiscard]] static Json boolean(bool value);
  [[nodiscard]] static Json number(double value);
  [[nodiscard]] static Json integer(std::int64_t value);
  [[nodiscard]] static Json string(std::string value);
  [[nodiscard]] static Json array();
  [[nodiscard]] static Json object();

  [[nodiscard]] Type type() const noexcept { return type_; }
  [[nodiscard]] bool is_null() const noexcept { return type_ == Type::kNull; }
  [[nodiscard]] bool is_bool() const noexcept { return type_ == Type::kBool; }
  [[nodiscard]] bool is_number() const noexcept { return type_ == Type::kNumber; }
  [[nodiscard]] bool is_string() const noexcept { return type_ == Type::kString; }
  [[nodiscard]] bool is_array() const noexcept { return type_ == Type::kArray; }
  [[nodiscard]] bool is_object() const noexcept { return type_ == Type::kObject; }

  [[nodiscard]] bool as_bool(bool fallback = false) const noexcept;
  [[nodiscard]] double as_number(double fallback = 0.0) const noexcept;
  [[nodiscard]] std::int64_t as_int(std::int64_t fallback = 0) const noexcept;
  [[nodiscard]] std::string_view as_string(std::string_view fallback = {}) const noexcept;

  // Object member lookup; nullptr when absent (or when this is not an object).
  [[nodiscard]] const Json* find(std::string_view key) const noexcept;
  // Object members in insertion order; empty when this is not an object.
  [[nodiscard]] const std::vector<std::pair<std::string, Json>>& members() const noexcept {
    return object_;
  }
  // Insert or replace an object member. No-op when this is not an object.
  void set(std::string key, Json value);

  // Array access; empty when this is not an array.
  [[nodiscard]] const std::vector<Json>& items() const noexcept { return array_; }
  void push_back(Json value);

  // Serialize compactly (no insignificant whitespace).
  [[nodiscard]] std::string dump() const;

  // Parse exactly one JSON value. Returns nullopt on malformed input (trailing
  // bytes other than whitespace are also rejected).
  [[nodiscard]] static std::optional<Json> parse(std::string_view text);

 private:
  Type type_ = Type::kNull;
  bool bool_ = false;
  double number_ = 0.0;
  std::string string_;
  std::vector<Json> array_;
  std::vector<std::pair<std::string, Json>> object_;
};

}  // namespace cs2::control
