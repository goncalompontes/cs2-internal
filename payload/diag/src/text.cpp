#include "cs2/diag/text.hpp"

#include "cs2/game/module.hpp"

namespace cs2::diag {
namespace {

struct ModuleRange {
  const char* name;
  std::uintptr_t base;
  std::uintptr_t end;
};

constexpr int k_max_modules = 8;
ModuleRange g_modules[k_max_modules];
int g_module_count = 0;

}  // namespace

void capture_modules() noexcept {
  g_module_count = 0;
  const auto add = [](const char* name, const char* file) {
    if (g_module_count >= k_max_modules) {
      return;
    }
    const cs2::game::ModuleInfo info = cs2::game::find_module(file);
    if (!info.valid() || info.size == 0) {
      return;
    }
    g_modules[g_module_count++] = {name, info.base, info.base + info.size};
  };
  add("payload", "payload.dll");
  add("client", "client.dll");
  add("engine", "engine2.dll");
  add("cs2", "cs2.exe");
}

TextBuffer& TextBuffer::operator+=(std::string_view text) noexcept {
  for (const char character : text) {
    if (size_ + 1 >= capacity_) {
      break;
    }
    data_[size_++] = character;
  }
  data_[size_] = '\0';
  return *this;
}

TextBuffer& TextBuffer::operator+=(char character) noexcept {
  if (size_ + 1 < capacity_) {
    data_[size_++] = character;
    data_[size_] = '\0';
  }
  return *this;
}

TextBuffer& TextBuffer::hex(std::uintptr_t value) noexcept {
  if (value == 0) {
    return *this += '0';
  }
  char digits[16];
  std::size_t count = 0;
  while (value != 0) {
    const auto nibble = static_cast<unsigned>(value & 0xF);
    digits[count++] = static_cast<char>(nibble < 10 ? '0' + nibble : 'a' + nibble - 10);
    value >>= 4;
  }
  while (count != 0) {
    *this += digits[--count];
  }
  return *this;
}

TextBuffer& TextBuffer::dec(unsigned long long value) noexcept {
  if (value == 0) {
    return *this += '0';
  }
  char digits[20];
  std::size_t count = 0;
  while (value != 0) {
    digits[count++] = static_cast<char>('0' + (value % 10));
    value /= 10;
  }
  while (count != 0) {
    *this += digits[--count];
  }
  return *this;
}

TextBuffer& TextBuffer::address(std::uintptr_t value) noexcept {
  for (int i = 0; i < g_module_count; ++i) {
    if (value >= g_modules[i].base && value < g_modules[i].end) {
      *this += g_modules[i].name;
      *this += "+0x";
      return hex(value - g_modules[i].base);
    }
  }
  *this += "0x";
  return hex(value);
}

}  // namespace cs2::diag
