#include "cs2/features/config_store.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <system_error>

#include "cs2/game/log.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace cs2::features {
namespace {

// The file's header: a comment, so anyone opening the file knows what it is.
constexpr std::string_view k_header =
    "# cs2-internal settings\n"
    "# One setting per line: <section>.<field> = <value>\n"
    "# Booleans are 0/1, colours are r,g,b,a, strings are quoted.\n";

// The last op token the payload acted on.
//
// The one-shot protocol is a "the token changed" signal, so the payload has to
// remember the token it last saw. The state is a file-scope atomic: it is
// constant-initialised (the payload is manually mapped, so a static with a
// dynamic initialiser would need the CRT's static-init machinery), and every
// access is atomic, so the render thread and the settings sink cannot race.
std::atomic<std::uint32_t> last_op_token{0};

// --- Small text helpers ----------------------------------------------------

auto trim(std::string_view text) -> std::string_view {
  constexpr std::string_view k_space = " \t\r\n";
  const std::size_t first = text.find_first_not_of(k_space);
  if (first == std::string_view::npos) {
    return {};
  }
  const std::size_t last = text.find_last_not_of(k_space);
  return text.substr(first, last - first + 1);
}

auto parse_u8(std::string_view value) -> std::optional<std::uint8_t> {
  unsigned int parsed = 0;
  const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
  if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || parsed > 255U) {
    return std::nullopt;
  }
  return static_cast<std::uint8_t>(parsed);
}

auto parse_u32(std::string_view value) -> std::optional<std::uint32_t> {
  std::uint32_t parsed = 0;
  const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
  if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
    return std::nullopt;
  }
  return parsed;
}

auto parse_float(std::string_view value) -> std::optional<float> {
  const std::string buffer{value};
  char* end = nullptr;
  const float parsed = std::strtof(buffer.c_str(), &end);
  // A non-finite value is corrupt, not a setting: it would poison a slider.
  if (end == buffer.c_str() || *end != '\0' || !std::isfinite(parsed)) {
    return std::nullopt;
  }
  return parsed;
}

auto parse_bool(std::string_view value) -> std::optional<bool> {
  if (value == "0" || value == "false") {
    return false;
  }
  if (value == "1" || value == "true") {
    return true;
  }
  return std::nullopt;
}

auto parse_color(std::string_view value) -> std::optional<Color> {
  std::uint8_t channels[4] = {};
  std::size_t start = 0;
  for (std::size_t index = 0; index < 4; ++index) {
    const std::size_t comma = value.find(',', start);
    const std::string_view part =
        comma == std::string_view::npos ? value.substr(start) : value.substr(start, comma - start);
    const auto parsed = parse_u8(trim(part));
    if (!parsed.has_value()) {
      return std::nullopt;
    }
    channels[index] = *parsed;
    if (index < 3) {
      if (comma == std::string_view::npos) {
        return std::nullopt;
      }
      start = comma + 1;
    }
  }
  return Color::from_wire(channels);
}

// A quoted string with backslash escapes, so a path with spaces survives.
auto parse_string(std::string_view value, std::string& out) -> bool {
  if (value.size() < 2 || value.front() != '"' || value.back() != '"') {
    return false;
  }
  const std::string_view body = value.substr(1, value.size() - 2);
  out.clear();
  for (std::size_t index = 0; index < body.size(); ++index) {
    const char c = body[index];
    if (c == '\\' && index + 1 < body.size()) {
      out += body[++index];
      continue;
    }
    if (c == '"') {
      return false;  // an unescaped quote ends the string early
    }
    out += c;
  }
  return true;
}

// --- Writing ----------------------------------------------------------------

auto append_key(std::string& out, const char* key) -> void {
  out += key;
  out += " = ";
}

auto append_bool(std::string& out, const char* key, bool value) -> void {
  append_key(out, key);
  out += value ? "1" : "0";
  out += '\n';
}

auto append_u8(std::string& out, const char* key, std::uint8_t value) -> void {
  append_key(out, key);
  out += std::to_string(static_cast<unsigned int>(value));
  out += '\n';
}

