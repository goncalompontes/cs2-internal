// Tests for the manual-map core: synthetic relocation + fixture section mapping.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "cs2/loader/mapping.hpp"
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
  int failures = 0;
  const auto check = [&](bool cond, const char* what) -> void {
    if (!cond) {
      std::cerr << "FAIL: " << what << "\n";
      ++failures;
    }
  };

  // --- synthetic DIR64 relocation (deterministic) ---
  {
    cs2::loader::pe::ImageInfo image;
    image.image_base = 0x1'4000'0000ULL;
    image.size_of_image = 0x1000;

    cs2::loader::pe::RelocationBlock block;
    block.page_rva = 0;
    block.entries.push_back(static_cast<std::uint16_t>((10u << 12) | 0x100u));  // DIR64 @ +0x100
    image.relocations.push_back(block);

    cs2::loader::mapping::BufferSink sink(0x2'0000'0000ULL, 0x1000);
    const std::uint64_t original = 0x1'4000'0000ULL;
    std::memcpy(sink.view().data() + 0x100, &original, 8);

    const auto delta = static_cast<std::int64_t>(sink.base()) -
                       static_cast<std::int64_t>(image.image_base);
    auto result = cs2::loader::mapping::apply_relocations(image, sink, delta);
    check(result.has_value(), "apply_relocations succeeds");

    std::uint64_t value = 0;
    std::memcpy(&value, sink.view().data() + 0x100, 8);
    check(value == 0x2'0000'0000ULL, "DIR64 relocation applied");
  }

  // --- fixture: map_sections copies headers + sections ---
  {
    const std::string path = std::string(CS2_FIXTURE_DIR) + "/tiny.dll";
    const auto bytes = read_file(path);
    if (bytes.empty()) {
      std::cerr << "missing fixture: " << path << "\n";
      return 1;
    }
    auto parsed = cs2::loader::pe::parse_image(bytes);
    if (!parsed) {
      std::cerr << "parse failed: " << cs2::loader::pe::to_string(parsed.error()) << "\n";
      return 1;
    }
    const auto& image = *parsed;

    cs2::loader::mapping::BufferSink sink(0x1000'0000ULL, image.size_of_image);
    auto result = cs2::loader::mapping::map_image(image, bytes, sink);
    check(result.has_value(), "map_image succeeds");

    auto view = sink.view();
    check(view.size() == image.size_of_image, "view size matches size_of_image");
    check(std::to_integer<std::uint8_t>(view[0]) == 'M' &&
              std::to_integer<std::uint8_t>(view[1]) == 'Z',
          "MZ header copied");

    if (!image.sections.empty()) {
      const auto& section = image.sections.front();
      if (section.raw_size > 0 && section.raw_offset + section.raw_size <= bytes.size() &&
          section.virtual_address + section.raw_size <= view.size()) {
        const bool same = std::memcmp(view.data() + section.virtual_address,
                                      bytes.data() + section.raw_offset, section.raw_size) == 0;
        check(same, "first section copied to its virtual address");
      }
    }
  }

  if (failures == 0) {
    std::cout << "mapping tests passed\n";
  }
  return failures == 0 ? 0 : 1;
}
