#include "cs2/control/aim_options.hpp"

#include <cmath>

namespace cs2::control {
auto apply_aim_options(const Json& args, aim::SolveInput& input, std::string& error) -> bool {
  error.clear();
  if (!args.is_object()) {
    error = "aim options must be an object";
    return false;
  }
  aim::SolveInput next = input;
  const auto number = [&](std::string_view key, float& value, float low, float high) {
    const auto* field = args.find(key);
    if (field == nullptr) return true;
    if (!field->is_number() || !std::isfinite(field->as_number()) || field->as_number() < low ||
        field->as_number() > high) {
      error = std::string(key) + " is outside its supported range";
      return false;
    }
    value = static_cast<float>(field->as_number());
    return true;
  };
  const auto boolean = [&](std::string_view key, bool& value) {
    const auto* field = args.find(key);
    if (field == nullptr) return true;
    if (!field->is_bool()) {
      error = std::string(key) + " must be boolean";
      return false;
    }
    value = field->as_bool();
    return true;
  };
  if (!number("min_damage", next.policy.minimum_damage, 0, 10000) ||
      !number("min_probability", next.policy.minimum_probability, 0, 1) ||
      !boolean("require_world", next.policy.require_known_world) ||
      !boolean("require_spread", next.policy.require_known_spread))
    return false;
  if (const auto* value = args.find("search_samples")) {
    if (!value->is_number() || value->as_number() < 1 || value->as_number() > 256 ||
        !std::isfinite(value->as_number())) {
      error = "search_samples must be1..256";
      return false;
    }
    next.policy.search_samples = static_cast<int>(value->as_number());
  }
  if (args.find("spread") != nullptr || args.find("inaccuracy") != nullptr) {
    float spread = 0, inaccuracy = 0;
    if (!number("spread", spread, 0, 2) || !number("inaccuracy", inaccuracy, 0, 2)) return false;
    next.kernel = aim::spread_kernel(spread, inaccuracy, 64);
    next.spread_known = true;
  }
  if (const auto* kernel = args.find("kernel")) {
    if (!kernel->is_array() || kernel->items().empty() || kernel->items().size() > 256) {
      error = "kernel must contain1..256 weighted offsets";
      return false;
    }
    next.kernel.clear();
    double total = 0;
    for (const auto& sample : kernel->items()) {
      if (!sample.is_array() || sample.items().size() < 2 || sample.items().size() > 3) {
        error = "kernel entries must be[x,y] or[x,y,weight]";
        return false;
      }
      const auto& values = sample.items();
      for (const auto& value : values)
        if (!value.is_number() || !std::isfinite(value.as_number())) {
          error = "kernel entries must be finite numbers";
          return false;
        }
      const float x = static_cast<float>(values[0].as_number()),
                  y = static_cast<float>(values[1].as_number());
      const float weight = values.size() == 3 ? static_cast<float>(values[2].as_number()) : 1.0F;
      if (std::abs(x) > 4 || std::abs(y) > 4 || !std::isfinite(weight) || weight < 0) {
        error = "kernel offset/weight outside supported range";
        return false;
      }
      next.kernel.push_back({{x, y}, weight});
      total += weight;
    }
    if (!(total > 0)) {
      error = "kernel must have positive total weight";
      return false;
    }
    next.spread_known = true;
  }
  if (!boolean("spread_known", next.spread_known)) return false;
  if (const auto* world = args.find("world")) {
    if (!world->is_array() || world->items().size() > 128) {
      error = "world must contain at most128 volumes";
      return false;
    }
    next.world.clear();
    next.world_complete = false;
    const auto vector = [&](const Json* value, aim::Vec3& out) {
      if (value == nullptr || !value->is_array() || value->items().size() != 3) return false;
      float xyz[3]{};
      for (int i = 0; i < 3; ++i) {
        const auto& item = value->items()[static_cast<std::size_t>(i)];
        if (!item.is_number() || !std::isfinite(item.as_number()) ||
            std::abs(item.as_number()) > 1e7)
          return false;
        xyz[i] = static_cast<float>(item.as_number());
      }
      out = {xyz[0], xyz[1], xyz[2]};
      return true;
    };
    for (const auto& volume : world->items()) {
      aim::Vec3 lo, hi;
      if (!volume.is_object() || !vector(volume.find("mins"), lo) ||
          !vector(volume.find("maxs"), hi) || lo.x >= hi.x || lo.y >= hi.y || lo.z >= hi.z) {
        error = "world volumes require ordered mins/maxs vectors";
        return false;
      }
      float retained = 0;
      if (const auto* v = volume.find("retained_per_unit")) {
        if (!v->is_number() || !std::isfinite(v->as_number()) || v->as_number() < 0 ||
            v->as_number() > 1) {
          error = "retained_per_unit must be within[0,1]";
          return false;
        }
        retained = static_cast<float>(v->as_number());
      }
      next.world.push_back({aim::aabb_obb(lo, hi), retained});
    }
  }
  if (!boolean("world_complete", next.world_complete)) return false;
  input = std::move(next);
  return true;
}
}  // namespace cs2::control