auto append_u32(std::string& out, const char* key, std::uint32_t value) -> void {
  append_key(out, key);
  out += std::to_string(value);
  out += '\n';
}

auto append_float(std::string& out, const char* key, float value) -> void {
  append_key(out, key);
  char buffer[32] = {};
  // %.9g round-trips every float exactly (a float needs 9 significant digits).
  std::snprintf(buffer, sizeof(buffer), "%.9g", static_cast<double>(value));
  out += buffer;
  out += '\n';
}

auto append_color(std::string& out, const char* key, const Color& color) -> void {
  append_key(out, key);
  out += std::to_string(static_cast<unsigned int>(color.r));
  out += ',';
  out += std::to_string(static_cast<unsigned int>(color.g));
  out += ',';
  out += std::to_string(static_cast<unsigned int>(color.b));
  out += ',';
  out += std::to_string(static_cast<unsigned int>(color.a));
  out += '\n';
}

template <std::size_t N>
auto append_string(std::string& out, const char* key, const std::array<char, N>& value) -> void {
  append_key(out, key);
  out += '"';
  for (const char c : value) {
    if (c == '\0') {
      break;
    }
    if (c == '"' || c == '\\') {
      out += '\\';
    }
    out += c;
  }
  out += '"';
  out += '\n';
}

