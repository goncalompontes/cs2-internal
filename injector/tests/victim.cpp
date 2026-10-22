// A benign Windows process to inject into during injector tests.
#include <windows.h>

auto main() -> int {
  for (;;) {
    Sleep(1000);
  }
}
