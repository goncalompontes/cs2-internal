// host_probe.cpp — minimal sanity check that exercises the toolchain and the
// Rust-like C++ conventions we use across the project (std::expected,
// std::variant). Grows into real host tooling (sig scanners, SDK generators).
#include <cstdint>
#include <expected>
#include <iostream>
#include <string>
#include <variant>
#include <vector>

namespace cs2 {

enum class ParseError { Empty, BadHex, Overflow };

// "parse, don't validate": raw text -> typed value, or a tagged error.
std::expected<std::uint64_t, ParseError> parse_hex(std::string_view text) {
  if (text.empty()) return std::unexpected(ParseError::Empty);
  if (text.starts_with("0x") || text.starts_with("0X")) text.remove_prefix(2);

  std::uint64_t value = 0;
  for (const char c : text) {
    const int digit = (c >= '0' && c <= '9')   ? c - '0'
                      : (c >= 'a' && c <= 'f') ? c - 'a' + 10
                      : (c >= 'A' && c <= 'F') ? c - 'A' + 10
                                               : -1;
    if (digit < 0) return std::unexpected(ParseError::BadHex);
    if (value > (UINT64_MAX - static_cast<std::uint64_t>(digit)) / 16)
      return std::unexpected(ParseError::Overflow);
    value = value * 16 + static_cast<std::uint64_t>(digit);
  }
  return value;
}

using ModuleId = std::variant<std::monostate, std::string, std::uint32_t>;

}  // namespace cs2

int main() {
  int failures = 0;

  if (auto v = cs2::parse_hex("0x2A"); v && *v == 42) {
    std::cout << "parse_hex(0x2A) = " << *v << "\n";
  } else {
    std::cerr << "parse_hex(0x2A) failed\n";
    ++failures;
  }

  if (auto v = cs2::parse_hex(""); !v && v.error() == cs2::ParseError::Empty) {
    std::cout << "parse_hex(\"\") -> Empty (expected)\n";
  } else {
    std::cerr << "parse_hex(\"\") did not report Empty\n";
    ++failures;
  }

  const cs2::ModuleId by_name = std::string{"client.dll"};
  std::visit([](const auto& m) {
    using T = std::decay_t<decltype(m)>;
    if constexpr (std::is_same_v<T, std::string>)
      std::cout << "module: " << m << "\n";
    else if constexpr (std::is_same_v<T, std::uint32_t>)
      std::cout << "module id: " << m << "\n";
    else
      std::cout << "module: <none>\n";
  }, by_name);

  return failures == 0 ? 0 : 1;
}
