// The fixture of the virtualizer's front end, in the production path.
//
// The production path is the one the payload build uses: the compiler
// loads the plugin and the extension points fire. The test compiles this
// file twice -- once with the plugin and once without -- and compares the
// body of the selected function: the analysis is read-only, so the body
// has to come out the way it went in.
//
// The function is a leaf on purpose. The call encryption (T19) rewrites
// the calls *of* a selected function, and the comparison is about the
// analysis; a selected function that made a call would change its body
// because of the *other* task.

extern "C" __declspec(noinline) int vm_leaf(int value) {
  const int sum = value + 1;
  return sum * 3;
}
