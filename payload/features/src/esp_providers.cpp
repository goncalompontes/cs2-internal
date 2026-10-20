#include "cs2/features/esp_cache.hpp"

namespace cs2::features {
namespace {

// The registry's value.
//
// A function-local static, not a namespace-scope global: it is constant
// initialised (so the mapped image never runs the CRT's static-init guard)
// and trivially destructible (so no atexit handler is registered). See the
// ADR-0010 note on `esp_providers` (`cs2/features/esp_cache.hpp`).
auto registry() noexcept -> EspProviders& {
  static EspProviders providers{};
  return providers;
}

}  // namespace

auto esp_providers() noexcept -> EspProviders {
  return registry();
}

void set_esp_providers(const EspProviders& providers) noexcept {
  registry() = providers;
}

}  // namespace cs2::features
