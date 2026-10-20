#pragma once

// The on-disk settings file (ADR-0021).
//
// The settings block (`wire::Settings`) is the single source of truth for the
// menu's state; this module persists it so the configuration survives a
// restart. The format is deliberately boring: a plain-text `key = value` file,
// one setting per line, `#` starts a comment, so a user can read and hand-edit
// it. The keys are the config's own names (`<section>.<field>`).
//
// The payload is the only writer. The menu asks for a save / load / reset with
// the one-shot op protocol (`wire::Settings::menu_config_op` and
// `menu_config_token`, the same shape the ragebot's per-weapon profiles use)
// and the payload carries the op out when it sees the token change.
//
// **Nothing here allocates.** The payload is manually mapped and the sink that
// calls into the store is `noexcept`: a `std::bad_alloc` in the store would
// unwind into a `noexcept` frame and terminate the game. The file's text, the
// serialized text and the path therefore live in fixed buffers.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "cs2/features/esp.hpp"
#include "cs2/overlay/wire.hpp"

namespace cs2::features {

// The ops the menu can request through the one-shot protocol.
enum class ConfigOp : std::uint8_t {
  none = 0,   // no op
  save = 1,   // write the live config to the file
  load = 2,   // read the file into the live config
  reset = 3,  // restore the defaults
};

// The maximum length of the settings file's path (`MAX_PATH`).
inline constexpr std::size_t k_max_config_path = 260;

// The one-shot op protocol's bookkeeping.
//
// The protocol is a "the token changed" signal, so the payload has to
// remember the token it last acted on. The state belongs to the caller (the
// payload's `Runtime`): a file-scope static would be new global mutable state
// (ADR-0010), and the payload is manually mapped, so a static with a dynamic
// initialiser is not an option either.
struct ConfigOpState {
  // The last token the payload acted on.
  std::atomic<std::uint32_t> last_token{0};
};

// A standard library whose 32-bit atomics are not lock-free would pull the
// CRT's lock-based atomics into the manually mapped payload.
static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
              "the config op token must be lock-free: the payload has no CRT");

// The outcome of the one-shot op.
struct ConfigOpOutcome {
  // The op was carried out (the token was consumed).
  bool ran = false;
  // The configuration changed, so the live block has to be refreshed. A save
  // changes nothing; a load and a reset do.
  bool changed = false;
  // The block the menu has to adopt when `changed`: the loaded/reset
  // configuration with the live protocol state carried across.
  cs2::overlay::wire::Settings block{};
};

// The settings file's location: `%LOCALAPPDATA%\cs2-internal\cs2-config.cfg`
// (the payload's data directory, see `cs2/game/log.hpp`), overridable with
// the `CS2_PAYLOAD_CONFIG` environment variable.
//
// The path is written into `out` (always NUL-terminated) and its length is
// returned; 0 when the path cannot be resolved (no writable data directory)
// or when the path does not fit. A path that does not fit is **refused**,
// never truncated: the path the menu shows has to be the path that is used.
[[nodiscard]] auto default_config_path(char* out, std::size_t size) noexcept -> std::size_t;

// Copy the settings file's path into `config.menu.config_path`.
//
// The path is read-only in the settings block: the payload owns it and
// publishes it, so the menu can show where the file lives. It is a no-op when
// the path cannot be resolved.
void set_config_path(Config& config) noexcept;

// Copy the settings file's path into a settings block.
//
// The path is a read-only display field: the payload owns it and publishes it,
// so the menu can show where the file lives. It is only written when it
// actually changes, so publishing it never disturbs the menu's in-flight
// edits. Returns true when the block changed.
[[nodiscard]] auto publish_config_path(cs2::overlay::wire::Settings& settings) noexcept -> bool;

// Load the settings from `path`.
//
// `std::nullopt` when the file is missing or corrupt — the caller keeps its
// defaults. Unknown keys are ignored and missing keys keep the default, so a
// file written by an older build still loads.
[[nodiscard]] auto load(const char* path) -> std::optional<Config>;

// Write the settings to `path`, creating the directory when needed. The file
// starts with a header comment. False when the file cannot be written.
[[nodiscard]] auto save(const char* path, const Config& config) -> bool;

// Apply the pending config op to the payload's configuration.
//
// **This is the payload's call site.** The menu sets `menu_config_op` and
// bumps `menu_config_token`; the payload calls this with the live settings
// block, its own configuration and the op's state, and the op runs once per
// token:
//
//     const auto outcome = features::apply_pending_op(config, live, state);
//     if (outcome.changed) {
//       // the live block has to adopt `outcome.block`
//     }
//
// The live block is the source of truth: the configuration is **rebuilt from
// it first**, so a save persists the settings the user is looking at rather
// than a stale copy of the defaults.
//
// The op is carried out against `default_config_path()`. A load replaces the
// configuration with the file's contents; a reset restores the defaults; a
// save writes the file and changes nothing. The outcome says whether the op
// ran and whether the configuration changed — only a change has to be
// published back to the menu.
[[nodiscard]] auto apply_pending_op(Config& config, const cs2::overlay::wire::Settings& live,
                                    ConfigOpState& state) noexcept -> ConfigOpOutcome;

}  // namespace cs2::features
