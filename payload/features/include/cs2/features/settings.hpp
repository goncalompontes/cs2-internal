#pragma once

// The overlay settings block (`wire::Settings`, ADR-0021) is the canonical,
// menu-editable configuration. The render/ESP path consumes the derived
// `Config`. These helpers bridge the two so the in-process menu (workstream A)
// and the shared-memory publisher stay in sync.

#include <cstdint>

#include "cs2/features/esp.hpp"
#include "cs2/overlay/wire.hpp"

namespace cs2::features {

// `Config` -> the settings block.
//
// **Every** field of the block is written: the ESP elements, the movement
// features, the ragebot's tuning, the visuals and the menu's own state. The
// mapping is one-to-one, so the block and the config carry the same
// information.
[[nodiscard]] auto to_wire_settings(const Config& config) -> cs2::overlay::wire::Settings;

// The settings block -> `Config`.
//
// Every field of the block is read, and every numeric field is clamped into
// its legal range (the enum-like fields fall back to their documented
// default): the shared segment is written by another process and may hold a
// stale, truncated or corrupt value, and a slider must never be pushed out of
// the range it was drawn for.
[[nodiscard]] auto from_wire_settings(const cs2::overlay::wire::Settings& settings) -> Config;

// The menu's defaults: `Config`'s defaults as a settings block (all ESP
// elements on, the debug HUD on, the movement/rage features off).
[[nodiscard]] auto default_wire_settings() -> cs2::overlay::wire::Settings;

// The outcome of a settings read.
enum class SettingsStatus : std::uint8_t {
  failed,     // the block could not be read (a torn read); the caller keeps its copy
  unchanged,  // the block is the one the caller already has
  adopted,    // the block changed; the caller has to apply it
};

// The settings block's publish/adopt protocol.
//
// The block lives in a shared segment that **two** processes write — the
// layer's menu edits it, and the payload publishes a loaded configuration back
// into it — so it is read under a seqlock and verified with the block's own
// checksum (`wire.hpp`).
//
// The latch is what makes "the block changed" detectable: a reader adopts the
// block only when the sequence moved since the last adoption, so a frame that
// reads the same block does not re-apply the configuration.
class SettingsProtocol {
 public:
  // Publish `settings` into `shared` (under the seqlock, with the checksum).
  void publish(cs2::overlay::wire::Shared& shared,
               const cs2::overlay::wire::Settings& settings) noexcept;

  // Read the block out of `shared` and adopt it when it changed.
  //
  // `out` always holds the block that was read — the previous copy when the
  // read failed — so the caller can push it to the features every frame.
  [[nodiscard]] auto adopt(cs2::overlay::wire::Shared& shared,
                           cs2::overlay::wire::Settings& out) noexcept -> SettingsStatus;

 private:
  // The sequence of the last adopted block.
  std::uint32_t adopted_sequence_{0};
  // Whether a block was adopted at all (the first read always is).
  bool adopted_{false};
};

}  // namespace cs2::features
