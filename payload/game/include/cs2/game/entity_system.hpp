#pragma once

// The target's paged entity identity table.
//
// `CGameEntitySystem` holds the entity list at `+0x10` (the address the
// `entity_list` global resolves to). It is a paged table, not a flat pointer
// array:
//
//   page   = index >> 9          // 0..63
//   slot   = index & 0x1ff       // 0..511
//   entry  = page_base + slot * 0x70   // CEntityIdentity
//
// `CEntityIdentity::m_pInstance` (+0x00) is the entity; `m_designerName`
// (+0x20) is its designer/class name. Verified live; see
// `docs/re/client-dll.md` ("Entity system and entity list").

#include <cstdint>
#include <string>

namespace cs2::game {

class EntitySystem {
 public:
  EntitySystem() = default;
  explicit EntitySystem(std::uintptr_t blocks) noexcept : blocks_(blocks) {}

  [[nodiscard]] bool valid() const noexcept { return blocks_ != 0; }

  // Base of a page's identity block, or 0 when the page is empty.
  [[nodiscard]] std::uintptr_t page(int page_index) const noexcept;

  // Address of the identity entry for `index`, or 0.
  [[nodiscard]] std::uintptr_t identity(int index) const noexcept;

  // Entity instance pointer for `index`, or 0.
  [[nodiscard]] std::uintptr_t instance(int index) const noexcept;

  // Resolve a 32-bit entity handle (`index & 0x7fff`).
  [[nodiscard]] std::uintptr_t resolve(std::uint32_t handle) const noexcept;

  // Designer name (e.g. "weapon_ak47") for `index`, or the empty string.
  [[nodiscard]] std::string designer_name(int index) const;

  // Paged layout constants.
  static constexpr int k_page_count = 64;
  static constexpr int k_slots_per_page = 512;
  static constexpr int k_page_shift = 9;
  static constexpr std::uintptr_t k_slot_mask = 0x1ff;
  static constexpr std::uintptr_t k_identity_stride = 0x70;

 private:
  std::uintptr_t blocks_ = 0;
};

}  // namespace cs2::game