// The config as the file's text. Every field of the settings block is
// written; the host test `features-config` proves the round trip.
auto serialize(const Config& config) -> std::string {
  std::string out{k_header};

  // --- ESP ---
  append_bool(out, "esp.box", config.esp.box);
  append_bool(out, "esp.health_bar", config.esp.health_bar);
  append_bool(out, "esp.ammo_bar", config.esp.ammo_bar);
  append_bool(out, "esp.name", config.esp.name);
  append_bool(out, "esp.weapon", config.esp.weapon);
  append_bool(out, "esp.flags", config.esp.flags);
  append_bool(out, "esp.only_enemies", config.esp.only_enemies);
  append_bool(out, "esp.show_dead", config.esp.show_dead);
  append_bool(out, "esp.hitboxes", config.esp.hitboxes);
  append_u8(out, "esp.hitbox_mode", static_cast<std::uint8_t>(config.esp.hitbox_mode));
  append_bool(out, "esp.hitbox_custom_color", config.esp.hitbox_custom_color);
  append_color(out, "esp.hitbox_color", config.esp.hitbox_color);
  append_u8(out, "esp.box_thickness", config.esp.box_thickness);
  append_u8(out, "esp.box_style", static_cast<std::uint8_t>(config.esp.box_style));
  append_bool(out, "esp.head_box", config.esp.head_box);
  append_bool(out, "esp.skeleton", config.esp.skeleton);
  append_bool(out, "esp.tracer", config.esp.tracer);
  append_u8(out, "esp.tracer_origin", static_cast<std::uint8_t>(config.esp.tracer_origin));
  append_bool(out, "esp.offscreen", config.esp.offscreen);
  append_bool(out, "esp.glow", config.esp.glow);
  append_bool(out, "esp.visible_only", config.esp.visible_only);
  append_bool(out, "esp.distance", config.esp.distance);
  append_bool(out, "esp.scale_by_distance", config.esp.scale_by_distance);
  append_u8(out, "esp.max_distance_m", config.esp.max_distance_m);
  append_bool(out, "esp.radar", config.esp.radar);
  append_bool(out, "esp.sound", config.esp.sound);
  append_bool(out, "esp.grenades", config.esp.grenades);
  append_bool(out, "esp.c4", config.esp.c4);
  append_bool(out, "esp.weapons", config.esp.weapons);
  append_bool(out, "esp.local_player", config.esp.local_player);
  append_bool(out, "esp.show_spectators", config.esp.show_spectators);
  append_bool(out, "esp.show_teammates", config.esp.show_teammates);
  append_float(out, "esp.max_box_scale", config.esp.max_box_scale);
  append_color(out, "esp.color_enemy", config.esp.color_enemy);
  append_color(out, "esp.color_team", config.esp.color_team);
  append_color(out, "esp.color_box", config.esp.color_box);
  append_color(out, "esp.color_skeleton", config.esp.color_skeleton);
  append_color(out, "esp.color_tracer", config.esp.color_tracer);
  append_color(out, "esp.color_offscreen", config.esp.color_offscreen);
  append_color(out, "esp.color_sound", config.esp.color_sound);
  append_color(out, "esp.color_grenade", config.esp.color_grenade);
  append_color(out, "esp.color_c4", config.esp.color_c4);

  // --- Movement ---
  append_bool(out, "movement.auto_strafe", config.movement.auto_strafe);
  append_bool(out, "movement.bhop", config.movement.bhop);
  append_u8(out, "movement.strafe_mode", static_cast<std::uint8_t>(config.movement.strafe_mode));
  append_bool(out, "movement.strafe_ignore_keys", config.movement.strafe_ignore_keys);
  append_float(out, "movement.strafe_wishspeed", config.movement.strafe_wishspeed);
  append_u8(out, "movement.strafe_subticks", config.movement.strafe_subticks);
  append_bool(out, "movement.thirdperson", config.movement.thirdperson);
  append_float(out, "movement.thirdperson_distance", config.movement.thirdperson_distance);
  append_float(out, "movement.thirdperson_angle", config.movement.thirdperson_angle);
  append_float(out, "movement.thirdperson_pivot_z", config.movement.thirdperson_pivot_z);
  append_float(out, "movement.thirdperson_fov", config.movement.thirdperson_fov);
  append_u8(out, "movement.thirdperson_side",
            static_cast<std::uint8_t>(config.movement.thirdperson_side));

  // --- Ragebot ---
  append_bool(out, "rage.enabled", config.rage.enabled);
  append_u8(out, "rage.key", config.rage.key);
  append_bool(out, "rage.fire", config.rage.fire);
  append_u8(out, "rage.objective", config.rage.objective);
  append_u8(out, "rage.hitbox", config.rage.hitbox);
  append_float(out, "rage.fov", config.rage.fov);
  append_float(out, "rage.min_damage", config.rage.min_damage);
  append_float(out, "rage.min_hitchance", config.rage.min_hitchance);
  append_u8(out, "rage.autostop", config.rage.autostop);
  append_u8(out, "rage.autoscope", config.rage.autoscope);
  append_u8(out, "rage.autoreload", config.rage.autoreload);
  append_u8(out, "rage.only_visible", config.rage.only_visible);
  append_u8(out, "rage.mode", config.rage.mode);
  append_u8(out, "rage.per_weapon", config.rage.per_weapon);
  append_u8(out, "rage.config_op", config.rage.config_op);
  append_u32(out, "rage.config_token", config.rage.config_token);
  append_string(out, "rage.weapon", config.rage.weapon);
  append_u8(out, "rage.recoil", config.rage.recoil);
  append_float(out, "rage.stop_speed", config.rage.stop_speed);
  append_u8(out, "rage.backtrack", config.rage.backtrack);

  // --- Visuals ---
  append_bool(out, "visuals.chams", config.visuals.chams);
  append_u8(out, "visuals.chams_mode", static_cast<std::uint8_t>(config.visuals.chams_mode));
  append_bool(out, "visuals.chams_visible", config.visuals.chams_visible);
  append_bool(out, "visuals.chams_local", config.visuals.chams_local);
  append_color(out, "visuals.chams_color", config.visuals.chams_color);

  // --- Menu / config ---
  append_bool(out, "menu.show_hud", config.menu.show_hud);
  append_bool(out, "menu.open", config.menu.open);
  append_u8(out, "menu.key", config.menu.key);
  append_color(out, "menu.accent", config.menu.accent);
  append_u8(out, "menu.transparency", config.menu.transparency);
  append_u8(out, "menu.config_op", config.menu.config_op);
  append_u32(out, "menu.config_token", config.menu.config_token);
  append_string(out, "menu.config_path", config.menu.config_path);

  return out;
}

// --- Reading ----------------------------------------------------------------

enum class ParseResult {
  ok,
  unknown_key,  // a key this build does not know: ignored
  bad_value,    // a known key with an unusable value: the file is corrupt
};

