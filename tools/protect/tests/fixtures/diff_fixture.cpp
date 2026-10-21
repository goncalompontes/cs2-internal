// The fixture of the differential test.
//
// The protection pass rewrites the calls of the selected function into
// indirect dispatches through an encrypted descriptor. A mistake does not
// crash: it calls the *wrong* function, so the fixture has to compute
// something that a wrong call changes.
//
// The program is built twice from this file -- once with the pass and once
// without -- and the two runs have to agree on their output and on their
// exit code.

#include <cstdio>

// The counter makes every call observable: the compiler cannot fold a call
// that writes to it, so the calls survive the optimiser.
extern "C" volatile int cs2_fixture_counter = 0;

extern "C" __attribute__((noinline)) int cs2_fixture_add(int a, int b) noexcept {
  cs2_fixture_counter += 1;
  return a + b;
}

extern "C" __attribute__((noinline)) int cs2_fixture_mul(int a, int b) noexcept {
  cs2_fixture_counter += 2;
  return a * b;
}

// A variadic callee: the pass must skip it (the indirect call cannot
// reproduce the variadic ABI), and it still has to work.
extern "C" __attribute__((noinline)) int cs2_fixture_variadic(int count, ...) {
  cs2_fixture_counter += 3;
  return count;
}

// A callee that returns a big aggregate: the call carries an `sret`
// parameter, so the pass must skip it.
struct Big {
  long a;
  long b;
  long c;
};

extern "C" __attribute__((noinline)) Big cs2_fixture_big(int value) {
  cs2_fixture_counter += 4;
  return Big{value, value + 1, value + 2};
}

// The callee of a tail call: the pass must skip a `musttail` call.
extern "C" __attribute__((noinline)) int cs2_fixture_tail(int value) {
  cs2_fixture_counter += 5;
  return value;
}

// A call that may throw: it becomes an `invoke` in the caller, and the
// pass must skip it.
extern "C" void cs2_fixture_may_throw() {
  cs2_fixture_counter += 6;
}

// The selected function. The pass rewrites the calls to cs2_fixture_add and
// cs2_fixture_mul (both are defined in this file) and leaves the variadic,
// the sret, the musttail and the invoke alone.
extern "C" __attribute__((noinline)) int cs2_fixture_caller(int seed) {
  int total = seed;
  try {
    for (int i = 0; i < 8; ++i) {
      total = cs2_fixture_add(total, seed + i);
      total = cs2_fixture_mul(total, 3);
    }
    cs2_fixture_may_throw();
  } catch (...) {
    return -1;
  }
  const Big big = cs2_fixture_big(total);
  total = cs2_fixture_variadic(total, 1, 2, 3);
  total += static_cast<int>(big.a + big.b + big.c);
  [[clang::musttail]] return cs2_fixture_tail(total);
}

int main() {
  const int value = cs2_fixture_caller(5);
  std::printf("value=%d counter=%d\n", value, cs2_fixture_counter);
  return value & 0x7F;
}
