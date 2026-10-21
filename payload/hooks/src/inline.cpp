#include "cs2/hooks/inline.hpp"

#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

#include "cs2/hooks/memory.hpp"

namespace cs2::hooks {
namespace {

// A register-free absolute jump: `jmp qword ptr [rip+0]` followed by the
// 8-byte destination. Using `mov rax, imm64; jmp rax` would clobber RAX, which
// corrupts the copied prologue's live registers (and, on the target patch, the
// MSVC x64 variadic convention that carries the SSE arg count in AL).
constexpr std::size_t k_jmp_size = 14;  // jmp [rip+0] ; .quad destination

void write_abs_jmp(unsigned char* out, std::uintptr_t destination) noexcept {
  out[0] = 0xFF;
  out[1] = 0x25;
  out[2] = 0x00;
  out[3] = 0x00;
  out[4] = 0x00;
  out[5] = 0x00;
  std::memcpy(out + 6, &destination, sizeof(destination));
}

struct Decoded {
  std::size_t length = 0;
  std::size_t rip_disp = static_cast<std::size_t>(-1);  // offset of a RIP disp32
  bool relative = false;                                // relative branch
};

auto parse_modrm(const unsigned char* code, std::size_t& pos, std::size_t max,
                 std::size_t& rip_disp) -> bool {
  if (pos >= max) {
    return false;
  }
  const unsigned char modrm = code[pos++];
  const unsigned char mod = modrm >> 6;
  const unsigned char rm = modrm & 7;
  if (mod == 3) {
    return true;
  }
  if (rm == 4) {  // SIB
    if (pos >= max) {
      return false;
    }
    const unsigned char sib = code[pos++];
    if (mod == 0 && (sib & 7) == 5) {
      if (pos + 4 > max) {
        return false;
      }
      pos += 4;
    }
  } else if (mod == 0 && rm == 5) {  // RIP-relative
    rip_disp = pos;
    if (pos + 4 > max) {
      return false;
    }
    pos += 4;
  }
  if (mod == 1) {
    if (pos + 1 > max) {
      return false;
    }
    pos += 1;
  } else if (mod == 2) {
    if (pos + 4 > max) {
      return false;
    }
    pos += 4;
  }
  return true;
}

// A focused x64 length decoder: enough for typical function prologues.
auto decode_one(const unsigned char* code, std::size_t max) -> std::optional<Decoded> {
  std::size_t pos = 0;
  bool opsize16 = false;
  bool rex_w = false;

  while (pos < max) {
    const unsigned char b = code[pos];
    if (b == 0xF0 || b == 0xF2 || b == 0xF3 || b == 0x2E || b == 0x36 || b == 0x3E || b == 0x26 ||
        b == 0x64 || b == 0x65) {
      ++pos;
      continue;
    }
    if (b == 0x66) {
      opsize16 = true;
      ++pos;
      continue;
    }
    if (b == 0x67) {
      ++pos;
      continue;
    }
    break;
  }
  if (pos < max && code[pos] >= 0x40 && code[pos] <= 0x4F) {
    rex_w = (code[pos] & 0x08) != 0;
    ++pos;
  }
  if (pos >= max) {
    return std::nullopt;
  }

  const unsigned char op = code[pos++];
  Decoded decoded;
  auto rip = static_cast<std::size_t>(-1);
  const auto finish = [&]() -> std::optional<Decoded> {
    if (pos > max) {
      return std::nullopt;
    }
    decoded.length = pos;
    decoded.rip_disp = rip;
    return decoded;
  };
  const auto imm_size = [&]() -> std::size_t { return opsize16 ? 2 : 4; };

  if (op == 0x0F) {
    if (pos >= max) {
      return std::nullopt;
    }
    const unsigned char op2 = code[pos++];
    if (op2 == 0x05 || op2 == 0x0B || op2 == 0x0E || op2 == 0x77 || op2 == 0x31 || op2 == 0xA2) {
      return finish();  // syscall / ud2 / femms / emms / rdtsc / cpuid
    }
    if (op2 >= 0x80 && op2 <= 0x8F) {  // jcc rel32
      if (pos + 4 > max) {
        return std::nullopt;
      }
      pos += 4;
      decoded.relative = true;
      return finish();
    }
    if ((op2 >= 0x10 && op2 <= 0x17) || (op2 >= 0x28 && op2 <= 0x2F) ||
        (op2 >= 0x40 && op2 <= 0x4F) || (op2 >= 0x50 && op2 <= 0x7F) ||
        (op2 >= 0x90 && op2 <= 0x9F) || op2 == 0x1E || op2 == 0x1F || op2 == 0x6E || op2 == 0x6F ||
        op2 == 0x7E || op2 == 0x7F || op2 == 0xAF || op2 == 0xB6 || op2 == 0xB7 || op2 == 0xBE ||
        op2 == 0xBF || op2 == 0xC0 || op2 == 0xC1 || op2 == 0xD6 || op2 == 0xEF) {
      if (!parse_modrm(code, pos, max, rip)) {
        return std::nullopt;
      }
      return finish();
    }
    return std::nullopt;
  }

  switch (op) {
    case 0x90: case 0xC3: case 0xC9: case 0xCC: case 0xF1: case 0xF4: case 0xF5:
    case 0xF8: case 0xF9: case 0xFA: case 0xFB: case 0xFC: case 0xFD:
    case 0x98: case 0x99: case 0x9C: case 0x9D: case 0x9E: case 0x9F:
    case 0xA4: case 0xA5: case 0xA6: case 0xA7: case 0xAA: case 0xAB:
    case 0xAC: case 0xAD: case 0xAE: case 0xAF:
    case 0xEC: case 0xED: case 0xEE: case 0xEF:
      return finish();
    default:
      break;
  }
  if (op >= 0x50 && op <= 0x5F) {
    return finish();  // push/pop
  }
  if (op >= 0x91 && op <= 0x97) {
    return finish();  // xchg eax, reg
  }
  if (op >= 0x70 && op <= 0x7F) {                 // jcc rel8
    if (pos + 1 > max) {
      return std::nullopt;
    }
    pos += 1;
    decoded.relative = true;
    return finish();
  }
  if (op >= 0xB0 && op <= 0xB7) {  // mov r8, imm8
    if (pos + 1 > max) {
      return std::nullopt;
    }
    pos += 1;
    return finish();
  }
  if (op >= 0xB8 && op <= 0xBF) {  // mov r, imm
    const std::size_t imm = rex_w ? 8 : imm_size();
    if (pos + imm > max) {
      return std::nullopt;
    }
    pos += imm;
    return finish();
  }
  if (op >= 0xD8 && op <= 0xDF) {  // x87
    if (!parse_modrm(code, pos, max, rip)) {
      return std::nullopt;
    }
    return finish();
  }
  if (op >= 0xE0 && op <= 0xE3) {  // loop/jcxz rel8
    if (pos + 1 > max) {
      return std::nullopt;
    }
    pos += 1;
    decoded.relative = true;
    return finish();
  }

  switch (op) {
    case 0x6A:
      if (pos + 1 > max) {
        return std::nullopt;
      }
      pos += 1;
      return finish();
    case 0x68: {
      const std::size_t imm = opsize16 ? 2 : 4;
      if (pos + imm > max) {
        return std::nullopt;
      }
      pos += imm;
      return finish();
    }
    case 0xE8: case 0xE9:
      if (pos + 4 > max) {
        return std::nullopt;
      }
      pos += 4;
      decoded.relative = true;
      return finish();
    case 0xEB:
      if (pos + 1 > max) {
        return std::nullopt;
      }
      pos += 1;
      decoded.relative = true;
      return finish();
    case 0xC2: case 0xCA:
      if (pos + 2 > max) {
        return std::nullopt;
      }
      pos += 2;
      return finish();
    case 0xCD:
      if (pos + 1 > max) {
        return std::nullopt;
      }
      pos += 1;
      return finish();
    case 0x04: case 0x0C: case 0x14: case 0x1C: case 0x24: case 0x2C: case 0x34: case 0x3C:
    case 0xA8:
      if (pos + 1 > max) {
        return std::nullopt;
      }
      pos += 1;
      return finish();
    case 0x05: case 0x0D: case 0x15: case 0x1D: case 0x25: case 0x2D: case 0x35: case 0x3D:
    case 0xA9: {
      const std::size_t imm = imm_size();
      if (pos + imm > max) {
        return std::nullopt;
      }
      pos += imm;
      return finish();
    }
    case 0x80: case 0x83: case 0xC0: case 0xC1: case 0x6B:
      if (!parse_modrm(code, pos, max, rip)) {
        return std::nullopt;
      }
      if (pos + 1 > max) {
        return std::nullopt;
      }
      pos += 1;
      return finish();
    case 0x81: case 0x69: case 0xC7: {
      if (!parse_modrm(code, pos, max, rip)) {
        return std::nullopt;
      }
      const std::size_t imm = imm_size();
      if (pos + imm > max) {
        return std::nullopt;
      }
      pos += imm;
      return finish();
    }
    case 0xC6:
      if (!parse_modrm(code, pos, max, rip)) {
        return std::nullopt;
      }
      if (pos + 1 > max) {
        return std::nullopt;
      }
      pos += 1;
      return finish();
    case 0xF6: {
      if (pos >= max) {
        return std::nullopt;
      }
      const unsigned char reg = (code[pos] >> 3) & 7;
      if (!parse_modrm(code, pos, max, rip)) {
        return std::nullopt;
      }
      if (reg == 0) {
        if (pos + 1 > max) {
          return std::nullopt;
        }
        pos += 1;
      }
      return finish();
    }
    case 0xF7: {
      if (pos >= max) {
        return std::nullopt;
      }
      const unsigned char reg = (code[pos] >> 3) & 7;
      if (!parse_modrm(code, pos, max, rip)) {
        return std::nullopt;
      }
      if (reg == 0) {
        const std::size_t imm = imm_size();
        if (pos + imm > max) {
          return std::nullopt;
        }
        pos += imm;
      }
      return finish();
    }
    default:
      break;
  }

  // Default ModRM forms: arithmetic (00-3F), mov/lea (84-8F), shifts (D0-D3),
  // group5 (FE/FF), movsxd (63).
  if (op <= 0x3F || (op >= 0x84 && op <= 0x8F) || op == 0x63 || (op >= 0xD0 && op <= 0xD3) ||
      op == 0xFE || op == 0xFF) {
    if (!parse_modrm(code, pos, max, rip)) {
      return std::nullopt;
    }
    return finish();
  }
  return std::nullopt;
}

}  // namespace

auto to_string(InlineError error) noexcept -> const char* {
  switch (error) {
    case InlineError::too_short: return "too_short";
    case InlineError::unsupported_instruction: return "unsupported_instruction";
    case InlineError::allocation_failed: return "allocation_failed";
    case InlineError::protect_failed: return "protect_failed";
  }
  return "unknown";
}

InlineHook::~InlineHook() {
  uninstall();
  // The retired trampolines are released here, and only here: their pages
  // have to stay mapped for as long as any detour could still hold the
  // pointer, and the hook's own destruction is the point where no detour can
  // (the feature is being torn down).
  for (const Retired& retired : retired_) {
    free_executable(retired.address, retired.size);
  }
  retired_.clear();
}

auto InlineHook::install(void* target, void* detour) noexcept -> std::optional<InlineError> {
  if (installed_.load(std::memory_order_acquire) || target == nullptr || detour == nullptr) {
    return InlineError::unsupported_instruction;
  }
  auto* code = static_cast<unsigned char*>(target);

  std::size_t total = 0;
  std::vector<std::size_t> rip_disps;
  while (total < k_jmp_size) {
    auto decoded = decode_one(code + total, 16);
    if (!decoded || decoded->relative) {
      return InlineError::unsupported_instruction;
    }
    if (decoded->rip_disp != static_cast<std::size_t>(-1)) {
      rip_disps.push_back(total + decoded->rip_disp);
    }
    total += decoded->length;
    if (total > 16) {
      return InlineError::unsupported_instruction;
    }
  }

  const std::size_t trampoline_size = total + k_jmp_size;
  auto* trampoline = static_cast<unsigned char*>(allocate_executable(trampoline_size));
  if (trampoline == nullptr) {
    return InlineError::allocation_failed;
  }
  std::memcpy(trampoline, code, total);

  // Relocate RIP-relative displacements (constant delta target - trampoline).
  const std::intptr_t delta =
      reinterpret_cast<std::intptr_t>(target) - reinterpret_cast<std::intptr_t>(trampoline);
  for (const std::size_t disp_off : rip_disps) {
    std::int32_t disp = 0;
    std::memcpy(&disp, trampoline + disp_off, sizeof(disp));
    const std::int64_t fixed = static_cast<std::int64_t>(disp) + delta;
    if (fixed < INT32_MIN || fixed > INT32_MAX) {
      free_executable(trampoline, trampoline_size);
      return InlineError::unsupported_instruction;
    }
    const auto relocated = static_cast<std::int32_t>(fixed);
    std::memcpy(trampoline + disp_off, &relocated, sizeof(relocated));
  }
  write_abs_jmp(trampoline + total, reinterpret_cast<std::uintptr_t>(code + total));

  std::memcpy(saved_, code, total);

  unsigned char patch[16];
  write_abs_jmp(patch, reinterpret_cast<std::uintptr_t>(detour));
  std::memset(patch + k_jmp_size, 0x90, total - k_jmp_size);
  if (!make_writable(target, total)) {
    free_executable(trampoline, trampoline_size);
    return InlineError::protect_failed;
  }
  // The trampoline is **published before the patch**: a detour that runs in the
  // window between the patch and the publication would find `original() ==
  // nullptr` and drop the call (the chams' draw hook returns without drawing
  // when there is no trampoline, so a primitive drawn in that window would be
  // silently lost). The trampoline is complete at this point, so publishing it
  // early is safe; `installed_` still comes after the patch, so the hook cannot
  // be uninstalled while it is being written.
  //
  // The publication is a **release** store: the trampoline's bytes are in
  // place before the pointer that finds them is visible, and the reader
  // (the game's render threads) reads it with an **acquire** load.
  target_ = target;
  trampoline_size_ = trampoline_size;
  patched_size_ = total;
  trampoline_.store(trampoline, std::memory_order_release);
  std::memcpy(code, patch, total);
  installed_.store(true, std::memory_order_release);
  return std::nullopt;
}

void InlineHook::uninstall() noexcept {
  if (!installed_.load(std::memory_order_acquire)) {
    return;
  }
  // The teardown order, and the reason for it:
  //
  //   1. `installed_` is cleared (release): no new caller proceeds on the
  //      word that says the hook is in place.
  //   2. `trampoline_` is set to `nullptr` (release): the null is
  //      **published before** the trampoline's memory is released, so a
  //      detour that enters from here on finds `original() == nullptr` and
  //      drops the call instead of calling a trampoline that is going away.
  //   3. the target is un-patched: no new detour enters at all.
  //   4. the trampoline is **retired**, not freed: a detour that was already
  //      inside when the uninstall happened may still hold the pointer and
  //      call it, and the pages it runs on have to stay mapped until the
  //      hook is destroyed. The old order freed the trampoline **before** it
  //      nulled the pointer, which is a use-after-free on freed executable
  //      memory.
  installed_.store(false, std::memory_order_release);
  // The exchange publishes the null **and** hands the pointer over: the
  // trampoline's pages stay mapped (see below), so the pointer is only kept,
  // not released.
  void* const trampoline = trampoline_.exchange(nullptr, std::memory_order_release);
  std::memcpy(target_, saved_, patched_size_);
  retired_.push_back(Retired{trampoline, trampoline_size_});
}

}  // namespace cs2::hooks
