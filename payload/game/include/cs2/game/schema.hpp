#pragma once
#include <cstdint>
#include <optional>
#include <string_view>
namespace cs2::game {
// Cached live schema lookup. Returns no value when the runtime cannot verify it.
std::optional<std::uint32_t> schema_field(std::string_view type, std::string_view field);
}
