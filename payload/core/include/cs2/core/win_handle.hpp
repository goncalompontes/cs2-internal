#pragma once

// The Windows policy for the RAII handle (cs2/core/handle.hpp): how to
// recognise a live Win32 kernel handle and how to release one.
//
// The handle is stored as an opaque `void*` so that the headers that *own* a
// handle (cs2/control/channel.hpp) do not have to include <windows.h>: the
// platform-independent core must not (docs/style-guide.md §3).
//
// One policy covers every kernel handle the tree holds -- files, pipes,
// threads and file mappings -- because they all live in the same handle table
// and are all released with `CloseHandle`.

#if !defined(_WIN32)
#error "cs2/core/win_handle.hpp is Windows-only"
#endif

#include <windows.h>

#include "cs2/core/handle.hpp"

namespace cs2::core {

// A Win32 kernel handle, stored opaquely.
using KernelHandle = void*;

struct KernelHandlePolicy {
  // `CreateFileA` reports failure with INVALID_HANDLE_VALUE and the other
  // creators with NULL, so neither is a handle.
  bool is_valid(KernelHandle handle) const noexcept {
    return handle != nullptr && handle != INVALID_HANDLE_VALUE;
  }
  void close(KernelHandle handle) const noexcept {
    CloseHandle(static_cast<HANDLE>(handle));
  }
};

// A kernel handle owned by the wrapper: the common case in this tree (files,
// pipes, threads and file mappings).
using ScopedKernelHandle = ScopedHandle<KernelHandle, KernelHandlePolicy>;

}  // namespace cs2::core
