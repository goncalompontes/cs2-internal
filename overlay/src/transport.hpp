#pragma once

// Layer-side transport for the shared overlay model (ADR-0016): maps the
// payload's shared segment (under the payload's writable data dir — on native
// Windows `%LOCALAPPDATA%\cs2-internal\cs2-overlay.shm`; under Proton the
// prefix's `drive_c`), publishes the presented extent for the payload, and
// reads the model it writes.

#include <cstdint>

#include "cs2/overlay/wire.hpp"

namespace cs2::overlay::transport {

// Record the current presented extent so the payload can project.
void write_extent(uint32_t width, uint32_t height) noexcept;

// Copy the latest model into `out`. Returns false if the segment is not mapped
// yet or a write was in progress (caller keeps its previous copy).
[[nodiscard]] auto read_model(wire::Model& out) noexcept -> bool;

// Menu settings (ADR-0021): the layer's menu writes, the payload reads.
// Returns false if the segment is not mapped or a write was in progress.
[[nodiscard]] auto read_settings(wire::Settings& out) noexcept -> bool;
void write_settings(const wire::Settings& in) noexcept;

}  // namespace cs2::overlay::transport
