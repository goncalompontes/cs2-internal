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
//
// The throwing callee lives here too, and that is the point of it: a
// throwing callee in the *same* translation unit is an `invoke` the pass
// leaves alone (the `invoke` rule), so the exception never reaches a
// handler through a thunk. Here the callee is a declaration the policy
// names, the `invoke` rule does not hold, and the call goes through the
// thunk: the exception unwinds through the thunk's frame and reaches the
// caller's handler.

#include <cstdint>

#include "diff_shared.hpp"

// The counter of the first translation unit: the throwing callee counts
// there too, so that the probe of the fixture sees every call. The
// definition is in that file.
extern "C" volatile int cs2_fixture_counter;

extern "C" volatile int cs2_fixture_helper_counter = 0;

extern "C" __attribute__((noinline)) int cs2_fixture_helper(int value) noexcept {
  cs2_fixture_helper_counter += 1;
  return value * 3 + 1;
}

extern "C" __attribute__((noinline)) FixtureBig cs2_fixture_cross(int value) {
  cs2_fixture_helper_counter += 2;
  return FixtureBig{value + 10, value + 20, value + 30};
}

// The same callee, but it does not throw: the call to it inside a catch
// handler is a plain `call` in the funclet, and not an `invoke`.
extern "C" __attribute__((noinline)) FixtureBig cs2_fixture_cross_noexcept(int value) noexcept {
  cs2_fixture_helper_counter += 3;
  return FixtureBig{value + 10, value + 20, value + 30};
}

// A callee that returns an aggregate **and** throws.
//
// The exception is unconditional: the handler of the caller has to be
// reached in every run, whatever the seed is, or the case the fixture
// exists for would not be exercised at all.
extern "C" __attribute__((noinline)) FixtureBig cs2_fixture_throwing(int value) noexcept(false) {
  cs2_fixture_counter += 7;
  throw value;
}
