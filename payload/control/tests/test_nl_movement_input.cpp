#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include "cs2/control/input.hpp"
using namespace cs2;
namespace {
void check(bool ok, const char* why) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", why);
    std::exit(1);
  }
}
template <class T>
void put(void* p, std::size_t off, T value) {
  std::memcpy(static_cast<std::byte*>(p) + off, &value, sizeof(value));
}
template <class T>
T get(const void* p, std::size_t off) {
  T value{};
  std::memcpy(&value, static_cast<const std::byte*>(p) + off, sizeof(value));
  return value;
}
struct TraceFixture {
  float fraction{.5F};
  game::Vec3 normal{0, 0, 1};
  bool solid{};
  int calls{};
  game::Vec3 start{}, end{}, mins{}, maxs{};
} traced;
std::uintptr_t fake_physics = 1;
std::uintptr_t fake_manager = reinterpret_cast<std::uintptr_t>(&fake_physics);
void trace_filter(void*, std::uintptr_t, std::uint64_t mask, int group) {
  check(mask == 0x10060000 && group == 11, "movement collision mask/group reach native filter");
}
bool trace_hull(void*, void* ray, const game::Vec3* start, const game::Vec3* end, const void*,
                void* result) {
  ++traced.calls;
  traced.start = *start;
  traced.end = *end;
  traced.mins = get<game::Vec3>(ray, 0);
  traced.maxs = get<game::Vec3>(ray, 12);
  put(result, 0xac, traced.fraction);
  put(result, 0x90, traced.normal);
  put(result, 0xb6, static_cast<std::uint8_t>(traced.solid));
  return true;
}
struct Fixture {
  alignas(8) std::array<std::byte, 0x100> pawn{}, services{}, base{}, cmd{}, angles{}, buttons{},
      entry{}, history_angles{};
  alignas(8) std::array<std::byte, 8 + 64 * 8> rep{};
  alignas(8) std::array<std::byte, 16> history_rep{};
  std::array<std::array<std::byte, 0x40>, 64> steps{};
  Fixture(bool ground = false) {
    put(pawn.data(), 8, reinterpret_cast<std::uintptr_t>(services.data()));
    put(pawn.data(), 0x10, 100);
    put(pawn.data(), 0x14, std::uint8_t{0});
    put(pawn.data(), 0x18, ground ? 1u : 0u);
    put(pawn.data(), 0x1c, std::uint8_t{2});
    put(pawn.data(), 0x20, game::Vec3{300, 0, 0});
    put(services.data(), 0x30, 250.0F);
    put(services.data(), 0x34, 1.0F);
    put(services.data(), 0x38, .6F);
    put(services.data(), 0x3c, .2F);
    put(cmd.data(), 8, 100);
    put(cmd.data(), 0x40, reinterpret_cast<std::uintptr_t>(base.data()));
    put(cmd.data(), 0x60, control::button::k_forward);
    put(base.data(), 0x40, reinterpret_cast<std::uintptr_t>(angles.data()));
    put(base.data(), 0x38, reinterpret_cast<std::uintptr_t>(buttons.data()));
    put(base.data(), 0x54, 100);
    put(base.data(), 0x58, 1.0F);
    put(base.data(), 0x28, reinterpret_cast<std::uintptr_t>(rep.data()));
    put(rep.data(), 0, 64);
    for (int i = 0; i < 64; ++i)
      put(rep.data(), 8 + i * 8, reinterpret_cast<std::uintptr_t>(steps[i].data()));
    put(cmd.data(), 0x30, 1u);
    put(cmd.data(), 0x38, reinterpret_cast<std::uintptr_t>(history_rep.data()));
    put(history_rep.data(), 0, 1u);
    put(history_rep.data(), 8, reinterpret_cast<std::uintptr_t>(entry.data()));
    put(entry.data(), 0x18, reinterpret_cast<std::uintptr_t>(history_angles.data()));
  }
  int count() const {
    return get<int>(base.data(), 0x20);
  }
  void* step(int i) {
    return get<void*>(rep.data(), 8 + i * 8);
  }
  game::Vec2 integrated_velocity(float air_acceleration = 12, game::Vec2 initial = {300, 0}) {
    game::Vec2 velocity = initial;
    bool analog = false;
    for (int i = 0; i < count(); ++i) analog = analog || (get<unsigned>(step(i), 0x10) & 0x18u);
    game::Vec2 axes =
        analog ? game::Vec2{.6F, .2F}
               : game::Vec2{get<float>(base.data(), 0x58), get<float>(base.data(), 0x5c)};
    float yaw = get<float>(angles.data(), 0x1c), previous = 0;
    const auto advance = [&](float dt) {
      const float length = game::length(axes);
      if (length < .001F) return;
      const float rad = yaw * control::k_pi / 180;
      const game::Vec2 wish{(std::cos(rad) * axes.x - std::sin(rad) * axes.y) / length,
                            (std::sin(rad) * axes.x + std::cos(rad) * axes.y) / length};
      const float wish_speed = 250 * std::min(1.0F, length);
      const float gain =
          std::min(std::max(0.0F, std::min(30.0F, wish_speed) - game::dot(velocity, wish)),
                   air_acceleration * wish_speed * dt / 64);
      velocity = velocity + wish * gain;
    };
    for (int i = 0; i < count(); ++i) {
      auto* p = step(i);
      const auto bits = get<unsigned>(p, 0x10);
      const float when = get<float>(p, 0x24);
      advance(when - previous);
      previous = when;
      if (bits & 0x40u) yaw += get<float>(p, 0x34);
      if (bits & 8u) axes.x += get<float>(p, 0x28);
      if (bits & 16u) axes.y += get<float>(p, 0x2c);
    }
    advance(1 - previous);
    return velocity;
  }
  float final_yaw() {
    float yaw = get<float>(angles.data(), 0x1c);
    for (int i = 0; i < count(); ++i)
      if (get<unsigned>(step(i), 0x10) & 0x40u) yaw += get<float>(step(i), 0x34);
    return std::remainder(yaw, 360.0F);
  }
  game::Vec2 final_world() {
    bool analog = false;
    for (int i = 0; i < count(); ++i) analog = analog || (get<unsigned>(step(i), 0x10) & 0x18);
    game::Vec2 value =
        analog ? game::Vec2{.6F, .2F}
               : game::Vec2{get<float>(base.data(), 0x58), get<float>(base.data(), 0x5c)};
    float yaw_delta = 0;
    for (int i = 0; i < count(); ++i) {
      const auto* p = step(i);
      const auto bits = get<unsigned>(p, 0x10);
      if ((bits & 0x40) && get<float>(p, 0x24) < .99999F) yaw_delta += get<float>(p, 0x34);
      if (bits & 8) value.x += get<float>(p, 0x28);
      if (bits & 16) value.y += get<float>(p, 0x2c);
    }
    const float length = game::length(value);
    if (length > 1) value = value * (1 / length);
    const float a = (get<float>(angles.data(), 0x1c) + yaw_delta) * control::k_pi / 180;
    return {std::cos(a) * value.x - std::sin(a) * value.y,
            std::sin(a) * value.x + std::cos(a) * value.y};
  }
};
}  // namespace
namespace cs2::control {
struct NlMovementInputTestAccess {
  static void setup(Input& input, Fixture& f, bool strafe, bool hop) {
    input.local_pawn_.store(reinterpret_cast<std::uintptr_t>(f.pawn.data()));
    input.flags_offset_ = 0x18;
    input.velocity_offset_ = 0x20;
    input.stop_enabled_.store(false);
    auto& o = input.nl_fields_;
    o.ready = true;
    o.movement = 8;
    o.health = 0x10;
    o.life = 0x14;
    o.flags = 0x18;
    o.move_type = 0x1c;
    o.velocity = 0x20;
    o.buttons = 0x10;
    o.max_speed = 0x30;
    o.friction = 0x34;
    o.forward = 0x38;
    o.left = 0x3c;
    o.modern = 0x40;
    o.land_tick = 4;
    o.land_frac = 8;
    o.actual_tick = 0x10;
    o.actual_frac = 0x14;
    o.last_processed = 0x68;
    o.stamina = 0x60;
    o.duck = 0x64;
    input.alloc_step_ = [](void*) -> void* { return nullptr; };
    input.add_step_ = [](void*, void*) -> void* { return nullptr; };
    input.set_strafe(strafe);
    input.set_bhop(hop);
    input.set_nl_movement_options(16, 1);
  }
  static void apply(Input& input, Fixture& f) {
    input.apply(f.cmd.data());
  }
  static void camera(Input& input, game::Vec3& camera) {
    input.ccsgo_input_ = reinterpret_cast<std::uintptr_t>(&camera);
    NativeCommandApi api;
    api.command_base = [](void*) -> void* { return nullptr; };
    api.command = [](void*, std::uint32_t) -> void* { return nullptr; };
    api.assign_crc = [](std::uintptr_t*, const EngineStringView*, void*) {};
    api.allocate_buttons = [](void*) -> void* { return nullptr; };
    api.get_view = [](void* view, int) -> game::Vec3* { return static_cast<game::Vec3*>(view); };
    api.set_view = [](void* view, int, const game::Vec3* value) {
      *static_cast<game::Vec3*>(view) = *value;
    };
    api.handle_view = reinterpret_cast<void*>(1);
    input.native_command_.bind(api);
  }
  static void balanced(Input& input) {
    input.nl_physics_state_ = {100, 30, 60, false, true};
    input.nl_physics_valid_.store(true);
    input.nl_quantized_.store(false);
    input.set_nl_movement_options(30, .99F);
  }
  static void apply_view_handler(Input& input, game::Vec3& camera) {
    input.original_view_ = reinterpret_cast<void*>(+[](void* value, int) {
      auto& view = *static_cast<game::Vec3*>(value);
      view.x += 10;
      view.y += 85;
    });
    input.preserve_command_view(&camera, 0);
    input.original_view_ = nullptr;
  }
  static void bind_trace(Input& input, Fixture& f) {
    auto& o = input.nl_fields_;
    o.trace_ready = true;
    o.scene = 0x50;
    o.origin = 0x60;
    o.collision = 0x80;
    o.mins = 0;
    o.maxs = 12;
    o.attribute = 0x20;
    o.mask = 0;
    o.gravity = 0x44;
    put(f.pawn.data(), 0x50, reinterpret_cast<std::uintptr_t>(f.pawn.data()));
    put(f.pawn.data(), 0x60, game::Vec3{0, 0, 10});
    put(f.pawn.data(), 0x80, game::Vec3{-16, -16, 0});
    put(f.pawn.data(), 0x8c, game::Vec3{16, 16, 54});
    put(f.pawn.data(), 0xa0, std::uint64_t{0x10060000});
    put(f.pawn.data(), 0x44, 1.0F);
    input.nl_trace_.bind(
        {&trace_filter, &trace_hull, reinterpret_cast<std::uintptr_t>(&fake_manager)});
  }
  static void break_allocator(Input& input) {
    input.alloc_step_ = nullptr;
  }
};
}  // namespace cs2::control
int main() {
  using namespace control;
  Fixture f;
  Input input;
  NlMovementInputTestAccess::setup(input, f, true, false);
  NlMovementInputTestAccess::apply(input, f);
  check(f.count() == 17 && input.strafe_steps() == 16,
        "subtick steering writes a complete bounded plan");
  check(get<float>(f.angles.data(), 0x1c) == 0, "steering leaves view angle alone");
  for (int pass = 0; pass < 5; ++pass) {
    put(f.base.data(), 0x58, 1.0F);
    put(f.angles.data(), 0x1c, 0.0F);
    NlMovementInputTestAccess::apply(input, f);
    check(f.count() == 17,
          "repeated command replaces analog events instead of appending duplicates");
  }
  // Interpret every native slice, including diagonal normalization. Checking only
  // the final wish misses a broken timeline and huge intermediate yaw reversals.
  aim::NlAirState air;
  air.velocity = {300, 0};
  air.directional_feedback = true;
  air.phase = 0;
  air.allow_counter = false;
  air.smooth_yaw = true;
  const auto reference = aim::nl_air_plan(air);
  game::Vec2 impulses{.6F, .2F};
  float movement_yaw = 0, total_yaw = 0;
  game::Vec2 decoded_velocity{300, 0};
  for (int i = 0; i < reference.count; ++i) {
    auto* event = f.step(i);
    const float delta = get<float>(event, 0x34);
    check(std::fabs(delta) <= 45.001F,
          "each intra-command turn respects the 45-degree admission bound");
    movement_yaw += delta;
    total_yaw += std::fabs(delta);
    impulses.x += get<float>(event, 0x28);
    impulses.y += get<float>(event, 0x2c);
    check(std::fabs(impulses.x - std::round(impulses.x)) < .001F &&
              std::fabs(impulses.y - std::round(impulses.y)) < .001F,
          "every slice lands on the native digital movement grid");
    const float radians = movement_yaw * k_pi / 180;
    const float magnitude = game::length(impulses);
    game::Vec2 world{(std::cos(radians) * impulses.x - std::sin(radians) * impulses.y) / magnitude,
                     (std::sin(radians) * impulses.x + std::cos(radians) * impulses.y) / magnitude};
    check(game::dot(world, reference.events[i].world_wish) > .99999F,
          "every decoded native slice matches the directional world wish");
    const float when = get<float>(event, 0x24);
    const float end = get<float>(f.step(i + 1), 0x24);
    const float gain = std::min(std::max(0.0F, 30 - game::dot(decoded_velocity, world)),
                                12 * 250 * (end - when) / 64);
    decoded_velocity = decoded_velocity + world * gain;
  }
  auto* restore = f.step(f.count() - 1);
  check(get<float>(restore, 0x24) == std::nextafter(1.0F, 0.0F),
        "restoration occurs only at the command boundary");
  check(std::fabs(std::remainder(movement_yaw + get<float>(restore, 0x34), 360)) < .001F,
        "complete command cancels synthetic steering yaw");
  check(game::length(decoded_velocity) > 303,
        "encoded straight-input steering must accelerate rather than merely preserve speed");
  check(game::length(decoded_velocity - reference.final_velocity) < .001F,
        "decoded command acceleration matches the modeled emitted wish");
  check(total_yaw <= reference.count * 45.001F,
        "each admitted steering slice respects its yaw bound");
  put(f.cmd.data(), 0x60, button::k_back);
  put(f.base.data(), 0x58, -1.0F);
  NlMovementInputTestAccess::apply(input, f);
  check(input.strafe_steps() == 16 && get<float>(f.base.data(), 0x58) == -1 &&
            input.nl_counter() < .00001F,
        "backward intent retains directional assistance without braking");
  Fixture left;
  Input leftward;
  NlMovementInputTestAccess::setup(leftward, left, true, false);
  put(left.base.data(), 0x58, 0.0F);
  put(left.cmd.data(), 0x60, button::k_moveleft);
  NlMovementInputTestAccess::apply(leftward, left);
  check(std::fabs(leftward.strafe_goal() - 90) < .001F,
        "A requests camera-left even when base analog axes are zero");
  std::set<void*> pointers;
  for (int i = 0; i < 64; ++i)
    check(pointers.insert(f.step(i)).second, "all allocated active/spare pointers remain unique");
  Fixture baseline, aimed;
  Input plain, silent;
  NlMovementInputTestAccess::setup(plain, baseline, true, false);
  NlMovementInputTestAccess::setup(silent, aimed, true, false);
  NlMovementInputTestAccess::apply(plain, baseline);
  silent.set_rage_fire(true);
  silent.set_rage_aim(5, 90, 3, 1);
  NlMovementInputTestAccess::apply(silent, aimed);
  const auto expected = baseline.final_world(), actual = aimed.final_world();
  check(std::fabs(expected.x - actual.x) < .001F && std::fabs(expected.y - actual.y) < .001F,
        "silent aim rebases initial impulses as well as analog deltas");
  check(get<float>(aimed.angles.data(), 0x1c) == 90, "silent aim still commits its shot yaw");
  Fixture ground(true);
  Input hop;
  NlMovementInputTestAccess::setup(hop, ground, false, true);
  put(ground.cmd.data(), 0x60, button::k_forward | button::k_jump);
  NlMovementInputTestAccess::apply(hop, ground);
  int presses = 0, releases = 0;
  float when = -1;
  for (int i = 0; i < ground.count(); ++i) {
    void* p = ground.step(i);
    if (get<std::uint64_t>(p, 0x18) & button::k_jump) {
      if (get<std::uint8_t>(p, 0x20)) {
        ++presses;
        when = get<float>(p, 0x24);
      } else {
        ++releases;
        check(get<float>(p, 0x24) == when,
              "dump jump pulse preserves press/release order at equal fraction");
      }
    }
  }
  check(presses == 1 && releases == 0 && ground.count() == 2,
        "hop pulse and movement reconciliation coexist");
  check(std::fabs(ground.final_world().x - 1) < .001F && std::fabs(ground.final_world().y) < .001F,
        "jump-only steps do not retain stale movement impulses");

  check(when == 0 && !(get<std::uint64_t>(ground.cmd.data(), 0x70) & button::k_jump),
        "ground jump is immediate and has no extra scroll edge");
  Fixture released;
  Input no_jump;
  NlMovementInputTestAccess::setup(no_jump, released, false, true);
  put(released.services.data(), 0x18, button::k_jump);
  NlMovementInputTestAccess::apply(no_jump, released);
  check(released.count() == 0,
        "stale simulated held state cannot keep hopping after the input is released");

  Fixture stable(true);
  Input stable_hop;
  NlMovementInputTestAccess::setup(stable_hop, stable, false, true);
  put(stable.cmd.data(), 0x60, button::k_forward | button::k_jump);
  NlMovementInputTestAccess::apply(stable_hop, stable);
  put(stable.pawn.data(), 0x18, 0u);
  put(stable.pawn.data(), 0x20, game::Vec3{300, 0, 250});
  put(stable.cmd.data(), 0x60, button::k_forward | button::k_jump);
  NlMovementInputTestAccess::apply(stable_hop, stable);
  check(stable_hop.bhop_mode() == 3 && stable.count() == 2,
        "same-command ascent does not erase the accepted jump edge");
  put(stable.cmd.data(), 8, 101);
  put(stable.cmd.data(), 0x60, button::k_forward | button::k_jump);
  NlMovementInputTestAccess::apply(stable_hop, stable);
  for (int n = 0; n < stable.count(); ++n)
    check(!(get<std::uint64_t>(stable.step(n), 0x18) & button::k_jump),
          "next ascending command releases instead of repeating the cached edge");
  Fixture contact;
  Input landing;
  NlMovementInputTestAccess::setup(landing, contact, true, true);
  NlMovementInputTestAccess::bind_trace(landing, contact);
  put(contact.pawn.data(), 0x20, game::Vec3{300, 0, -100});
  put(contact.cmd.data(), 0x60, button::k_forward | button::k_jump);
  traced = {};
  NlMovementInputTestAccess::apply(landing, contact);
  check(landing.bhop_mode() == 6 && landing.bhop_when() == .5F,
        "falling command schedules jump at native hull contact, before old landing record updates");
  check(traced.calls == 1 && traced.maxs.z == 54 &&
            std::fabs(traced.end.z - (10 - 106.25F / 64)) < .001F,
        "landing uses current crouched hull and gravity without early downward padding");
  MovementTrace oracle;
  oracle.bind({&trace_filter, &trace_hull, reinterpret_cast<std::uintptr_t>(&fake_manager)});
  LandingQuery query{1, 0x10060000, {0, 0, 10}, {300, 0, -100}, {-16, -16, 0}, {16, 16, 54}};
  traced.normal.z = .2F;
  check(!oracle.landing(query), "wall contact is not a landing");
  traced.normal.z = 1;
  traced.solid = true;
  check(!oracle.landing(query), "start-solid hull is not used as a landing time");
  traced.solid = false;
  query.velocity.z = 100;
  check(!oracle.landing(query), "ascending hull does not request an airborne jump");
  Fixture launch(true);
  Input assisted;
  NlMovementInputTestAccess::setup(assisted, launch, true, true);
  put(launch.cmd.data(), 0x60, button::k_forward | button::k_jump);
  NlMovementInputTestAccess::apply(assisted, launch);
  check(assisted.strafe_steps() == 0 && launch.count() == 2 &&
            get<float>(launch.base.data(), 0x58) == 1 &&
            (get<std::uint64_t>(launch.cmd.data(), 0x60) & button::k_forward),
        "jump launch retains native movement and queues no air steering on ground");
  put(launch.cmd.data(), 0x60, button::k_forward);
  put(launch.cmd.data(), 0x70, std::uint64_t{0});
  put(launch.angles.data(), 0x1c, 0.0F);
  put(launch.base.data(), 0x58, 1.0F);
  put(launch.base.data(), 0x5c, 0.0F);
  NlMovementInputTestAccess::apply(assisted, launch);
  for (int i = 0; i < launch.count(); ++i)
    check(!(get<std::uint64_t>(launch.step(i), 0x18) & button::k_jump),
          "releasing jump removes an owned pulse from a rebuilt command");

  Fixture launch_plain(true), launch_aim(true);
  Input jumping, jumping_aim;
  NlMovementInputTestAccess::setup(jumping, launch_plain, true, true);
  NlMovementInputTestAccess::setup(jumping_aim, launch_aim, true, true);
  put(launch_plain.cmd.data(), 0x60, button::k_forward | button::k_jump);
  put(launch_aim.cmd.data(), 0x60, button::k_forward | button::k_jump);
  jumping_aim.set_rage_fire(true);
  jumping_aim.set_rage_aim(5, 73, 3, 1);
  NlMovementInputTestAccess::apply(jumping, launch_plain);
  NlMovementInputTestAccess::apply(jumping_aim, launch_aim);
  const auto launch_expected = launch_plain.final_world(), launch_actual = launch_aim.final_world();
  check(std::fabs(launch_expected.x - launch_actual.x) < .001F &&
            std::fabs(launch_expected.y - launch_actual.y) < .001F,
        "jump-launch steering survives arbitrary silent shot yaw");
  // Failed allocation restores old mixed events and active pointer order.
  Fixture rollback;
  Input failed;
  NlMovementInputTestAccess::setup(failed, rollback, true, false);
  put(rollback.base.data(), 0x20, 1);
  put(rollback.steps[0].data(), 0x10, 0x1fu);
  put(rollback.steps[0].data(), 0x18, button::k_attack);
  put(rollback.steps[0].data(), 0x20, std::uint8_t{1});
  auto saved = rollback.steps[0];
  NlMovementInputTestAccess::break_allocator(failed);
  NlMovementInputTestAccess::apply(failed, rollback);
  check(rollback.count() == 1 && rollback.steps[0] == saved,
        "allocation failure rolls back old mixed command payload");

  Fixture full;
  Input limited;
  NlMovementInputTestAccess::setup(limited, full, true, false);
  put(full.base.data(), 0x20, 32);
  for (int i = 0; i < 32; ++i) {
    put(full.steps[i].data(), 0x10, 7u);
    put(full.steps[i].data(), 0x18, button::k_attack);
    put(full.steps[i].data(), 0x20, std::uint8_t{1});
    put(full.steps[i].data(), 0x24, i / 64.0F);
  }
  const auto before_full = full.steps;
  NlMovementInputTestAccess::apply(limited, full);
  check(full.count() == 32 && full.steps == before_full && limited.nl_movement_reason() == 9,
        "full button-event budget is preserved without overflow");
  Fixture mixed;
  Input preserved;
  NlMovementInputTestAccess::setup(preserved, mixed, true, false);
  put(mixed.base.data(), 0x20, 1);
  put(mixed.steps[0].data(), 0x10, 0x47u);
  put(mixed.steps[0].data(), 0x18, button::k_attack);
  put(mixed.steps[0].data(), 0x20, std::uint8_t{1});
  put(mixed.steps[0].data(), 0x24, .5F);
  put(mixed.steps[0].data(), 0x34, 10.0F);
  NlMovementInputTestAccess::apply(preserved, mixed);
  bool found = false;
  for (int i = 0; i < mixed.count(); ++i) {
    auto* p = mixed.step(i);
    if (get<std::uint64_t>(p, 0x18) & button::k_attack)
      found = get<float>(p, 0x24) == .5F && get<std::uint8_t>(p, 0x20) == 1;
  }
  check(found && mixed.count() > 1 && mixed.count() <= 32,
        "existing attack and yaw payloads survive analog replacement");
  check(std::fabs(mixed.final_yaw() - 10) < .001F,
        "shared physical mouse timestamp preserves total player yaw after the restoration tail");
  const int mixed_count = mixed.count();
  for (int i = 0; i < mixed.count(); ++i) put(mixed.step(i), 0x14, 1234u);
  NlMovementInputTestAccess::apply(preserved, mixed);
  check(mixed.count() == mixed_count,
        "serialization cache updates do not break ownership or accumulate steering on rebuild");
  // The engine may recycle an owned zero-yaw reconciliation entry as a real
  // analog event while rebuilding this same command. A matching pointer/yaw
  // alone is not evidence that the new contents still belong to the assist.
  auto* recycled = mixed.step(0);
  put(recycled, 0x10, 0x1cu);
  put(recycled, 0x24, .25F);
  put(recycled, 0x28, .4F);
  put(recycled, 0x2c, -.2F);
  std::array<std::byte, 0x28> recycled_bytes{};
  std::memcpy(recycled_bytes.data(), static_cast<std::byte*>(recycled) + 0x10,
              recycled_bytes.size());
  NlMovementInputTestAccess::apply(preserved, mixed);
  bool recycled_preserved = false;
  for (int i = 0; i < mixed.count(); ++i)
    if (mixed.step(i) == recycled)
      recycled_preserved =
          std::memcmp(recycled_bytes.data(), static_cast<std::byte*>(recycled) + 0x10,
                      recycled_bytes.size()) == 0;
  check(recycled_preserved,
        "engine-written analog data survives reuse of an owned zero-yaw pointer");
  Fixture rotated;
  Input rotated_input;
  NlMovementInputTestAccess::setup(rotated_input, rotated, true, false);
  game::Vec3 camera{};
  NlMovementInputTestAccess::camera(rotated_input, camera);
  put(rotated.angles.data(), 0x1c, 90.0F);
  put(rotated.base.data(), 0x58, 0.0F);
  put(rotated.base.data(), 0x5c, -1.0F);
  NlMovementInputTestAccess::apply(rotated_input, rotated);
  check(std::fabs(rotated_input.strafe_goal()) < .001F,
        "W travel intent follows real camera rather than the already rotated command yaw");
  check(get<float>(rotated.base.data(), 0x58) == 0 && get<float>(rotated.base.data(), 0x5c) == -1,
        "command-frame movement correction survives directional steering");
  aim::NlAirState rotated_plan;
  rotated_plan.velocity = {300, 0};
  rotated_plan.smooth_yaw = true;
  rotated_plan.allow_counter = false;
  rotated_plan.directional_feedback = true;
  const auto rotated_expected = aim::nl_air_plan(rotated_plan);
  check(
      game::dot(rotated.final_world(),
                rotated_expected.events[rotated_expected.count - 1].world_wish) > .99999F,
      "rotated command and real view produce the same decoded world steering as an unfaked frame");
  Fixture mouse;
  Input mouse_input;
  NlMovementInputTestAccess::setup(mouse_input, mouse, true, false);
  put(mouse.base.data(), 0x20, 1);
  put(mouse.steps[0].data(), 0x10, 0x44u);
  put(mouse.steps[0].data(), 0x24, .875F);
  put(mouse.steps[0].data(), 0x34, 15.0F);
  const auto physical_mouse = mouse.steps[0];
  NlMovementInputTestAccess::apply(mouse_input, mouse);
  check(get<unsigned>(mouse.steps[0].data(), 0x10) == get<unsigned>(physical_mouse.data(), 0x10) &&
            get<float>(mouse.steps[0].data(), 0x24) == .875F,
        "shared mouse entry preserves physical timestamp and payload presence");
  check(game::length(mouse.integrated_velocity()) > 303,
        "late mouse events retain substantial decoded gain instead of reducing it to the final "
        "slices");
  check(std::fabs(mouse.final_yaw() - 15) < .001F,
        "command finishes with physical mouse yaw and no remaining synthetic steering yaw");
  NlMovementInputTestAccess::apply(mouse_input, mouse);
  check(std::fabs(mouse.final_yaw() - 15) < .001F,
        "rebuilt mouse command has exactly one restoration tail");
  NlMovementInputTestAccess::apply_view_handler(rotated_input, camera);
  check(camera.x == 0 && camera.y == 0 && rotated_input.camera_restores() == 1,
        "view handler's synthetic camera update is restored without altering command steering");
  check(std::fabs(aimed.final_yaw() - 90) < .001F,
        "silent shot frame remains its requested yaw after the steering tail");
  mouse_input.set_strafe(false);
  NlMovementInputTestAccess::apply(mouse_input, mouse);
  check(mouse.count() == 1 && mouse.steps[0] == physical_mouse,
        "disabling the feature restores the physical mouse delta and retires synthetic steps");
  Fixture dense_mouse;
  Input dense_input;
  NlMovementInputTestAccess::setup(dense_input, dense_mouse, true, false);
  put(dense_mouse.base.data(), 0x20, 16);
  for (int i = 0; i < 16; ++i) {
    put(dense_mouse.steps[i].data(), 0x10, 0x44u);
    put(dense_mouse.steps[i].data(), 0x24, static_cast<float>(i + 1) / 17);
    put(dense_mouse.steps[i].data(), 0x34, .5F);
  }
  NlMovementInputTestAccess::apply(dense_input, dense_mouse);
  check(dense_mouse.count() <= 32 && dense_input.strafe_steps() > 0 &&
            game::length(dense_mouse.integrated_velocity()) > 303,
        "sixteen physical mouse events retain accelerating steering within the shared 32-entry "
        "limit");
  check(std::fabs(dense_mouse.final_yaw() - 8) < .001F,
        "dense mouse input retains physical yaw without steering residue");
  Fixture repeated_state;
  Input frozen_state;
  NlMovementInputTestAccess::setup(frozen_state, repeated_state, true, false);
  put(repeated_state.services.data(), 0x68, 99);
  NlMovementInputTestAccess::apply(frozen_state, repeated_state);
  const auto gain_before = frozen_state.strafe_plan_gain();
  const auto steps_before = repeated_state.steps;
  put(repeated_state.pawn.data(), 0x20, game::Vec3{700, 150, 0});
  put(repeated_state.services.data(), 0x38, -1.0F);
  put(repeated_state.services.data(), 0x3c, 1.0F);
  put(repeated_state.services.data(), 0x68, 100);
  NlMovementInputTestAccess::apply(frozen_state, repeated_state);
  check(std::fabs(frozen_state.strafe_plan_gain() - gain_before) < .0001F &&
            frozen_state.movement_last_processed() == 99 &&
            frozen_state.movement_velocity().x == 300,
        "duplicate callbacks keep the original command prestate rather than its simulated output");
  check(repeated_state.steps == steps_before,
        "same command encodes identical steering after movement services advance");
  put(repeated_state.cmd.data(), 8, 101);
  put(repeated_state.base.data(), 0x20, 0);
  NlMovementInputTestAccess::apply(frozen_state, repeated_state);
  check(frozen_state.movement_velocity().x == 700 && frozen_state.movement_last_processed() == 100,
        "next command acquires a fresh pre-prediction state");
  Fixture too_late;
  Input late_phase;
  NlMovementInputTestAccess::setup(late_phase, too_late, true, false);
  put(too_late.services.data(), 0x68, 100);
  NlMovementInputTestAccess::apply(late_phase, too_late);
  check(
      too_late.count() == 0 && late_phase.nl_movement_reason() == 17,
      "first encounter after simulation does not invent a prestate or rewrite processed movement");
  Fixture paired;
  Input paired_input;
  NlMovementInputTestAccess::setup(paired_input, paired, true, false);
  NlMovementInputTestAccess::apply(paired_input, paired);
  NlMovementInputTestAccess::balanced(paired_input);
  NlMovementInputTestAccess::apply(paired_input, paired);
  const auto paired_velocity = paired.integrated_velocity(100);
  check(paired.count() <= 32 && paired_velocity.x > 310 && std::fabs(paired_velocity.y) < .003F,
        "decoded 99-percent/30-subtick analog commands gain speed and cancel sideways velocity");
  for (int i = 0; i < paired.count(); ++i)
    check(!(get<unsigned>(paired.step(i), 0x10) & 0x40u),
          "unquantized paired path emits no synthetic view yaw");
  check(std::fabs(paired.final_yaw()) < .001F && std::fabs(paired.final_world().x - 1) < .001F &&
            std::fabs(paired.final_world().y) < .001F,
        "paired command finishes with native view and manual movement axes");
  const auto paired_steps = paired.steps;
  NlMovementInputTestAccess::apply(paired_input, paired);
  check(paired.steps == paired_steps, "paired analog rebuild remains command-stable");
  paired_input.set_rage_aim(5, 73, 3, 1);
  paired_input.set_rage_fire(true);
  NlMovementInputTestAccess::apply(paired_input, paired);
  const auto paired_aim_velocity = paired.integrated_velocity(100);
  check(game::length(paired_aim_velocity - paired_velocity) < .01F,
        "silent shot frame correction preserves paired analog acceleration");
  Fixture paired_diagonal;
  Input diagonal_input;
  NlMovementInputTestAccess::setup(diagonal_input, paired_diagonal, true, false);
  const game::Vec2 diagonal_velocity{212.132034F, 212.132034F};
  put(paired_diagonal.pawn.data(), 0x20, game::Vec3{diagonal_velocity.x, diagonal_velocity.y, 0});
  put(paired_diagonal.cmd.data(), 0x60, button::k_forward | button::k_moveleft);
  put(paired_diagonal.base.data(), 0x5c, 1.0F);
  game::Vec3 diagonal_camera{};
  NlMovementInputTestAccess::camera(diagonal_input, diagonal_camera);
  NlMovementInputTestAccess::apply(diagonal_input, paired_diagonal);
  NlMovementInputTestAccess::balanced(diagonal_input);
  NlMovementInputTestAccess::apply(diagonal_input, paired_diagonal);
  const auto diagonal_before = paired_diagonal.integrated_velocity(100, diagonal_velocity);
  diagonal_input.set_rage_aim(5, 73, 3, 1);
  diagonal_input.set_rage_fire(true);
  NlMovementInputTestAccess::apply(diagonal_input, paired_diagonal);
  check(game::length(paired_diagonal.integrated_velocity(100, diagonal_velocity) -
                     diagonal_before) < .02F,
        "diagonal silent aim rotates absolute analog states without weakening paired acceleration");
  check((get<std::uint64_t>(paired_diagonal.cmd.data(), 0x60) &
         (button::k_forward | button::k_moveleft | button::k_moveright | button::k_back)) ==
            (button::k_forward | button::k_moveleft),
        "paired shot correction preserves physical direction keys");
  NlMovementInputTestAccess::apply(diagonal_input, paired_diagonal);
  check(std::fabs(diagonal_input.strafe_goal() - 45) < .001F,
        "duplicate paired command keeps real diagonal intent after a silent shot");
  check(game::length(paired_diagonal.integrated_velocity(100, diagonal_velocity) -
                     diagonal_before) < .02F,
        "paired gain survives repeated diagonal silent-shot command builds");
  Fixture late;
  Input late_input;
  NlMovementInputTestAccess::setup(late_input, late, true, false);
  put(late.base.data(), 0x20, 1);
  put(late.steps[0].data(), 0x10, 0x1cu);
  put(late.steps[0].data(), 0x24, 63.0F / 64);
  put(late.steps[0].data(), 0x34, 10.0F);
  const auto late_step = late.steps[0];
  NlMovementInputTestAccess::apply(late_input, late);
  check(
      late.count() == 1 && late.steps[0] == late_step && late_input.strafe_steps() == 0,
      "late physical input retains native control instead of receiving an earlier synthetic turn");
  Fixture keys;
  Input priority;
  NlMovementInputTestAccess::setup(priority, keys, true, false);
  put(keys.cmd.data(), 0x60, button::k_moveleft);
  NlMovementInputTestAccess::apply(priority, keys);
  check(std::fabs(priority.strafe_goal() - 90) < .001F, "first A steers left");
  put(keys.cmd.data(), 0x60, button::k_moveleft | button::k_moveright);
  NlMovementInputTestAccess::apply(priority, keys);
  check(std::fabs(priority.strafe_goal() + 90) < .001F && priority.strafe_steps() > 0,
        "new D overrides held A instead of disabling movement");
  put(keys.cmd.data(), 0x60, button::k_moveleft);
  NlMovementInputTestAccess::apply(priority, keys);
  check(std::fabs(priority.strafe_goal() - 90) < .001F, "releasing D restores held A");
  priority.set_nl_movement_options(16, 0);
  NlMovementInputTestAccess::apply(priority, keys);
  check(keys.count() == 0 && priority.strafe_steps() == 0,
        "zero steering strength restores untouched manual control");
  Fixture transition;
  Input direction_change;
  NlMovementInputTestAccess::setup(direction_change, transition, true, false);
  put(transition.base.data(), 0x20, 1);
  put(transition.steps[0].data(), 0x10, 7u);
  put(transition.steps[0].data(), 0x18, button::k_forward);
  put(transition.steps[0].data(), 0x20, std::uint8_t{0});
  put(transition.steps[0].data(), 0x24, .5F);
  NlMovementInputTestAccess::apply(direction_change, transition);
  bool release_preserved = false;
  for (int i = 0; i < transition.count(); ++i)
    if (get<std::uint64_t>(transition.step(i), 0x18) & button::k_forward)
      release_preserved = !get<std::uint8_t>(transition.step(i), 0x20) &&
                          get<float>(transition.step(i), 0x24) == .5F;
  check(release_preserved, "physical W release remains in the native movement timeline");
  // Exercise all travel directions, at rest and in the air. The previous
  // regression tests decoded our synthetic output but never required the
  // original movement buttons and base axes to survive.
  constexpr std::uint64_t travel[] = {button::k_forward,
                                      button::k_back,
                                      button::k_moveleft,
                                      button::k_moveright,
                                      button::k_forward | button::k_moveleft,
                                      button::k_forward | button::k_moveright};
  for (bool on_ground : {false, true})
    for (auto keys : travel) {
      Fixture manual(on_ground);
      Input controlled;
      NlMovementInputTestAccess::setup(controlled, manual, true, true);
      const float forward =
          (keys & button::k_forward ? 1.0F : 0) - (keys & button::k_back ? 1.0F : 0);
      const float left =
          (keys & button::k_moveleft ? 1.0F : 0) - (keys & button::k_moveright ? 1.0F : 0);
      put(manual.cmd.data(), 0x60, keys | button::k_jump);
      put(manual.base.data(), 0x58, forward);
      put(manual.base.data(), 0x5c, left);
      NlMovementInputTestAccess::apply(controlled, manual);
      constexpr auto directions =
          button::k_forward | button::k_back | button::k_moveleft | button::k_moveright;
      if (!on_ground && (keys & (button::k_back | button::k_moveleft | button::k_moveright)))
        check(controlled.strafe_steps() > 0 && controlled.nl_counter() < .00001F,
              "directional assistance remains active without opposing velocity");
      check((get<std::uint64_t>(manual.cmd.data(), 0x60) & directions) == keys &&
                get<float>(manual.base.data(), 0x58) == forward &&
                get<float>(manual.base.data(), 0x5c) == left,
            "WASD and diagonals survive assist unchanged, including jump launches");
    }
  // The native subtick loop begins from previous service impulses, not from
  // base axes. A negative delta here is a release/correction, not a brake wish.
  Fixture opposite;
  Input correction;
  NlMovementInputTestAccess::setup(correction, opposite, true, false);
  put(opposite.services.data(), 0x38, -1.0F);
  put(opposite.services.data(), 0x3c, 0.0F);
  NlMovementInputTestAccess::apply(correction, opposite);
  void* first = opposite.step(0);
  check((get<unsigned>(first, 0x10) & 0x18u) == 0x18u &&
            std::fabs(get<float>(first, 0x28) - 2) < .001F,
        "previous back impulse is reconciled once before forward steering");
  const float first_yaw = get<float>(first, 0x34) * k_pi / 180;
  const float actual_forward = -1 + get<float>(first, 0x28);
  const float actual_left = get<float>(first, 0x2c);
  check(std::cos(first_yaw) * actual_forward - std::sin(first_yaw) * actual_left > 0,
        "decoded first slice no longer accelerates against the current velocity");
  check(correction.nl_counter() < .00001F,
        "large input correction is not classified as backward force");
  NlMovementInputTestAccess::apply(correction, opposite);
  check(opposite.count() == 17 && std::fabs(get<float>(opposite.step(0), 0x28) - 2) < .001F,
        "repeated command replaces its owned correction instead of accumulating it");
  put(opposite.base.data(), 0x58, 0.0F);
  put(opposite.cmd.data(), 0x60, std::uint64_t{0});
  NlMovementInputTestAccess::apply(correction, opposite);
  check(opposite.count() == 0, "releasing travel retires owned yaw and analog correction together");
  Fixture crouched;
  Input duck_strafe;
  NlMovementInputTestAccess::setup(duck_strafe, crouched, true, false);
  put(crouched.services.data(), 0x64, 1.0F);
  NlMovementInputTestAccess::apply(duck_strafe, crouched);
  aim::NlAirState crouch_plan;
  crouch_plan.velocity = {300, 0};
  crouch_plan.wish_speed = 85;
  crouch_plan.directional_feedback = true;
  crouch_plan.phase = 0;
  crouch_plan.allow_counter = false;
  crouch_plan.smooth_yaw = true;
  const auto crouch_wish = aim::nl_air_plan(crouch_plan).events[15].world_wish;
  check(game::dot(crouched.final_world(), crouch_wish) > .99999F,
        "duck-adjusted acceleration budget reaches the native command");
  Fixture idle;
  Input idle_assist;
  NlMovementInputTestAccess::setup(idle_assist, idle, true, false);
  put(idle.cmd.data(), 0x60, std::uint64_t{0});
  put(idle.base.data(), 0x58, 0.0F);
  NlMovementInputTestAccess::apply(idle_assist, idle);
  check(idle.count() == 0, "no movement input does not create synthetic travel");
  Fixture starting;
  Input startup;
  NlMovementInputTestAccess::setup(startup, starting, true, false);
  put(starting.pawn.data(), 0x20, game::Vec3{20, 0, 0});
  NlMovementInputTestAccess::apply(startup, starting);
  check(starting.count() > 0 && get<float>(starting.base.data(), 0x58) == 1,
        "air steering stays active below the old 80-unit cutoff without replacing native axes");
  // Original analog transitions are kept byte-for-byte, including mixed buttons.
  Fixture timeline;
  Input preserving;
  NlMovementInputTestAccess::setup(preserving, timeline, true, false);
  put(timeline.base.data(), 0x20, 1);
  put(timeline.steps[0].data(), 0x10, 0x1fu);
  put(timeline.steps[0].data(), 0x18, button::k_forward);
  put(timeline.steps[0].data(), 0x20, std::uint8_t{1});
  put(timeline.steps[0].data(), 0x24, .25F);
  put(timeline.steps[0].data(), 0x28, .4F);
  put(timeline.steps[0].data(), 0x2c, -.2F);
  const auto physical = timeline.steps[0];
  NlMovementInputTestAccess::apply(preserving, timeline);
  check(timeline.steps[0] == physical,
        "physical analog/button message is not rewritten by steering");
  // A rebuilt command may contain different physical intent at the same sequence.
  put(left.cmd.data(), 0x60, button::k_moveright);
  put(left.base.data(), 0x5c, -1.0F);
  NlMovementInputTestAccess::apply(leftward, left);
  check(std::fabs(leftward.strafe_goal() + 90) < .001F,
        "same-command direction change is read afresh instead of cached");
  Fixture ladder;
  Input climb;
  NlMovementInputTestAccess::setup(climb, ladder, true, true);
  put(ladder.pawn.data(), 0x1c, std::uint8_t{9});
  put(ladder.cmd.data(), 0x60, button::k_jump);
  NlMovementInputTestAccess::apply(climb, ladder);
  check(ladder.count() == 0 && (get<std::uint64_t>(ladder.cmd.data(), 0x60) & button::k_jump),
        "ladder input remains untouched");
  std::puts(
      "PASS: subtick adapter, duplicate safety, pointer ownership, silent aim, hop ordering, "
      "impulse reconciliation, rollback and ladder gate");
}
