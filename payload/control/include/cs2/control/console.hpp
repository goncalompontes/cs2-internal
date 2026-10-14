#pragma once

// In-process `exec`: queues console command lines through the engine's command
// buffer ("Cbuf"). This is the `exec` primitive from `docs/re/engine2.md`:
// `FUN_1801b8790(&g_InputService, slot, line, 0)` appends the text to command
// buffer slot 5, which `CInputService` drains every frame.
//
// The two addresses (`input_service`, `cbuf_add_text`) come from the curated
// dumper signatures (ADR-0012), so they re-resolve across builds.

#include <string_view>

namespace cs2::control {

class Console {
 public:
  // The slot the engine itself uses for console/pipe input.
  static constexpr int k_slot = 5;

  // Resolve engine2.dll and the Cbuf addresses if not already resolved.
  // Idempotent; safe to call on every request.
  void refresh() noexcept;

  [[nodiscard]] bool ready() const noexcept { return add_text_ != nullptr; }

  // Queue `line` (may contain `;`-separated commands). Returns false when the
  // engine addresses are not resolved yet.
  [[nodiscard]] bool exec(std::string_view line) noexcept;

 private:
  using add_text_fn = void (*)(void* input_service, int slot, const char* text, int flags);

  add_text_fn add_text_ = nullptr;
  void* input_service_ = nullptr;
};

}  // namespace cs2::control
