// A benign Windows process to inject into during injector tests.
#include <windows.h>

int main() {
  for (;;) {
    Sleep(1000);
  }
}
