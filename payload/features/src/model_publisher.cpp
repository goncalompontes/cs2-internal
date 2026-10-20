#include "cs2/features/model_publisher.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <type_traits>
#include <variant>

#include "cs2/features/settings.hpp"
#include "cs2/overlay/wire.hpp"
#include "cs2/diag/watchdog.hpp"
#include "cs2/game/log.hpp"
#include "cs2/render/draw_list.hpp"
#include "cs2/tls/bootstrap.hpp"

namespace cs2::features {
namespace {

namespace wire = ::cs2::overlay::wire;

using wire::DrawKind;

void copy_string(char* out, std::size_t size, const std::string& value) noexcept {
  if (size == 0) {
    return;
  }
  const std::size_t count = (std::min)(value.size(), size - 1);
  std::memcpy(out, value.data(), count);
  out[count] = '\0';
}

void store_color(wire::Draw& out, const render::Color& color) noexcept {
  out.r = color.r;
  out.g = color.g;
  out.b = color.b;
  out.a = color.a;
}

}  // namespace

ModelPublisher::~ModelPublisher() { stop(); }

#if defined(_WIN32)

unsigned long __stdcall ModelPublisher::thread_entry(void* self) noexcept {
  // This thread has no TLS block for the mapped image until we install one.
  cs2::tls::ensure_thread_tls();
  static_cast<ModelPublisher*>(self)->run();
  return 0;
}

void ModelPublisher::run() noexcept {
  const bool no_state = std::getenv("CS2_PAYLOAD_PUB_NOSTATE") != nullptr;
  const bool no_write = std::getenv("CS2_PAYLOAD_PUB_NOWRITE") != nullptr;
  unsigned interval_ms = 16;
  if (const char* interval = std::getenv("CS2_PAYLOAD_PUB_INTERVAL_MS")) {
    interval_ms = static_cast<unsigned>(std::atoi(interval));
    if (interval_ms == 0) {
      interval_ms = 16;
    }
  }
  while (running_.load(std::memory_order_relaxed)) {
    // The publish loop is the payload's frame source now that the Present hook
    // is gone; feed the freeze watchdog so it does not suspend all threads.
    cs2::diag::heartbeat();
    if (no_write) {  // isolation: thread runs, heartbeats, but never publishes
      Sleep(interval_ms);
      continue;
    }
    auto* shared = static_cast<wire::Shared*>(shared_);
    float width = 0.0F;
    float height = 0.0F;
    if (shared != nullptr) {
      width = static_cast<float>(shared->screen_width.load(std::memory_order_relaxed));
      height = static_cast<float>(shared->screen_height.load(std::memory_order_relaxed));
    }
    // Isolation switch: skip game-state reads (and the periodic log) and publish
    // a static model — separates shm publishing from the state reader.
    if (no_state) {
      // Stack object (not a function-local static): a static here would use the
      // CRT's thread-safe-init TLS, which is unreliable in a mapped image.
      const render::OverlayModel empty;
      publish(empty, width, height);
    } else {
      apply_settings();
      const render::OverlayModel& model = overlay_->update(width, height);
      publish(model, width, height);
    }
    Sleep(interval_ms);
  }
}

void ModelPublisher::publish(const render::OverlayModel& source, float /*width*/,
                             float /*height*/) noexcept {
  auto* shared = static_cast<wire::Shared*>(shared_);
  if (shared == nullptr || shared->magic != wire::k_magic) {
    return;
  }

  const uint32_t sequence = shared->sequence.load(std::memory_order_relaxed);
  shared->sequence.store(sequence + 1, std::memory_order_relaxed);  // odd: writing

  wire::Model& out = shared->model;

  copy_string(out.title, sizeof(out.title), source.hud.title);
  out.in_game = source.in_game ? 1 : 0;
  const std::size_t section_count =
      (std::min)(source.hud.sections.size(), static_cast<std::size_t>(wire::k_max_sections));
  out.section_count = static_cast<uint32_t>(section_count);
  for (std::size_t i = 0; i < section_count; ++i) {
    const render::HudSection& section = source.hud.sections[i];
    wire::Section& target = out.sections[i];
    copy_string(target.title, sizeof(target.title), section.title);
    const std::size_t line_count =
        (std::min)(section.lines.size(), static_cast<std::size_t>(wire::k_max_lines));
    target.line_count = static_cast<uint32_t>(line_count);
    for (std::size_t j = 0; j < line_count; ++j) {
      copy_string(target.lines[j].label, sizeof(target.lines[j].label), section.lines[j].label);
      copy_string(target.lines[j].value, sizeof(target.lines[j].value), section.lines[j].value);
    }
  }

  const std::size_t draw_count =
      (std::min)(source.draw.commands.size(), static_cast<std::size_t>(wire::k_max_draws));
  out.draw_count = static_cast<uint32_t>(draw_count);
  uint32_t vertex_cursor = 0;
  for (std::size_t i = 0; i < draw_count; ++i) {
    wire::Draw& target = out.draws[i];
    target = wire::Draw{};
    std::visit(
        [&](const auto& command) -> auto {
          using T = std::decay_t<decltype(command)>;
          if constexpr (std::is_same_v<T, render::Rect>) {
            target.kind = DrawKind::rect;
            target.x0 = command.x0;
            target.y0 = command.y0;
            target.x1 = command.x1;
            target.y1 = command.y1;
            target.thickness = command.thickness;
            store_color(target, command.color);
          } else if constexpr (std::is_same_v<T, render::FilledRect>) {
            target.kind = DrawKind::filled_rect;
            target.x0 = command.x0;
            target.y0 = command.y0;
            target.x1 = command.x1;
            target.y1 = command.y1;
            store_color(target, command.color);
          } else if constexpr (std::is_same_v<T, render::Line>) {
            target.kind = DrawKind::line;
            target.x0 = command.x0;
            target.y0 = command.y0;
            target.x1 = command.x1;
            target.y1 = command.y1;
            target.thickness = command.thickness;
            store_color(target, command.color);
          } else if constexpr (std::is_same_v<T, render::Text>) {
            target.kind = DrawKind::text;
            target.x0 = command.x;
            target.y0 = command.y;
            target.centered = command.centered ? 1 : 0;
            target.shadow = command.shadow ? 1 : 0;
            target.outline = command.outline ? 1 : 0;
            store_color(target, command.color);
            copy_string(target.text, sizeof(target.text), command.value);
          } else if constexpr (std::is_same_v<T, render::FilledPoly>) {
            target.kind = DrawKind::filled_poly;
            const std::size_t room =
                static_cast<std::size_t>(wire::k_max_vertices) - vertex_cursor;
            const std::size_t count = (std::min)(command.points.size(), room);
            target.vertex_offset = vertex_cursor;
            target.vertex_count = static_cast<uint32_t>(count);
            for (std::size_t j = 0; j < count; ++j) {
              out.vertices[(vertex_cursor + j) * 2] = command.points[j].first;
              out.vertices[(vertex_cursor + j) * 2 + 1] = command.points[j].second;
            }
            vertex_cursor += static_cast<uint32_t>(count);
            store_color(target, command.color);
          }
        },
        source.draw.commands[i]);
  }
  out.vertex_count = vertex_cursor;

  shared->sequence.store(sequence + 2, std::memory_order_release);  // even: stable
}

void ModelPublisher::publish_settings() noexcept {
  auto* shared = static_cast<wire::Shared*>(shared_);
  if (shared == nullptr) {
    return;
  }
  wire::Settings defaults = to_wire_settings(overlay_->esp_config());
  // The movement features' initial state travels with the defaults; the menu then
  // owns them (see `apply_settings`).
  defaults.auto_strafe = movement_auto_strafe_ ? 1 : 0;
  defaults.bhop = movement_bhop_ ? 1 : 0;
  defaults.rage = rage_ ? 1 : 0;
  defaults.rage_key = static_cast<uint8_t>(rage_key_);
  defaults.rage_fire = rage_fire_ ? 1 : 0;
  const std::uint32_t sequence = shared->settings_sequence.load(std::memory_order_relaxed);
  shared->settings_sequence.store(sequence + 1, std::memory_order_relaxed);  // odd: writing
  std::memcpy(&shared->settings, &defaults, sizeof(defaults));
  shared->settings_sequence.store(sequence + 2, std::memory_order_release);  // even: stable
  settings_sequence_ = sequence + 2;
}

// Write a settings block into the shared segment under the seqlock the layer
// reads it with (ADR-0021): the layer's menu owns the block in the Vulkan render
// mode, so the payload has to be able to publish a block back (e.g. after a
// config load) or the layer would keep showing the pre-load state.
void ModelPublisher::publish_settings_block(const wire::Settings& settings) noexcept {
#if defined(_WIN32)
  auto* shared = static_cast<wire::Shared*>(shared_);
  if (shared == nullptr) {
    return;
  }
  const std::uint32_t sequence = shared->settings_sequence.load(std::memory_order_relaxed);
  shared->settings_sequence.store(sequence + 1, std::memory_order_relaxed);  // odd: writing
  std::memcpy(&shared->settings, &settings, sizeof(settings));
  shared->settings_sequence.store(sequence + 2, std::memory_order_release);  // even: stable
  settings_sequence_ = sequence + 2;
#endif
}

void ModelPublisher::apply_settings() noexcept {  auto* shared = static_cast<wire::Shared*>(shared_);
  if (shared == nullptr) {
    return;
  }
  // Read the block every frame (16 bytes under a seqlock) so the movement features
  // can be corrected even when nobody writes it — a flag left set out-of-band (a
  // stale control command) is reverted on the next frame and the menu stays the
  // single owner. The ESP config only needs the *change* check.
  wire::Settings copy{};
  bool fresh = false;
  for (int attempt = 0; attempt < 3 && !fresh; ++attempt) {
    const std::uint32_t sequence = shared->settings_sequence.load(std::memory_order_acquire);
    if ((sequence & 1U) != 0U) {
      continue;  // mid-write
    }
    std::memcpy(&copy, &shared->settings, sizeof(copy));
    if (shared->settings_sequence.load(std::memory_order_acquire) == sequence) {
      fresh = true;
    }
  }
  if (!fresh) {
    return;  // raced a write; retry on the next frame
  }

  const std::uint32_t sequence = shared->settings_sequence.load(std::memory_order_acquire);
  if (sequence != settings_sequence_) {
    settings_sequence_ = sequence;
    overlay_->set_esp_config(from_wire_settings(copy));
  }

  // The movement features are **menu-owned**: push the settings' state every frame
  // (two atomic stores), so an out-of-band change — a stale control command, a
  // leftover from an earlier session — is corrected on the next frame instead of
  // sticking.
  if (settings_sink_ != nullptr) {
    settings_sink_(settings_sink_context_, copy);
  }
}

auto ModelPublisher::map() noexcept -> bool {
  if (shared_ != nullptr) {
    return true;
  }
  // Isolation switch: skip the shared-memory mapping (diagnostics only).
  if (std::getenv("CS2_PAYLOAD_PUB_NOSHM") != nullptr) {
    return false;
  }
  // The segment lives in the payload's writable data dir (%LOCALAPPDATA%), not
  // `C:\` root: a Medium-integrity game process cannot create files there.
  char path_buffer[MAX_PATH];
  const char* shm_path = std::getenv("CS2_OVERLAY_SHM");
  if (shm_path == nullptr) {
    if (!cs2::game::data_file_path("cs2-overlay.shm", path_buffer, sizeof(path_buffer))) {
      return false;
    }
    shm_path = path_buffer;
  }
  HANDLE file = CreateFileA(shm_path, GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return false;
  }
  SetFilePointer(file, static_cast<LONG>(sizeof(wire::Shared)), nullptr, FILE_BEGIN);
  SetEndOfFile(file);
  HANDLE mapping = CreateFileMappingA(file, nullptr, PAGE_READWRITE, 0,
                                      static_cast<DWORD>(sizeof(wire::Shared)), nullptr);
  CloseHandle(file);
  if (mapping == nullptr) {
    return false;
  }
  void* view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(wire::Shared));
  if (view == nullptr) {
    CloseHandle(mapping);
    return false;
  }
  mapping_ = mapping;
  shared_ = view;
  auto* shared = static_cast<wire::Shared*>(view);
  if (shared->magic != wire::k_magic || shared->version != wire::k_version) {
    shared->magic = wire::k_magic;
    shared->version = wire::k_version;
    shared->sequence.store(0, std::memory_order_relaxed);
    shared->settings_sequence.store(0, std::memory_order_relaxed);
    shared->screen_width.store(0, std::memory_order_relaxed);
    shared->screen_height.store(0, std::memory_order_relaxed);
  }
  // Publish the default ESP toggles so the layer's menu starts in sync with the
  // payload's configuration.
  publish_settings();
  return true;
}

