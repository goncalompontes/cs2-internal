// The second translation unit of the differential test.
//
// The functions live here, and the selected function of the other file calls
// them. At the IR level those calls are *declarations* -- the same thing as
// an import -- and the policy's `cross_module` list is what tells the pass
// that the payload's own code.
//
// The pass routes the calls through the caller's own table, as a relocatable
// pointer to this symbol. A dispatch that reaches the *wrong* function does
// not crash: it computes something else, and the differential test is what
// catches it.
//
// One of the callees returns an aggregate: the call carries an `sret`
// parameter, and the hidden pointer has to reach this function unchanged --
// the struct this function writes is the struct the caller reads back.

#include <cstdint>

#include "diff_shared.hpp"

extern "C" volatile int cs2_fixture_helper_counter = 0;

extern "C" __attribute__((noinline)) int cs2_fixture_helper(int value) noexcept {
  cs2_fixture_helper_counter += 1;
  return value * 3 + 1;
}

extern "C" __attribute__((noinline)) FixtureBig cs2_fixture_cross(int value) {
  cs2_fixture_helper_counter += 2;
  return FixtureBig{value + 10, value + 20, value + 30};
}