auto assign(bool& target, std::string_view value) -> ParseResult {
  const auto parsed = parse_bool(value);
  if (!parsed.has_value()) {
    return ParseResult::bad_value;
  }
  target = *parsed;
  return ParseResult::ok;
}

auto assign(std::uint8_t& target, std::string_view value) -> ParseResult {
  const auto parsed = parse_u8(value);
  if (!parsed.has_value()) {
    return ParseResult::bad_value;
  }
  target = *parsed;
  return ParseResult::ok;
}

auto assign(std::uint32_t& target, std::string_view value) -> ParseResult {
  const auto parsed = parse_u32(value);
  if (!parsed.has_value()) {
    return ParseResult::bad_value;
  }
  target = *parsed;
  return ParseResult::ok;
}

auto assign(float& target, std::string_view value) -> ParseResult {
  const auto parsed = parse_float(value);
  if (!parsed.has_value()) {
    return ParseResult::bad_value;
  }
  target = *parsed;
  return ParseResult::ok;
}

auto assign(Color& target, std::string_view value) -> ParseResult {
  const auto parsed = parse_color(value);
  if (!parsed.has_value()) {
    return ParseResult::bad_value;
  }
  target = *parsed;
  return ParseResult::ok;
}

template <std::size_t N>
auto assign(std::array<char, N>& target, std::string_view value) -> ParseResult {
  std::string text;
  if (!parse_string(value, text) || text.size() > N - 1) {
    return ParseResult::bad_value;
  }
  target.fill('\0');
  std::memcpy(target.data(), text.data(), text.size());
  return ParseResult::ok;
}

// An enumerated setting travels as its integer value; an unknown value is a
// corrupt file, not a setting.
template <typename Enum>
auto assign_enum(Enum& target, std::string_view value, std::uint8_t max) -> ParseResult {
  const auto parsed = parse_u8(value);
  if (!parsed.has_value() || *parsed > max) {
    return ParseResult::bad_value;
  }
  target = static_cast<Enum>(*parsed);
  return ParseResult::ok;
}

auto apply_esp(std::string_view field, std::string_view value, Config::Esp& esp) -> ParseResult {
  if (field == "box") {
    return assign(esp.box, value);
  }
  if (field == "health_bar") {
    return assign(esp.health_bar, value);
  }
  if (field == "ammo_bar") {
    return assign(esp.ammo_bar, value);
  }
  if (field == "name") {
    return assign(esp.name, value);
  }
  if (field == "weapon") {
    return assign(esp.weapon, value);
  }
  if (field == "flags") {
    return assign(esp.flags, value);
  }
  if (field == "only_enemies") {
    return assign(esp.only_enemies, value);
  }
  if (field == "show_dead") {
    return assign(esp.show_dead, value);
  }
  if (field == "hitboxes") {
    return assign(esp.hitboxes, value);
  }
  if (field == "hitbox_mode") {
    return assign_enum(esp.hitbox_mode, value, 2);
  }
  if (field == "hitbox_custom_color") {
    return assign(esp.hitbox_custom_color, value);
  }
  if (field == "hitbox_color") {
    return assign(esp.hitbox_color, value);
  }
  if (field == "box_thickness") {
    return assign(esp.box_thickness, value);
  }
  if (field == "box_style") {
    return assign_enum(esp.box_style, value, 1);
  }
  if (field == "head_box") {
    return assign(esp.head_box, value);
  }
  if (field == "skeleton") {
    return assign(esp.skeleton, value);
  }
  if (field == "tracer") {
    return assign(esp.tracer, value);
  }
  if (field == "tracer_origin") {
    return assign_enum(esp.tracer_origin, value, 1);
  }
  if (field == "offscreen") {
    return assign(esp.offscreen, value);
  }
  if (field == "glow") {
    return assign(esp.glow, value);
  }
  if (field == "visible_only") {
    return assign(esp.visible_only, value);
  }
  if (field == "distance") {
    return assign(esp.distance, value);
  }
  if (field == "scale_by_distance") {
    return assign(esp.scale_by_distance, value);
  }
  if (field == "max_distance_m") {
    return assign(esp.max_distance_m, value);
  }
  if (field == "radar") {
    return assign(esp.radar, value);
  }
  if (field == "sound") {
    return assign(esp.sound, value);
  }
  if (field == "grenades") {
    return assign(esp.grenades, value);
  }
  if (field == "c4") {
    return assign(esp.c4, value);
  }
  if (field == "weapons") {
    return assign(esp.weapons, value);
  }
  if (field == "local_player") {
    return assign(esp.local_player, value);
  }
  if (field == "show_spectators") {
    return assign(esp.show_spectators, value);
  }
  if (field == "show_teammates") {
    return assign(esp.show_teammates, value);
  }
  if (field == "max_box_scale") {
    return assign(esp.max_box_scale, value);
  }
  if (field == "color_enemy") {
    return assign(esp.color_enemy, value);
  }
  if (field == "color_team") {
    return assign(esp.color_team, value);
  }
  if (field == "color_box") {
    return assign(esp.color_box, value);
  }
  if (field == "color_skeleton") {
    return assign(esp.color_skeleton, value);
  }
  if (field == "color_tracer") {
    return assign(esp.color_tracer, value);
  }
  if (field == "color_offscreen") {
    return assign(esp.color_offscreen, value);
  }
  if (field == "color_sound") {
    return assign(esp.color_sound, value);
  }
  if (field == "color_grenade") {
    return assign(esp.color_grenade, value);
  }
  if (field == "color_c4") {
    return assign(esp.color_c4, value);
  }
  return ParseResult::unknown_key;
}

