// The fixture of the string protection.
//
// It is built twice -- once with the pass and once without -- and the two
// builds have to agree. It is a **DLL**, because the key is derived from the
// instance handle and only the OS loader calls DllMain with the real handle:
// the fixture therefore mirrors the payload, and defines
// `cs2_protect_instance_base` and publishes its own handle in its DllMain.
//
// The strings it uses are the cases that break a naive implementation:
//
//   * a string used twice in one function;
//   * a string used in two functions;
//   * a string used in two blocks of one function, one of them the entry
//     block: the decode has to run before both uses;
//   * a string that is a format argument;
//   * a string compared with `==` against a runtime value;
//   * a string of exactly the minimum length;
//   * a string the policy denies;
//   * a string the policy does not select;
//   * a string whose address is baked into a global initializer -- the case
//     the gate refuses with `escapes_to_initializer`.
//
// Every string is used through a value the compiler cannot fold, so that the
// literal really is in the image: a string that the optimiser folds away
// would make the test pass for the wrong reason.

#include <cstdint>
#include <cstdio>
#include <string_view>

#if defined(_WIN32)
#include <windows.h>
#endif

// The protection pass reads this: the instance handle is the payload's own
// mapped image base -- a value that is not in the file and differs per
// injection.
//
// The definition is here because there must be exactly one of it; the pass
// declares it in every translation unit that decodes a string.
extern "C" std::uint64_t cs2_protect_instance_base = 0;

namespace {

// The refusal case: the address of the literal is baked into a global
// initializer, and a pointer to a decoded buffer cannot exist before the
// program runs.
constexpr std::string_view kGlobal = "escaped";

// A volatile global.
//
// The hash function reads it, so that the compiler cannot merge two calls
// of it: a function that only reads its argument is `readonly`, and two
// such calls with the same argument return the same value -- the optimiser
// would fold the two uses of a literal into one call, and the fixture would
// not have the shape it is here for.
volatile std::uint64_t g_sink = 0;

// The hash of a string, at run time.
//
// It is `noinline` on purpose: an inlined hash of a constant string is
// folded by the optimiser, the literal disappears from the image, and the
// test would pass without protecting anything.
extern "C" __declspec(noinline) auto runtime_hash(const char* text) noexcept -> std::uint64_t {
  std::uint64_t hash = 1469598103934665603ULL ^ g_sink;
  for (const char* cursor = text; *cursor != '\0'; ++cursor) {
    hash ^= static_cast<unsigned char>(*cursor);
    hash *= 1099511628211ULL;
  }
  return hash;
}

// A second function that uses a shared string.
extern "C" __declspec(noinline) auto other_hash() noexcept -> std::uint64_t {
  return runtime_hash("shared");
}

}  // namespace

#if defined(_WIN32)
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    cs2_protect_instance_base = reinterpret_cast<std::uint64_t>(instance);
  }
  return TRUE;
}
#endif

extern "C" __declspec(dllexport) auto probe(const char* text) noexcept -> std::uint64_t {
  std::uint64_t hash = 0;
  // A string used twice in one function, and compared with `==` against a
  // value the compiler does not know.
  if (std::string_view(text) == "healthy") {
    hash ^= runtime_hash("healthy");
  }
  // A string used in two functions.
  hash ^= other_hash();
  // A string that is a format argument.
  char buffer[64] = {};
  std::snprintf(buffer, sizeof(buffer), "%s-%s", "formatted", text);
  hash ^= runtime_hash(buffer);
  // A string of exactly the minimum length.
  hash ^= runtime_hash("four");
  // A string the policy denies, and one the policy does not select: both
  // stay in the image.
  hash ^= runtime_hash("deniedxx");
  hash ^= runtime_hash("untouched");
  // The refused case.
  hash ^= runtime_hash(kGlobal.data());
  return hash;
}

// A literal used in **two blocks** of one function, one of them the entry
// block.
//
// The decode has to run before **both** uses. A decode that is emitted
// after the use of the entry block leaves that use reading an uninitialised
// stack slot: the function returns a *wrong* value, and nothing crashes, so
// nothing else would notice it.
extern "C" __declspec(dllexport) auto probe_blocks(const char* text) noexcept -> std::uint64_t {
  // The use in the entry block.
  std::uint64_t hash = runtime_hash("twoblok");
  if (text[0] == 'x') {
    // The use in the other block.
    hash ^= runtime_hash("twoblok");
  }
  return hash;
}
