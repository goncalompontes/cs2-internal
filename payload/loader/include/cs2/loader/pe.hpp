#pragma once

// Minimal PE image parser used by the manual-map loader.
//
// Deliberately platform-independent (no <windows.h>) so it compiles both into
// the Windows payload and into host-side unit tests.

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace cs2::loader::pe {

enum class Error {
  truncated,
  bad_dos_magic,
  bad_pe_signature,
  unsupported_optional_header,
  bad_directory,
};

[[nodiscard]] const char* to_string(Error error) noexcept;

struct Section {
  std::string name;
  std::uint32_t virtual_size{};
  std::uint32_t virtual_address{};
  std::uint32_t raw_size{};
  std::uint32_t raw_offset{};
  std::uint32_t characteristics{};
};

struct ImportSymbol {
  std::string name;
  std::uint16_t ordinal{};
  bool by_ordinal{};
};

struct ImportModule {
  std::string name;
  std::vector<ImportSymbol> symbols;
};

struct RelocationBlock {
  std::uint32_t page_rva{};
  std::vector<std::uint16_t> entries;
};

struct ExportSymbol {
  std::string name;
  std::uint32_t rva{};
  std::uint32_t ordinal{};
};

struct ImageInfo {
  std::uint16_t machine{};
  std::uint64_t image_base{};
  std::uint32_t size_of_image{};
  std::uint32_t size_of_headers{};
  std::uint32_t entry_point_rva{};
  std::vector<Section> sections;
  std::vector<ImportModule> imports;
  std::vector<RelocationBlock> relocations;
  std::vector<ExportSymbol> exports;
};

// Parse a PE image from bytes (either a file image or an already-mapped image).
[[nodiscard]] std::expected<ImageInfo, Error> parse_image(std::span<const std::byte> image);

}  // namespace cs2::loader::pe