auto apply_movement(std::string_view field, std::string_view value, Config::Movement& movement)
    -> ParseResult {
  if (field == "auto_strafe") {
    return assign(movement.auto_strafe, value);
  }
  if (field == "bhop") {
    return assign(movement.bhop, value);
  }
  if (field == "strafe_mode") {
    return assign_enum(movement.strafe_mode, value, 2);
  }
  if (field == "strafe_ignore_keys") {
    return assign(movement.strafe_ignore_keys, value);
  }
  if (field == "strafe_wishspeed") {
    return assign(movement.strafe_wishspeed, value);
  }
  if (field == "strafe_subticks") {
    return assign(movement.strafe_subticks, value);
  }
  if (field == "thirdperson") {
    return assign(movement.thirdperson, value);
  }
  if (field == "thirdperson_distance") {
    return assign(movement.thirdperson_distance, value);
  }
  if (field == "thirdperson_angle") {
    return assign(movement.thirdperson_angle, value);
  }
  if (field == "thirdperson_pivot_z") {
    return assign(movement.thirdperson_pivot_z, value);
  }
  if (field == "thirdperson_fov") {
    return assign(movement.thirdperson_fov, value);
  }
  if (field == "thirdperson_side") {
    return assign_enum(movement.thirdperson_side, value, 1);
  }
  return ParseResult::unknown_key;
}

auto apply_rage(std::string_view field, std::string_view value, Config::Rage& rage) -> ParseResult {
  if (field == "enabled") {
    return assign(rage.enabled, value);
  }
  if (field == "key") {
    return assign(rage.key, value);
  }
  if (field == "fire") {
    return assign(rage.fire, value);
  }
  if (field == "objective") {
    return assign(rage.objective, value);
  }
  if (field == "hitbox") {
    return assign(rage.hitbox, value);
  }
  if (field == "fov") {
    return assign(rage.fov, value);
  }
  if (field == "min_damage") {
    return assign(rage.min_damage, value);
  }
  if (field == "min_hitchance") {
    return assign(rage.min_hitchance, value);
  }
  if (field == "autostop") {
    return assign(rage.autostop, value);
  }
  if (field == "autoscope") {
    return assign(rage.autoscope, value);
  }
  if (field == "autoreload") {
    return assign(rage.autoreload, value);
  }
  if (field == "only_visible") {
    return assign(rage.only_visible, value);
  }
  if (field == "mode") {
    return assign(rage.mode, value);
  }
  if (field == "per_weapon") {
    return assign(rage.per_weapon, value);
  }
  if (field == "config_op") {
    return assign(rage.config_op, value);
  }
  if (field == "config_token") {
    return assign(rage.config_token, value);
  }
  if (field == "weapon") {
    return assign(rage.weapon, value);
  }
  if (field == "recoil") {
    return assign(rage.recoil, value);
  }
  if (field == "stop_speed") {
    return assign(rage.stop_speed, value);
  }
  if (field == "backtrack") {
    return assign(rage.backtrack, value);
  }
  return ParseResult::unknown_key;
}

