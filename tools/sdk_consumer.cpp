// Host-side consumer of the generated SDK headers.
//
// Proves the generated `payload/sdk` headers are usable from a normal C++
// translation unit and that a build depending on the generator sees them.
// Tolerates empty/missing dumps (fresh checkouts) by not asserting on data.

#include <cstdio>

#if __has_include("interfaces.hpp")
#include "interfaces.hpp"
#define CS2_HAVE_INTERFACES 1
#endif

#if __has_include("schemas.hpp")
#include "schemas.hpp"
#define CS2_HAVE_SCHEMAS 1
#endif

int main() {
#ifdef CS2_HAVE_INTERFACES
  std::printf("sdk-consumer: %zu interface(s)\n", cs2::sdk::INTERFACES.size());
  if (!cs2::sdk::INTERFACES.empty() && cs2::sdk::find_interface_by_target("Source2Client002") == nullptr) {
    std::puts("sdk-consumer: expected Source2Client002");
    return 1;
  }
#else
  std::puts("sdk-consumer: no interfaces.hpp (no dumps yet)");
#endif

#if defined(CS2_HAVE_SCHEMAS) && __has_include("schemas/client_dll.hpp")
  const auto* entity = cs2::sdk::schemas::client_dll::find_class_by_target("C_BaseEntity");
  if (entity != nullptr) {
    const auto health = cs2::sdk::schemas::client_dll::field_offset_by_target("C_BaseEntity", "m_iHealth");
    std::printf("sdk-consumer: C_BaseEntity size=0x%x fields=%zu m_iHealth=0x%x\n",
                entity->size, entity->fields.size(), health);
    if (health != 0x34c) {
      std::puts("sdk-consumer: unexpected m_iHealth offset");
      return 2;
    }
  }
#endif
  return 0;
}
