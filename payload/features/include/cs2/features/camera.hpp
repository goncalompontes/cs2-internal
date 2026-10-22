#pragma once
#include <atomic>
#include <cstdint>
#include "cs2/game/math.hpp"
#include "cs2/game/module.hpp"
namespace cs2::features {
struct CameraBindings {
  void* override_view{};
  void* render_scope{};
  void (*filter)(void*, std::uintptr_t, std::uint64_t, std::uint8_t, int){};
  bool (*trace)(void*, void*, const game::Vec3*, const game::Vec3*, const void*, void*){};
  game::Vec3* (*view_angles)(void*, int){};
  void* input{};
  std::uintptr_t manager_global{};
  std::uintptr_t entity_blocks_global{};
};
class CameraFeatures {
 public:
  ~CameraFeatures();
  bool prepare(game::ModuleInfo module) noexcept;
  bool install() noexcept;
  void uninstall() noexcept;
  void configure(bool third_person, bool remove_scope, float distance, float hull) noexcept;
  void note_local_pawn(std::uintptr_t pawn) noexcept { pawn_.store(pawn); }
  void bind(CameraBindings api) noexcept { api_ = api; }
  void apply_view(void* setup) noexcept;
  void apply_scope(void* scope) noexcept;
  void* original_view() const noexcept { return original_view_; }
  void* original_scope() const noexcept { return original_scope_; }
 private:
  CameraBindings api_{};
  std::atomic<std::uintptr_t> pawn_{0};
  std::atomic<bool> third_person_{false}, remove_scope_{false};
  std::atomic<float> distance_{85}, hull_{12};
  std::uint32_t scene_offset_{0x330}, origin_offset_{0xc8}, eye_offset_{0xf60};
  std::uint32_t health_offset_{0x34c}, life_offset_{0x354}, team_offset_{0x3e7};
  void* original_view_{}; void* original_scope_{};
  bool installed_{};
};
}