auto apply_visuals(std::string_view field, std::string_view value, Config::Visuals& visuals)
    -> ParseResult {
  if (field == "chams") {
    return assign(visuals.chams, value);
  }
  if (field == "chams_mode") {
    return assign_enum(visuals.chams_mode, value, 2);
  }
  if (field == "chams_visible") {
    return assign(visuals.chams_visible, value);
  }
  if (field == "chams_local") {
    return assign(visuals.chams_local, value);
  }
  if (field == "chams_color") {
    return assign(visuals.chams_color, value);
  }
  return ParseResult::unknown_key;
}

auto apply_menu(std::string_view field, std::string_view value, Config::Menu& menu) -> ParseResult {
  if (field == "show_hud") {
    return assign(menu.show_hud, value);
  }
  if (field == "open") {
    return assign(menu.open, value);
  }
  if (field == "key") {
    return assign(menu.key, value);
  }
  if (field == "accent") {
    return assign(menu.accent, value);
  }
  if (field == "transparency") {
    return assign(menu.transparency, value);
  }
  if (field == "config_op") {
    return assign(menu.config_op, value);
  }
  if (field == "config_token") {
    return assign(menu.config_token, value);
  }
  if (field == "config_path") {
    return assign(menu.config_path, value);
  }
  return ParseResult::unknown_key;
}

auto apply_field(std::string_view section, std::string_view field, std::string_view value,
                 Config& config) -> ParseResult {
  if (section == "esp") {
    return apply_esp(field, value, config.esp);
  }
  if (section == "movement") {
    return apply_movement(field, value, config.movement);
  }
  if (section == "rage") {
    return apply_rage(field, value, config.rage);
  }
  if (section == "visuals") {
    return apply_visuals(field, value, config.visuals);
  }
  if (section == "menu") {
    return apply_menu(field, value, config.menu);
  }
  return ParseResult::unknown_key;
}

// --- File I/O ---------------------------------------------------------------

#if defined(_WIN32)
auto read_file(const std::filesystem::path& path, std::string& out) -> bool {
  HANDLE file = CreateFileW(path.native().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return false;
  }
  char buffer[4096];
  DWORD read = 0;
  while (ReadFile(file, buffer, sizeof(buffer), &read, nullptr) != 0 && read > 0) {
    out.append(buffer, static_cast<std::size_t>(read));
  }
  CloseHandle(file);
  return true;
}

auto write_file(const std::filesystem::path& path, const std::string& text) -> bool {
  HANDLE file = CreateFileW(path.native().c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return false;
  }
  DWORD written = 0;
  const bool ok =
      WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) != 0;
  CloseHandle(file);
  return ok;
}
#else
auto read_file(const std::filesystem::path& path, std::string& out) -> bool {
  std::FILE* file = std::fopen(path.c_str(), "rb");
  if (file == nullptr) {
    return false;
  }
  char buffer[4096];
  std::size_t read = 0;
  while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
    out.append(buffer, read);
  }
  std::fclose(file);
  return true;
}

auto write_file(const std::filesystem::path& path, const std::string& text) -> bool {
  std::FILE* file = std::fopen(path.c_str(), "wb");
  if (file == nullptr) {
    return false;
  }
  const std::size_t written = std::fwrite(text.data(), 1, text.size(), file);
  std::fclose(file);
  return written == text.size();
}
#endif

// Create the file's directory when it is missing. `create_directories` reports
// "false" for a directory that already exists, which is not an error.
auto ensure_parent_directory(const std::filesystem::path& path) -> bool {
  const auto parent = path.parent_path();
  if (parent.empty()) {
    return true;
  }
  std::error_code error;
  std::filesystem::create_directories(parent, error);
  return !error;
}

