#pragma once
#include <cstdint>
#include "cs2/game/module.hpp"
namespace cs2::game {
std::uintptr_t unique_code_match(ModuleInfo module, const char* pattern);
}
