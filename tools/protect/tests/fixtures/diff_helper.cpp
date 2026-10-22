// The second translation unit of the differential test.
//
// The function lives here, and the selected function of the other file calls
// it. At the IR level that call is a *declaration* -- the same thing as an
// import -- and the policy's `cross_module` list is what tells the pass
// that it is the payload's own code.
//
// The pass routes the call through the caller's own table, as a relocatable
// pointer to this symbol. A dispatch that reaches the *wrong* function does
// not crash: it computes something else, and the differential test is what
// catches it.

extern "C" volatile int cs2_fixture_helper_counter = 0;

extern "C" __attribute__((noinline)) int cs2_fixture_helper(int value) noexcept {
  cs2_fixture_helper_counter += 1;
  return value * 3 + 1;
}
