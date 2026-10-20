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

#include <cstdint>
#include <filesystem>
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

// The settings file's location: `%LOCALAPPDATA%\cs2-internal\cs2-config.cfg`
// (the payload's data directory, see `cs2/game/log.hpp`), overridable with the
// `CS2_PAYLOAD_CONFIG` environment variable. Empty when the path cannot be
// resolved (no writable data directory).
[[nodiscard]] auto default_config_path() -> std::filesystem::path;

// Copy the settings file's path into `config.menu.config_path`.
//
// The path is read-only in the settings block: the payload owns it and
// publishes it, so the menu can show where the file lives. Call this whenever
// the config is published. It is a no-op when the path cannot be resolved.
void set_config_path(Config& config);

// Load the settings from `path`.
//
// `std::nullopt` when the file is missing or corrupt — the caller keeps its
// defaults. Unknown keys are ignored and missing keys keep the default, so a
// file written by an older build still loads.
[[nodiscard]] auto load(const std::filesystem::path& path) -> std::optional<Config>;

// Write the settings to `path`, creating the directory when needed. The file
// starts with a header comment. False when the file cannot be written.
[[nodiscard]] auto save(const std::filesystem::path& path, const Config& config) -> bool;

// Apply the pending config op to `config`.
//
// **This is the payload's call site.** The menu sets `menu_config_op` and bumps
// `menu_config_token`; the payload calls this with the current settings block
// and the current config, and the op runs once per token:
//
//     if (features::apply_pending_op(config, settings)) {
//       // the op ran; the config may have changed
//     }
//
// The op is carried out against `default_config_path()`. A load replaces
// `config` with the file's contents (and re-points `config.menu.config_path`
// at the file that was actually read); a reset restores the defaults. Returns
// true when the op was carried out.
[[nodiscard]] auto apply_pending_op(Config& config, const cs2::overlay::wire::Settings& settings)
    -> bool;

}  // namespace cs2::features
