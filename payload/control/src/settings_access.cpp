#include "cs2/control/settings_access.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

namespace cs2::control {
namespace {

namespace wire = ::cs2::overlay::wire;

using wire::SettingInfo;
using wire::SettingType;

// The phrase for an error message: what the field expects.
auto expected_phrase(SettingType type) -> std::string {
  switch (type) {
    case SettingType::flag:
      return "a flag (0/1 or true/false)";
    case SettingType::number:
      return "a number";
    case SettingType::unsigned_number:
      return "a non-negative integer";
    case SettingType::colour:
      return "a colour ([r, g, b, a])";
    case SettingType::text:
      return "a string";
    case SettingType::count:
      break;
  }
  return "a value";
}

// The bytes of one field inside the block.
auto field_bytes(const wire::Settings& block, const SettingInfo& info) -> const std::uint8_t* {
  return reinterpret_cast<const std::uint8_t*>(&block) + info.offset;
}

auto mutable_field_bytes(wire::Settings& block, const SettingInfo& info) -> std::uint8_t* {
  return reinterpret_cast<std::uint8_t*>(&block) + info.offset;
}

// The value of one field, as JSON.
auto field_value(const wire::Settings& block, const SettingInfo& info) -> Json {
  const std::uint8_t* bytes = field_bytes(block, info);
  switch (info.type) {
    case SettingType::flag:
      return Json::integer(static_cast<std::int64_t>(bytes[0]));
    case SettingType::number: {
      float value = 0.0F;
      std::memcpy(&value, bytes, sizeof(value));
      return Json::number(static_cast<double>(value));
    }
    case SettingType::unsigned_number: {
      std::uint32_t value = 0;
      std::memcpy(&value, bytes, sizeof(value));
      return Json::integer(static_cast<std::int64_t>(value));
    }
    case SettingType::colour: {
      Json channels = Json::array();
      for (std::size_t i = 0; i < info.count; ++i) {
        channels.push_back(Json::integer(static_cast<std::int64_t>(bytes[i])));
      }
      return channels;
    }
    case SettingType::text: {
      // A NUL-terminated char array: the length is the field's, not the
      // string's.
      const auto* text = reinterpret_cast<const char*>(bytes);
      std::size_t length = 0;
      while (length < info.size && text[length] != '\0') {
        ++length;
      }
      return Json::string(std::string(text, length));
    }
    case SettingType::count:
      break;
  }
  return Json();
}

// True when the field holds the same bytes in both blocks.
auto field_equal(const wire::Settings& a, const wire::Settings& b, const SettingInfo& info)
    -> bool {
  return std::memcmp(field_bytes(a, info), field_bytes(b, info), info.size) == 0;
}

// Write `value` into the field. Returns false (with `error` set) when the
// value has the wrong type; a value that is merely out of range is clamped
// (the features clamp their own ranges again when they read the block).
auto write_field(wire::Settings& block, const SettingInfo& info, const Json& value,
                 std::string& error) -> bool {
  std::uint8_t* bytes = mutable_field_bytes(block, info);
  switch (info.type) {
    case SettingType::flag: {
      std::int64_t number = 0;
      if (value.is_bool()) {
        number = value.as_bool() ? 1 : 0;
      } else if (value.is_number()) {
        number = value.as_int();
      } else {
        error =
            "settings: \"" + std::string(info.name) + "\" expects " + expected_phrase(info.type);
        return false;
      }
      // A one-byte field: the value is clamped into 0..255.
      std::int64_t bounded = number;
      if (bounded < 0) {
        bounded = 0;
      } else if (bounded > 255) {
        bounded = 255;
      }
      bytes[0] = static_cast<std::uint8_t>(bounded);
      return true;
    }
    case SettingType::number: {
      if (!value.is_number()) {
        error =
            "settings: \"" + std::string(info.name) + "\" expects " + expected_phrase(info.type);
        return false;
      }
      const float number = static_cast<float>(value.as_number());
      std::memcpy(bytes, &number, sizeof(number));
      return true;
    }
    case SettingType::unsigned_number: {
      if (!value.is_number()) {
        error =
            "settings: \"" + std::string(info.name) + "\" expects " + expected_phrase(info.type);
        return false;
      }
      const auto number = value.as_int();
      std::int64_t bounded = number;
      if (bounded < 0) {
        bounded = 0;
      } else if (bounded > 4294967295LL) {
        bounded = 4294967295LL;
      }
      const auto clamped = static_cast<std::uint32_t>(bounded);
      std::memcpy(bytes, &clamped, sizeof(clamped));
      return true;
    }
    case SettingType::colour: {
      if (!value.is_array() || value.items().size() != info.count) {
        error =
            "settings: \"" + std::string(info.name) + "\" expects " + expected_phrase(info.type);
        return false;
      }
      for (std::size_t i = 0; i < info.count; ++i) {
        const Json& channel = value.items()[i];
        if (!channel.is_number()) {
          error =
              "settings: \"" + std::string(info.name) + "\" expects " + expected_phrase(info.type);
          return false;
        }
        const auto number = channel.as_int();
        std::int64_t bounded = number;
        if (bounded < 0) {
          bounded = 0;
        } else if (bounded > 255) {
          bounded = 255;
        }
        bytes[i] = static_cast<std::uint8_t>(bounded);
      }
      return true;
    }
    case SettingType::text: {
      if (!value.is_string()) {
        error =
            "settings: \"" + std::string(info.name) + "\" expects " + expected_phrase(info.type);
        return false;
      }
      const std::string_view text = value.as_string();
      // The field is NUL-terminated: the text is truncated to fit.
      const std::size_t length = std::min(text.size(), info.size - 1);
      std::memcpy(bytes, text.data(), length);
      bytes[length] = '\0';
      return true;
    }
    case SettingType::count:
      break;
  }
  error = "settings: \"" + std::string(info.name) + "\" has an unknown kind";
  return false;
}

}  // namespace

auto run_settings(const Json& args, SettingsAccess& access)
    -> std::expected<SettingsOutcome, CommandError> {
  // The fields to read.
  std::vector<std::string> requested;
  const bool has_get = args.find("get") != nullptr;
  if (const Json* get = args.find("get"); get != nullptr) {
    if (get->is_string()) {
      requested.emplace_back(get->as_string());
    } else if (get->is_array()) {
      for (const Json& item : get->items()) {
        if (!item.is_string()) {
          return std::unexpected(CommandError{"settings: \"get\" takes field names"});
        }
        requested.emplace_back(item.as_string());
      }
    } else {
      return std::unexpected(CommandError{"settings: \"get\" takes a name or an array of names"});
    }
  }

  // The fields to write. The whole request is validated before anything is
  // written, so a typo in one key leaves the block untouched.
  const Json* set = args.find("set");
  std::vector<std::pair<const SettingInfo*, const Json*>> writes;
  if (set != nullptr) {
    if (!set->is_object()) {
      return std::unexpected(CommandError{"settings: \"set\" takes an object of name -> value"});
    }
    for (const auto& [name, value] : set->members()) {
      const SettingInfo* info = wire::find_setting(name);
      if (info == nullptr) {
        return std::unexpected(CommandError{"settings: unknown key \"" + name + "\""});
      }
      writes.emplace_back(info, &value);
    }
  }
  for (const std::string& name : requested) {
    if (wire::find_setting(name) == nullptr) {
      return std::unexpected(CommandError{"settings: unknown key \"" + name + "\""});
    }
  }

  // The live block, and the copy the writes land in.
  wire::Settings block{};
  access.read(block);
  wire::Settings updated = block;

  std::vector<std::string> changed;
  for (const auto& [info, value] : writes) {
    std::string error;
    if (!write_field(updated, *info, *value, error)) {
      return std::unexpected(CommandError{std::move(error)});
    }
    // The changed list: only the keys whose value actually changed (a set to
    // the value it already had is not a change).
    if (!field_equal(block, updated, *info)) {
      changed.emplace_back(info->name);
    }
  }
  if (!writes.empty()) {
    access.write(updated);
  }

  // The values the client asked for: the requested fields, the written
  // fields, or every field when the request named neither.
  Json values = Json::object();
  if (!has_get && set == nullptr) {
    for (const SettingInfo& info : wire::SETTING_TABLE) {
      values.set(std::string(info.name), field_value(updated, info));
    }
  } else {
    for (const std::string& name : requested) {
      const SettingInfo* info = wire::find_setting(name);
      values.set(name, field_value(updated, *info));
    }
    for (const auto& write : writes) {
      values.set(std::string(write.first->name), field_value(updated, *write.first));
    }
  }

  SettingsOutcome outcome;
  outcome.values = std::move(values);
  outcome.changed = std::move(changed);
  outcome.wrote = set != nullptr;
  return outcome;
}

auto BatchRunner::run(const Json& steps, int depth, Dispatch dispatch, void* context, Json& results)
    -> std::expected<void, CommandError> {
  if (depth > k_max_depth) {
    return std::unexpected(CommandError{"batch: nested too deep"});
  }
  if (!steps.is_array()) {
    return std::unexpected(CommandError{"batch requires a \"steps\" array"});
  }
  const std::vector<Json>& items = steps.items();
  if (items.size() > k_max_steps) {
    return std::unexpected(CommandError{"batch: too many steps (the limit is 64)"});
  }

  Json out = Json::array();
  for (const Json& step : items) {
    if (!step.is_object()) {
      return std::unexpected(CommandError{"batch: every step must be a request object"});
    }
    const Json* cmd = step.find("cmd");
    if (cmd == nullptr || !cmd->is_string()) {
      return std::unexpected(CommandError{"batch: every step needs a \"cmd\" string"});
    }
    // The recursion guard: a step may not be a batch (the depth is bounded
    // too, but a nested batch is rejected outright).
    if (cmd->as_string() == "batch") {
      return std::unexpected(CommandError{"batch: a step must not be a batch"});
    }

    // The step runs through the service's own dispatcher, so it behaves
    // exactly like a direct request.
    Json response = Json::object();
    const bool keep_going = dispatch(context, step, response);

    Json entry = Json::object();
    entry.set("cmd", Json::string(std::string(cmd->as_string())));
    for (const auto& [key, value] : response.members()) {
      if (key == "ok" || key == "result" || key == "error") {
        entry.set(key, value);
      }
    }
    out.push_back(std::move(entry));
    if (!keep_going) {
      break;  // a `quit` step closed the channel
    }
  }
  results = std::move(out);
  return {};
}

auto log_tail(std::string_view text, std::size_t count) -> std::vector<std::string> {
  std::vector<std::string> lines;
  if (count == 0 || text.empty()) {
    return lines;
  }
  // Walk the lines from the end: the tail is what the client asked for.
  std::size_t end = text.size();
  while (end > 0 && lines.size() < count) {
    std::size_t stop = end;
    while (stop > 0 && (text[stop - 1] == '\n' || text[stop - 1] == '\r')) {
      --stop;
    }
    if (stop == 0) {
      break;  // no more lines
    }
    std::size_t start = stop;
    while (start > 0 && text[start - 1] != '\n') {
      --start;
    }
    std::size_t length = stop - start;
    if (length > 0 && text[start + length - 1] == '\r') {
      --length;  // a CRLF terminator
    }
    lines.emplace_back(text.substr(start, length));
    end = start;
  }
  std::reverse(lines.begin(), lines.end());
  return lines;
}

}  // namespace cs2::control
