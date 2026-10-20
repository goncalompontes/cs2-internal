// The settings **sink** must hand the features the **clamped** configuration.
//
// The review's finding: the sink (`on_movement_settings` in the payload's
// entry point) read the wire fields **directly** and handed them to the
// movement, ragebot and third-person features, so a control client's
// `{"cmd":"settings","args":{"set":{"movement_thirdperson_distance":1e9}}}`
// put `1e9` into the engine's camera-distance field. The clamp lives in
// `features::from_wire_settings` — the host test `features-config` covers the
// conversion itself — and the sink has to go **through** it.
//
// The sink lives in the payload's entry point, which needs the game, so this
// is a **source** check: the sink must not read the raw wire fields of the
// features it drives, and it must convert the block **once**.

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

// The wire field names the sink must not read: the numeric fields of the
// movement, ragebot and third-person features. Their names differ from the
// typed config's (`movement.thirdperson_distance` against the wire's
// `movement_thirdperson_distance`), so a plain substring search cannot be
// fooled by the clamped path.
constexpr const char* k_raw_fields[] = {
    "movement_thirdperson_distance",
    "movement_thirdperson_angle",
    "movement_thirdperson_pivot_z",
    "movement_thirdperson_fov",
    "movement_strafe_wishspeed",
    "movement_strafe_subticks",
    "rage_fov",
    "rage_min_damage",
    "rage_min_hitchance",
    "rage_stop_speed",
};

// The sink's body: from its definition to the closing brace at the start of a
// line. The comments are stripped: the sink's own comment names the fields it
// must **not** read.
auto sink_body(const std::string& source) -> std::string {
  const std::string marker = "void on_movement_settings(";
  const std::size_t start = source.find(marker);
  if (start == std::string::npos) {
    return {};
  }
  const std::size_t end = source.find("\n}\n", start);
  if (end == std::string::npos) {
    return {};
  }
  std::string body = source.substr(start, end - start);
  std::string stripped;
  std::istringstream stream(body);
  std::string line;
  while (std::getline(stream, line)) {
    const std::size_t comment = line.find("//");
    if (comment != std::string::npos) {
      line.erase(comment);
    }
    stripped += line;
    stripped += '\n';
  }
  return stripped;
}

}  // namespace

auto main(int argc, char** argv) -> int {
  if (argc < 2) {
    std::puts("settings-sink: no source directory given");
    return 1;
  }
  const std::string path = std::string(argv[1]) + "/payload/src/payload_main.cpp";
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    std::puts(("settings-sink: cannot read " + path).c_str());
    return 1;
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  // The line endings are normalised: the source is CRLF on Windows, and the
  // markers below are written for LF.
  std::string source = buffer.str();
  source.erase(std::remove(source.begin(), source.end(), '\r'), source.end());

  const std::string body = sink_body(source);
  if (body.empty()) {
    std::puts("settings-sink: the settings sink was not found");
    return 1;
  }

  // The sink converts the block **once**, through the clamping conversion.
  if (body.find("from_wire_settings") == std::string::npos) {
    std::puts("settings-sink: the sink does not go through from_wire_settings");
    return 1;
  }

  // ... and it does not read the raw wire fields of the features it drives.
  for (const char* field : k_raw_fields) {
    if (body.find(field) != std::string::npos) {
      std::puts((std::string("settings-sink: the sink reads the raw wire field ") + field).c_str());
      return 1;
    }
  }

  std::puts("settings-sink: ok");
  return 0;
}
