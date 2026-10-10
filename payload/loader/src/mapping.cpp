#include "cs2/loader/mapping.hpp"

#include <algorithm>
#include <cstring>

namespace cs2::loader::mapping {

const char* to_string(Error error) noexcept {
  switch (error) {
    case Error::size_mismatch: return "size_mismatch";
    case Error::bad_section: return "bad_section";
    case Error::relocation_out_of_range: return "relocation_out_of_range";
  }
  return "unknown";
}

std::expected<void, Error> map_sections(const pe::ImageInfo& image,
                                        std::span<const std::byte> raw, ImageSink& sink) {
  auto view = sink.view();
  if (view.size() < image.size_of_image) {
    return std::unexpected(Error::size_mismatch);
  }

  const std::size_t headers = std::min<std::size_t>(image.size_of_headers, raw.size());
  std::copy_n(raw.begin(), headers, view.begin());

  for (const auto& section : image.sections) {
    if (section.raw_size == 0) {
      continue;
    }
    if (static_cast<std::size_t>(section.raw_offset) + section.raw_size > raw.size()) {
      return std::unexpected(Error::bad_section);
    }
    if (static_cast<std::size_t>(section.virtual_address) + section.raw_size > view.size()) {
      return std::unexpected(Error::bad_section);
    }
    std::copy_n(raw.begin() + section.raw_offset, section.raw_size,
                view.begin() + section.virtual_address);
  }
  return {};
}

std::expected<void, Error> apply_relocations(const pe::ImageInfo& image, ImageSink& sink,
                                             std::int64_t delta) {
  auto view = sink.view();
  for (const auto& block : image.relocations) {
    for (const std::uint16_t entry : block.entries) {
      const std::uint16_t type = entry >> 12;
      const std::uint32_t offset = entry & 0x0FFF;
      if (type == 0) {  // IMAGE_REL_BASED_ABSOLUTE
        continue;
      }
      const std::size_t at = static_cast<std::size_t>(block.page_rva) + offset;
      if (type == 3) {  // IMAGE_REL_BASED_HIGHLOW (32-bit)
        if (at + 4 > view.size()) {
          return std::unexpected(Error::relocation_out_of_range);
        }
        std::uint32_t value = 0;
        std::memcpy(&value, view.data() + at, 4);
        value = static_cast<std::uint32_t>(value + delta);
        std::memcpy(view.data() + at, &value, 4);
      } else if (type == 10) {  // IMAGE_REL_BASED_DIR64
        if (at + 8 > view.size()) {
          return std::unexpected(Error::relocation_out_of_range);
        }
        std::uint64_t value = 0;
        std::memcpy(&value, view.data() + at, 8);
        value = static_cast<std::uint64_t>(value + delta);
        std::memcpy(view.data() + at, &value, 8);
      }
    }
  }
  return {};
}

std::expected<void, Error> map_image(const pe::ImageInfo& image,
                                     std::span<const std::byte> raw, ImageSink& sink) {
  if (auto result = map_sections(image, raw, sink); !result) {
    return result;
  }
  const std::int64_t delta = static_cast<std::int64_t>(sink.base()) -
                             static_cast<std::int64_t>(image.image_base);
  return apply_relocations(image, sink, delta);
}

}  // namespace cs2::loader::mapping
