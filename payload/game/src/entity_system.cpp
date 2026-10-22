#include "cs2/game/entity_system.hpp"

#include "cs2/game/memory.hpp"

namespace cs2::game {
namespace {

// `CEntityIdentity` layout (verified live).
constexpr std::uintptr_t k_instance_offset = 0x00;  // m_pInstance
constexpr std::uintptr_t k_designer_offset = 0x20;  // m_designerName (const char*)

}  // namespace

auto EntitySystem::page(int page_index) const noexcept -> std::uintptr_t {
  if (blocks_ == 0 || page_index < 0 || page_index >= k_page_count) {
    return 0;
  }
  std::uintptr_t page_base = 0;
  if (!read(blocks_ + static_cast<std::uintptr_t>(page_index) * 8, page_base)) {
    return 0;
  }
  return page_base;
}

auto EntitySystem::identity(int index) const noexcept -> std::uintptr_t {
  if (index < 0) {
    return 0;
  }
  const std::uintptr_t page_base = page(index >> k_page_shift);
  if (page_base == 0) {
    return 0;
  }
  return page_base + k_identity_stride * static_cast<std::uintptr_t>(index & k_slot_mask);
}

auto EntitySystem::instance(int index) const noexcept -> std::uintptr_t {
  const std::uintptr_t entry = identity(index);
  if (entry == 0) {
    return 0;
  }
  std::uintptr_t entity = 0;
  if (!read(entry + k_instance_offset, entity)) {
    return 0;
  }
  return entity;
}

auto EntitySystem::resolve(std::uint32_t handle) const noexcept -> std::uintptr_t {
  if (handle == 0 || handle == 0xffffffffU) {
    return 0;
  }
  return instance(static_cast<int>(handle & 0x7fffU));
}

auto EntitySystem::designer_name(int index) const -> std::string {
  const std::uintptr_t entry = identity(index);
  if (entry == 0) {
    return {};
  }
  std::uintptr_t name = 0;
  if (!read(entry + k_designer_offset, name) || name == 0) {
    return {};
  }
  return read_cstring(name, 64);
}

}  // namespace cs2::game
