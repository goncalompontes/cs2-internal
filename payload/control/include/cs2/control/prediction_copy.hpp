#pragma once

// The prediction's copy of the engine's movement services — and why it is a
// plain local, never a `thread_local`.
//
// ## The defect this header exists to prevent
//
// The payload is **manually mapped**: the Windows loader never loads it, so it
// never allocates the per-thread TLS block a `thread_local` resolves against.
// The injector reserves a slot in the target (`TlsAlloc`) and writes the index
// into the image's `__tls_index`; the entry stub then installs the per-thread
// block **on the entry thread only**. The game's own threads therefore
// resolve the image's `thread_local` against a block pointer that was never
// written — the address is null (or garbage), and the copy that follows fails
// **silently**: the fault-safe reader answers "false" and the prediction is
// simply off, with no error anywhere. `cs2::tls::ensure_thread_tls()` cannot
// repair it either: it refuses an index that is not below 64, and the live
// build's index is 154.
//
// The copy is therefore **the caller's buffer**: a plain local of the
// prediction's step, handed to the helpers below. It lives in the caller's
// frame, so it needs no per-thread storage, no lock and no global state, and
// two threads can run the prediction at the same time without touching each
// other's copy.
//
// See `docs/re/prediction.md` §"the TLS defect" for the evidence.

#include <array>
#include <cstddef>
#include <cstdint>

namespace cs2::control::prediction {

// `CCSPlayer_MovementServices` — the object the engine's `RunCommand` mutates
// and the prediction restores (`docs/re/prediction.md`).
inline constexpr std::size_t MOVEMENT_SERVICES_SIZE = 0xfe0;

// The snapshot's type: a plain byte array. It is a **local** of the
// prediction's step — never a `thread_local`, never a global.
using MovementServicesSnapshot = std::array<std::byte, MOVEMENT_SERVICES_SIZE>;

// The reader: copies `count` bytes out of the engine's object. False when the
// address cannot be read — the payload's fault-safe `cs2::game::read_bytes`.
using SnapshotReader = bool (*)(std::uintptr_t address, void* out, std::size_t count) noexcept;

// The writer: puts the snapshot back into the engine's object.
using SnapshotWriter = void (*)(std::uintptr_t address, const void* data,
                                std::size_t count) noexcept;

// Take the snapshot of the engine's movement services into `snapshot`.
//
// False when the object could not be read. The caller must then leave the
// engine's object alone: nothing was changed, so there is nothing to restore.
[[nodiscard]] inline auto snapshot(std::uintptr_t address, MovementServicesSnapshot& snapshot,
                                   SnapshotReader read) noexcept -> bool {
  if (address == 0 || read == nullptr) {
    return false;
  }
  return read(address, snapshot.data(), snapshot.size());
}

// Put the snapshot back: the engine's object is exactly as it was before the
// simulation.
inline auto restore(std::uintptr_t address, const MovementServicesSnapshot& snapshot,
                    SnapshotWriter write) noexcept -> void {
  if (address == 0 || write == nullptr) {
    return;
  }
  write(address, snapshot.data(), snapshot.size());
}

}  // namespace cs2::control::prediction
