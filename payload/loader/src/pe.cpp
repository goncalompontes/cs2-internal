#include "cs2/loader/pe.hpp"

#include <optional>

namespace cs2::loader::pe {
namespace {

using Bytes = std::span<const std::byte>;

constexpr std::uint16_t k_dos_magic = 0x5A4D;      // "MZ"
constexpr std::uint32_t k_pe_signature = 0x00004550;  // "PE\0\0"
constexpr std::uint16_t k_opt_pe32 = 0x010B;
constexpr std::uint16_t k_opt_pe32_plus = 0x020B;

constexpr std::size_t k_e_lfanew = 0x3C;
constexpr std::size_t k_coff_size = 20;
constexpr std::size_t k_section_size = 40;
constexpr std::size_t k_import_descriptor_size = 20;

bool has(Bytes bytes, std::size_t offset, std::size_t count) {
  return offset <= bytes.size() && count <= bytes.size() - offset;
}

std::uint8_t rd8(Bytes bytes, std::size_t offset) {
  return std::to_integer<std::uint8_t>(bytes[offset]);
}

std::uint16_t rd16(Bytes bytes, std::size_t offset) {
  return static_cast<std::uint16_t>(rd8(bytes, offset) |
                                    (rd8(bytes, offset + 1) << 8));
}

std::uint32_t rd32(Bytes bytes, std::size_t offset) {
  return static_cast<std::uint32_t>(rd16(bytes, offset) |
                                    (static_cast<std::uint32_t>(rd16(bytes, offset + 2)) << 16));
}

std::uint64_t rd64(Bytes bytes, std::size_t offset) {
  return static_cast<std::uint64_t>(rd32(bytes, offset)) |
         (static_cast<std::uint64_t>(rd32(bytes, offset + 4)) << 32);
}

std::string cstring(Bytes bytes, std::size_t offset) {
  std::string out;
  while (offset < bytes.size() && bytes[offset] != std::byte{0}) {
    out.push_back(static_cast<char>(rd8(bytes, offset)));
    ++offset;
    if (out.size() > 4096) {
      break;
    }
  }
  return out;
}

std::expected<std::uint32_t, Error> rva_to_offset_inner(const ImageInfo& image, Bytes bytes,
                                                        std::uint32_t rva) {
  if (rva < image.size_of_headers) {
    return rva;
  }
  for (const auto& section : image.sections) {
    const std::uint32_t span = section.virtual_size > section.raw_size ? section.virtual_size
                                                                       : section.raw_size;
    if (rva >= section.virtual_address && rva < section.virtual_address + span) {
      const std::uint32_t delta = rva - section.virtual_address;
      if (delta >= section.raw_size) {
        break;
      }
      const std::uint32_t offset = section.raw_offset + delta;
      if (!has(bytes, offset, 1)) {
        return std::unexpected(Error::bad_directory);
      }
      return offset;
    }
  }
  return std::unexpected(Error::bad_directory);
}

std::expected<std::size_t, Error> directory_offset(const ImageInfo& image, Bytes bytes,
                                                   std::uint32_t dir_rva) {
  if (dir_rva == 0) {
    return std::unexpected(Error::bad_directory);
  }
  auto off = rva_to_offset_inner(image, bytes, dir_rva);
  if (!off) {
    return std::unexpected(off.error());
  }
  return static_cast<std::size_t>(*off);
}

void parse_sections(const ImageInfo& image, Bytes bytes, std::size_t section_table,
                    std::uint16_t count, std::vector<Section>& out) {
  for (std::uint16_t i = 0; i < count; ++i) {
    const std::size_t at = section_table + i * k_section_size;
    if (!has(bytes, at, k_section_size)) {
      return;
    }
    Section section;
    section.name = cstring(bytes, at);
    section.virtual_size = rd32(bytes, at + 8);
    section.virtual_address = rd32(bytes, at + 12);
    section.raw_size = rd32(bytes, at + 16);
    section.raw_offset = rd32(bytes, at + 20);
    section.characteristics = rd32(bytes, at + 36);
    out.push_back(std::move(section));
  }
  (void)image;
}

void parse_imports(const ImageInfo& image, Bytes bytes, std::uint32_t dir_rva,
                   std::vector<ImportModule>& out) {
  auto start = directory_offset(image, bytes, dir_rva);
  if (!start) {
    return;
  }
  std::size_t at = *start;
  for (std::size_t guard = 0; guard < 4096; ++guard) {
    if (!has(bytes, at, k_import_descriptor_size)) {
      break;
    }
    const std::uint32_t original_first_thunk = rd32(bytes, at);
    const std::uint32_t name_rva = rd32(bytes, at + 12);
    const std::uint32_t first_thunk = rd32(bytes, at + 16);
    if (original_first_thunk == 0 && name_rva == 0 && first_thunk == 0) {
      break;
    }

    ImportModule module;
    auto name_off = rva_to_offset_inner(image, bytes, name_rva);
    if (name_off) {
      module.name = cstring(bytes, *name_off);
    }

    const std::uint32_t thunk_rva = original_first_thunk != 0 ? original_first_thunk : first_thunk;
    auto thunk_off = rva_to_offset_inner(image, bytes, thunk_rva);
    if (thunk_off) {
      std::size_t thunk_at = *thunk_off;
      for (std::size_t t = 0; t < 8192; ++t) {
        if (!has(bytes, thunk_at, 8)) {
          break;
        }
        const std::uint64_t entry = rd64(bytes, thunk_at);
        if (entry == 0) {
          break;
        }
        ImportSymbol symbol;
        if ((entry & 0x8000000000000000ULL) != 0) {
          symbol.by_ordinal = true;
          symbol.ordinal = static_cast<std::uint16_t>(entry & 0xFFFF);
        } else {
          auto hint_off = rva_to_offset_inner(image, bytes, static_cast<std::uint32_t>(entry));
          if (hint_off) {
            symbol.name = cstring(bytes, *hint_off + 2);  // skip the 2-byte hint
          }
        }
        module.symbols.push_back(std::move(symbol));
        thunk_at += 8;
      }
    }
    out.push_back(std::move(module));
    at += k_import_descriptor_size;
  }
}

void parse_relocations(const ImageInfo& image, Bytes bytes, std::uint32_t dir_rva,
                       std::uint32_t dir_size, std::vector<RelocationBlock>& out) {
  auto start = directory_offset(image, bytes, dir_rva);
  if (!start) {
    return;
  }
  const std::size_t end = *start + dir_size;
  std::size_t at = *start;
  while (at + 8 <= end && has(bytes, at, 8)) {
    RelocationBlock block;
    block.page_rva = rd32(bytes, at);
    const std::uint32_t block_size = rd32(bytes, at + 4);
    if (block_size < 8) {
      break;
    }
    const std::size_t count = (block_size - 8) / 2;
    for (std::size_t i = 0; i < count; ++i) {
      const std::size_t entry_at = at + 8 + i * 2;
      if (!has(bytes, entry_at, 2)) {
        break;
      }
      block.entries.push_back(rd16(bytes, entry_at));
    }
    out.push_back(std::move(block));
    at += block_size;
  }
}

void parse_exports(const ImageInfo& image, Bytes bytes, std::uint32_t dir_rva,
                   std::vector<ExportSymbol>& out) {
  auto start = directory_offset(image, bytes, dir_rva);
  if (!start) {
    return;
  }
  const std::size_t at = *start;
  if (!has(bytes, at, 40)) {
    return;
  }
  const std::uint32_t ordinal_base = rd32(bytes, at + 16);
  const std::uint32_t number_of_names = rd32(bytes, at + 24);
  const std::uint32_t address_of_functions = rd32(bytes, at + 28);
  const std::uint32_t address_of_names = rd32(bytes, at + 32);
  const std::uint32_t address_of_name_ordinals = rd32(bytes, at + 36);

  for (std::uint32_t i = 0; i < number_of_names && i < 65536; ++i) {
    auto name_entry = rva_to_offset_inner(image, bytes, address_of_names + i * 4);
    if (!name_entry || !has(bytes, *name_entry, 4)) {
      continue;
    }
    auto name_str = rva_to_offset_inner(image, bytes, rd32(bytes, *name_entry));
    auto ordinal_ptr = rva_to_offset_inner(image, bytes, address_of_name_ordinals + i * 2);
    if (!name_str || !ordinal_ptr || !has(bytes, *ordinal_ptr, 2)) {
      continue;
    }
    const std::uint16_t ordinal_index = rd16(bytes, *ordinal_ptr);
    auto func_ptr = rva_to_offset_inner(image, bytes, address_of_functions + ordinal_index * 4);
    if (!func_ptr || !has(bytes, *func_ptr, 4)) {
      continue;
    }
    ExportSymbol symbol;
    symbol.name = cstring(bytes, *name_str);
    symbol.rva = rd32(bytes, *func_ptr);
    symbol.ordinal = ordinal_base + ordinal_index;
    out.push_back(std::move(symbol));
  }
}

}  // namespace

const char* to_string(Error error) noexcept {
  switch (error) {
    case Error::truncated: return "truncated";
    case Error::bad_dos_magic: return "bad_dos_magic";
    case Error::bad_pe_signature: return "bad_pe_signature";
    case Error::unsupported_optional_header: return "unsupported_optional_header";
    case Error::bad_directory: return "bad_directory";
  }
  return "unknown";
}

std::expected<ImageInfo, Error> parse_image(Bytes image) {
  if (!has(image, 0, k_e_lfanew + 4)) {
    return std::unexpected(Error::truncated);
  }
  if (rd16(image, 0) != k_dos_magic) {
    return std::unexpected(Error::bad_dos_magic);
  }
  const std::uint32_t e_lfanew = rd32(image, k_e_lfanew);
  if (!has(image, e_lfanew, 4 + k_coff_size)) {
    return std::unexpected(Error::truncated);
  }
  if (rd32(image, e_lfanew) != k_pe_signature) {
    return std::unexpected(Error::bad_pe_signature);
  }

  const std::size_t coff = e_lfanew + 4;
  ImageInfo info;
  info.machine = rd16(image, coff);
  const std::uint16_t number_of_sections = rd16(image, coff + 2);
  const std::uint16_t size_of_optional_header = rd16(image, coff + 16);

  const std::size_t opt = coff + k_coff_size;
  if (!has(image, opt, size_of_optional_header)) {
    return std::unexpected(Error::truncated);
  }
  const std::uint16_t magic = rd16(image, opt);
  std::size_t dirs_offset = 0;
  std::size_t num_dirs_offset = 0;
  if (magic == k_opt_pe32_plus) {
    info.image_base = rd64(image, opt + 24);
    info.entry_point_rva = rd32(image, opt + 16);
    info.size_of_image = rd32(image, opt + 56);
    info.size_of_headers = rd32(image, opt + 60);
    num_dirs_offset = opt + 108;
    dirs_offset = opt + 112;
  } else if (magic == k_opt_pe32) {
    info.image_base = rd32(image, opt + 28);
    info.entry_point_rva = rd32(image, opt + 16);
    info.size_of_image = rd32(image, opt + 56);
    info.size_of_headers = rd32(image, opt + 60);
    num_dirs_offset = opt + 92;
    dirs_offset = opt + 96;
  } else {
    return std::unexpected(Error::unsupported_optional_header);
  }

  const std::size_t section_table = opt + size_of_optional_header;
  parse_sections(info, image, section_table, number_of_sections, info.sections);

  const std::uint32_t number_of_rva_and_sizes =
      has(image, num_dirs_offset, 4) ? rd32(image, num_dirs_offset) : 0;
  const auto dir_rva = [&](std::uint32_t index) -> std::uint32_t {
    if (index >= number_of_rva_and_sizes) return 0;
    const std::size_t at = dirs_offset + index * 8;
    return has(image, at, 4) ? rd32(image, at) : 0;
  };
  const auto dir_size = [&](std::uint32_t index) -> std::uint32_t {
    if (index >= number_of_rva_and_sizes) return 0;
    const std::size_t at = dirs_offset + index * 8 + 4;
    return has(image, at, 4) ? rd32(image, at) : 0;
  };

  parse_exports(info, image, dir_rva(0), info.exports);
  parse_imports(info, image, dir_rva(1), info.imports);
  parse_relocations(info, image, dir_rva(5), dir_size(5), info.relocations);

  return info;
}

}  // namespace cs2::loader::pe
