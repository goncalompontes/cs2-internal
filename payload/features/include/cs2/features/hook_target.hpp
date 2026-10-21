#pragma once

// The guard that decides whether a function entry may be **patched**.
//
// **Why this exists.** A hard-coded RVA is not a target: it is a guess that
// happens to be right on one build. The chams' draw hook paid for that lesson
// twice — the first address came from an MSVC RTTI *lambda* type (a lambda's
// `operator()` is static, so it has no `this` and the argument list was shifted
// by one), and the second was a stale RVA that only asked "is the address
// inside the module?". Patching the wrong bytes in the renderer's hot path is
// an access violation in the **game**, not in us.
//
// So the target is resolved through a **dumper signature** (ADR-0012: no
// hard-coded RVA) and the bytes at the resolved address are **read and
// compared** before anything is patched. The comparison is the rule; this
// header is the rule, kept free of the engine and of the platform so a host
// test can drive it.
//
// **The rule**, in order:
//
//   1. the offset must be present and belong to the module that is about to be
//      patched (a missing offset means the calibration dump is stale — the
//      signature was never resolved);
//   2. the module must be loaded (the game is not up yet);
//   3. the resolved address must lie inside the module image, with room for
//      the whole prologue;
//   4. the prologue must have been read, in full;
//   5. the prologue must equal the expected bytes.
//
// Only then may the caller patch the entry.
//
// **Reuse.** The third-person camera's hook has the same shape of problem (a
// stale target in the renderer's hot path), so the guard lives in its own
// header rather than inside the chams' source: any feature that patches a
// function entry can use it.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cs2::features::hook_target {

// The number of bytes the guard compares.
inline constexpr std::size_t PROLOGUE_SIZE = 20;

// The bytes the guard compares.
using Prologue = std::array<std::uint8_t, PROLOGUE_SIZE>;

// The prologue of `CAnimatableSceneObjectDesc::DrawPrimitive`.
//
// Provenance (scenesystem.dll sha256 040eb851…, image base 0x180000000): the
// function is at RVA 0x580a0 and starts with
//
//   4C 89 4C 24 20   MOV qword ptr [RSP + 0x20], R9
//   48 89 54 24 10   MOV qword ptr [RSP + 0x10], RDX
//   55               PUSH RBP
//   53               PUSH RBX
//   56               PUSH RSI
//   57               PUSH RDI
//   41 54            PUSH R12
//   41 55            PUSH R13
//   41 57            PUSH R15
//
// The first 14 bytes carry no RIP-relative operand and no relative branch, so
// the repo's inline hook (a 14-byte jump) can displace them. The 20-byte
// prologue is unique in the module — `docs/re/chams.md` §8 records the
// measurement.
inline constexpr Prologue SCENE_DRAW_PROLOGUE = {
    0x4C, 0x89, 0x4C, 0x24, 0x20, 0x48, 0x89, 0x54, 0x24, 0x10,
    0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x57,
};

// Why the hook is (or is not) installed.
enum class Status : std::uint8_t {
  // The feature is off: nothing is patched.
  idle,
  // The feature is on and the install is being retried.
  pending,
  // Every check passed: the target may be patched.
  accepted,
  // The hook is in place.
  installed,
  // The calibration dump has no usable offset for the target.
  no_offset,
  // The module is not loaded (the game is not up yet).
  no_module,
  // The resolved address is not inside the module image.
  outside_module,
  // The prologue could not be read (or only in part).
  unreadable,
  // The prologue is not the expected one: the module is not the build the
  // measurement was taken on.
  mismatch,
  // The hook engine refused the patch.
  refused,
};

// A one-line description of a status, for the log and the overlay.
[[nodiscard]] constexpr auto describe(Status status) noexcept -> std::string_view {
  switch (status) {
    case Status::idle:
      return "off";
    case Status::pending:
      return "installing";
    case Status::accepted:
      return "accepted";
    case Status::installed:
      return "on";
    case Status::no_offset:
      return "no offset (the calibration dump has no scene_draw_primitive)";
    case Status::no_module:
      return "no module (scenesystem.dll is not loaded)";
    case Status::outside_module:
      return "outside the module";
    case Status::unreadable:
      return "unreadable prologue";
    case Status::mismatch:
      return "prologue mismatch (not the build the RE measured)";
    case Status::refused:
      return "the hook engine refused the patch";
  }
  return "unknown";
}

// The facts the decision is made from.
//
// The engine reads happen in the caller (the payload); the decision itself is
// pure, so the host test can drive it with any bytes it likes.
struct Facts {
  // The dumper's offset for the target: present, and its RVA.
  bool offset_present = false;
  std::uint32_t rva = 0;
  // The module the offset belongs to.
  std::string_view offset_module{};
  // The module the target lives in.
  bool module_present = false;
  std::uintptr_t module_base = 0;
  std::size_t module_size = 0;
  // The bytes read at the resolved address.
  bool prologue_read = false;
  std::size_t prologue_length = 0;
  Prologue prologue{};
};

// The decision.
//
// `expected` is the prologue the module is known to carry, and `expected_module`
// the module the offset has to belong to. Returns `Status::accepted` when the
// target may be patched — the caller still has to install the hook, and
// reports `Status::installed` or `Status::refused`.
[[nodiscard]] constexpr auto decide(const Facts& facts, const Prologue& expected,
                                    std::string_view expected_module) noexcept -> Status {
  // 1. The offset: it must be there, it must not be the module's base (RVA 0
  //    is never a function entry) and it must belong to the module that is
  //    about to be patched.
  if (!facts.offset_present || facts.rva == 0) {
    return Status::no_offset;
  }
  if (!facts.offset_module.empty() && facts.offset_module != expected_module) {
    return Status::no_offset;
  }
  // 2. The module: the game may not be up yet.
  if (!facts.module_present || facts.module_base == 0) {
    return Status::no_module;
  }
  // 3. The address: inside the module image, with room for the prologue.
  const auto target = facts.module_base + static_cast<std::uintptr_t>(facts.rva);
  if (facts.module_size == 0 || target < facts.module_base ||
      target + PROLOGUE_SIZE > facts.module_base + facts.module_size) {
    return Status::outside_module;
  }
  // 4. The bytes: read, and read in full.
  if (!facts.prologue_read || facts.prologue_length < PROLOGUE_SIZE) {
    return Status::unreadable;
  }
  // 5. The bytes: the expected ones.
  for (std::size_t index = 0; index < PROLOGUE_SIZE; ++index) {
    if (facts.prologue[index] != expected[index]) {
      return Status::mismatch;
    }
  }
  return Status::accepted;
}

}  // namespace cs2::features::hook_target
