// Fixture test for the PE parser — parses a real (tiny) DLL, no game required.

#include <cstddef>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "cs2/loader/pe.hpp"

#ifndef CS2_FIXTURE_DIR
#error "CS2_FIXTURE_DIR must be defined by the build"
#endif

namespace {

auto read_file(const std::string& path) -> std::vector<std::byte> {
  std::ifstream in(path, std::ios::binary);
  std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  std::vector<std::byte> out(raw.size());
  std::memcpy(out.data(), raw.data(), raw.size());
  return out;
}

}  // namespace

auto main() -> int {
  const std::string path = std::string(CS2_FIXTURE_DIR) + "/tiny.dll";
  const auto bytes = read_file(path);
  if (bytes.empty()) {
    std::cerr << "empty or missing fixture: " << path << "\n";
    return 1;
  }

  auto parsed = cs2::loader::pe::parse_image(bytes);
  if (!parsed) {
    std::cerr << "parse failed: " << cs2::loader::pe::to_string(parsed.error()) << "\n";
    return 1;
  }
  const auto& image = *parsed;

  int failures = 0;
  const auto check = [&](bool cond, const char* what) -> void {
    if (!cond) {
      std::cerr << "FAIL: " << what << "\n";
      ++failures;
    }
  };

  check(image.machine == 0x8664, "machine is x86-64");
  check(image.size_of_image > 0, "size_of_image > 0");
  check(!image.sections.empty(), "has sections");
  check(!image.relocations.empty(), "has relocations");

  bool has_add = false;
  bool has_ptr = false;
  for (const auto& symbol : image.exports) {
    if (symbol.name == "cs2_tiny_add") has_add = true;
    if (symbol.name == "cs2_tiny_ptr") has_ptr = true;
  }
  check(has_add, "export cs2_tiny_add");
  check(has_ptr, "export cs2_tiny_ptr");

  std::cout << "tiny.dll: machine=0x" << std::hex << image.machine << std::dec
            << " sections=" << image.sections.size()
            << " exports=" << image.exports.size()
            << " reloc_blocks=" << image.relocations.size() << "\n";

  return failures == 0 ? 0 : 1;
}
