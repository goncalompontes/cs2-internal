#include "cs2/game/rage_debug.hpp"
#include <imgui.h>
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "cs2/render/native_menu.hpp"
void check(bool ok, const char* why) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", why);
    std::exit(1);
  }
}
int main() {
  cs2::game::RageDebugSnapshot snapshot;
  snapshot.enabled = true;
  snapshot.auto_fire = true;
  snapshot.hook = true;
  snapshot.command = 123;
  snapshot.clip = 10;
  snapshot.snapshot_clip = 10;
  snapshot.observer_ms = GetTickCount64();
  snapshot.captured_ms = snapshot.observer_ms;
  snapshot.source.eye_call = true;
  snapshot.source.eye_distance = 1800;
  snapshot.source.returned_eye = {0, 0, 0};
  snapshot.gates[static_cast<std::size_t>(cs2::game::RageDebugStage::eye)] =
      cs2::game::RageDebugGate::fail;
  std::snprintf(snapshot.detail, sizeof(snapshot.detail),
                "shoot-eye result invalid, local delta 1800 (limit 128)");
  std::snprintf(snapshot.weapon_name, sizeof(snapshot.weapon_name), "weapon_ssg08");
  const auto text = cs2::game::format_rage_debug(snapshot);
  check(text.find("Shooting position: BLOCKED") != std::string::npos &&
            text.find("delta 1800") != std::string::npos,
        "copied report names failing gate and exact reason");
  check(text.find("snapshot_ammo=10") != std::string::npos &&
            text.find("Command serialization") != std::string::npos,
        "copied report retains weapon and downstream pipeline context");
  ImGui::CreateContext();
  auto& io = ImGui::GetIO();
  io.IniFilename=nullptr;
  io.DisplaySize = {1280, 720};
  io.DeltaTime = 1.0F / 60;
  unsigned char* pixels = nullptr;
  int width = 0, height = 0;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
  cs2::render::NativeMenu menu;
  cs2::overlay::wire::Settings settings{};
  settings.rage_debug = 1;
  menu.configure(settings, nullptr, nullptr);
  menu.configure_debug(
      [](void* p, cs2::game::RageDebugSnapshot& out) {
        out = *static_cast<cs2::game::RageDebugSnapshot*>(p);
        return true;
      },
      &snapshot);
  // First ImGui frame creates/sizes windows; the second produces visible geometry.
  for (int i = 0; i < 2; ++i) {
    ImGui::NewFrame();
    menu.frame();
    ImGui::Render();
  }
  const auto* draw = ImGui::GetDrawData();
  check(!menu.open() && draw && draw->TotalVtxCount > 0,
        "debug indicators render even with menu closed");
  for (int i = 0; i < draw->CmdListsCount; ++i)
    for (const auto& command : draw->CmdLists[i]->CmdBuffer)
      check(command.ClipRect.x >= 0 && command.ClipRect.y >= 0 && command.ClipRect.z <= 1280 &&
                command.ClipRect.w <= 720,
            "debug panel clipped inside viewport");
  menu.shutdown();
  ImGui::DestroyContext();
  std::puts(
      "PASS: exact failure report, full pipeline details, and closed-menu debug panel geometry "
      "within viewport");
}