void ModelPublisher::publish_now() noexcept {
  // Runs on the game's own thread (present hook); no payload thread exists.
  if (shared_ == nullptr && !map()) {
    return;
  }
  auto* shared = static_cast<wire::Shared*>(shared_);
  const auto width = static_cast<float>(shared->screen_width.load(std::memory_order_relaxed));
  const auto height = static_cast<float>(shared->screen_height.load(std::memory_order_relaxed));
  apply_settings();
  const render::OverlayModel& model = overlay_->update(width, height);
  publish(model, width, height);
}

auto ModelPublisher::start() noexcept -> bool {
  if (running_.load()) {
    return true;
  }
  map();
  running_.store(true);
  // Skip LdrpInitializeThread: a normal thread runs the game's TLS callbacks and
  // DllMain(DLL_THREAD_ATTACH), which wedges CS2/DXVK on a mode change.
  void* thread = cs2::tls::create_thread_no_attach(
      reinterpret_cast<void* (*)(void*)>(&ModelPublisher::thread_entry), this);
  if (thread == nullptr) {
    running_.store(false);
    return false;
  }
  thread_ = thread;
  return true;
}

void ModelPublisher::stop() noexcept {
  running_.store(false);
  if (thread_ != nullptr) {
    WaitForSingleObject(static_cast<HANDLE>(thread_), 2000);
    CloseHandle(static_cast<HANDLE>(thread_));
    thread_ = nullptr;
  }
  if (shared_ != nullptr) {
    UnmapViewOfFile(shared_);
    shared_ = nullptr;
  }
  if (mapping_ != nullptr) {
    CloseHandle(static_cast<HANDLE>(mapping_));
    mapping_ = nullptr;
  }
}

#else

bool ModelPublisher::map() noexcept { return false; }
void ModelPublisher::publish_now() noexcept {}
bool ModelPublisher::start() noexcept { return false; }
void ModelPublisher::stop() noexcept {}

#endif

}  // namespace cs2::features
