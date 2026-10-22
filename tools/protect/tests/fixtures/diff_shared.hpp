// The aggregates the fixture returns by value.
//
// The struct is **shared**: the caller and the callee are in two
// translation units, and the layout of the aggregate is part of the call.
// The two files have to agree on it, byte for byte, or the callee writes
// the struct somewhere else than the caller reads it.

#pragma once

#include <cstdint>

// A big aggregate: bigger than a register, so the ABI returns it through a
// hidden pointer (an `sret` parameter).
struct FixtureBig {
  std::int64_t a;
  std::int64_t b;
  std::int64_t c;
};

// A small aggregate: at most 16 bytes, so the ABI returns it in registers.
struct FixtureSmall {
  std::int32_t a;
  float b;
};

// An aggregate with **mixed members**: an integer, a float and a pointer, so
// the copy back is not a memcpy of something uniform.
struct FixtureMixed {
  std::int64_t a;
  float b;
  void* c;
  std::int64_t d;
};