// The settings file's location as text. The narrow form is what the payload
// publishes in `wire::Settings::menu_config_path`.
auto default_config_text() -> std::string {
  if (const char* override_path = std::getenv("CS2_PAYLOAD_CONFIG");
      override_path != nullptr && override_path[0] != '\0') {
    return std::string(override_path);
  }
  char path[260] = {};
  if (cs2::game::data_file_path("cs2-config.cfg", path, sizeof(path))) {
    return std::string(path);
  }
  return {};
}

}  // namespace

auto default_config_path() -> std::filesystem::path {
  const std::string text = default_config_text();
  if (text.empty()) {
    return {};
  }
  return std::filesystem::path(text);
}

void set_config_path(Config& config) {
  const std::string text = default_config_text();
  config.menu.config_path.fill('\0');
  if (text.empty()) {
    return;
  }
  const std::size_t count = (std::min)(text.size(), config.menu.config_path.size() - 1);
  std::memcpy(config.menu.config_path.data(), text.data(), count);
}

auto load(const std::filesystem::path& path) -> std::optional<Config> {
  std::string text;
  if (!read_file(path, text)) {
    return std::nullopt;  // missing or unreadable: the caller keeps its defaults
  }

  Config config{};  // the defaults; the file overrides what it names
  std::size_t cursor = 0;
  while (cursor < text.size()) {
    const std::size_t newline = text.find('\n', cursor);
    const std::string_view line = newline == std::string::npos
                                      ? std::string_view(text).substr(cursor)
                                      : std::string_view(text).substr(cursor, newline - cursor);
    cursor = newline == std::string::npos ? text.size() : newline + 1;

    const std::string_view trimmed = trim(line);
    if (trimmed.empty() || trimmed.front() == '#') {
      continue;  // a comment or a blank line
    }
    const std::size_t equals = trimmed.find('=');
    if (equals == std::string_view::npos) {
      return std::nullopt;  // not a `key = value` line: the file is corrupt
    }
    const std::string_view key = trim(trimmed.substr(0, equals));
    const std::string_view value = trim(trimmed.substr(equals + 1));
    const std::size_t dot = key.find('.');
    if (dot == std::string_view::npos) {
      continue;  // an unknown key: ignored
    }
    const ParseResult result = apply_field(key.substr(0, dot), key.substr(dot + 1), value, config);
    if (result == ParseResult::bad_value) {
      return std::nullopt;  // a known key with an unusable value: corrupt
    }
    // An unknown key is ignored on purpose: a file written by a newer build
    // still loads.
  }
  return config;
}

auto save(const std::filesystem::path& path, const Config& config) -> bool {
  if (path.empty()) {
    return false;
  }
  if (!ensure_parent_directory(path)) {
    return false;
  }
  return write_file(path, serialize(config));
}

auto apply_pending_op(Config& config, const cs2::overlay::wire::Settings& settings) -> bool {
  const std::uint8_t op = settings.menu_config_op;
  if (op == 0) {
    return false;
  }
  // One click = one op: the menu bumps the token, and the op runs once per
  // token (the same protocol the ragebot's per-weapon profiles use).
  const std::uint32_t token = settings.menu_config_token;
  if (last_op_token.exchange(token, std::memory_order_relaxed) == token) {
    return false;  // this op was already carried out
  }

  const std::filesystem::path path = default_config_path();
  switch (static_cast<ConfigOp>(op)) {
    case ConfigOp::save: {
      const bool ok = save(path, config);
      cs2::game::append_log(ok ? "[config] saved" : "[config] save failed");
      return ok;
    }
    case ConfigOp::load: {
      const auto loaded = load(path);
      if (!loaded.has_value()) {
        cs2::game::append_log("[config] load failed");
        return false;
      }
      config = *loaded;
      // The path in the file is informational; the live path is the one the
      // payload is actually using.
      set_config_path(config);
      cs2::game::append_log("[config] loaded");
      return true;
    }
    case ConfigOp::reset: {
      config = Config{};
      cs2::game::append_log("[config] reset");
      return true;
    }
    case ConfigOp::none:
    default:
      return false;
  }
}

}  // namespace cs2::features
