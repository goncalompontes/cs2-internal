#pragma once

// Manual-map core: copies a parsed PE image into a destination and applies base
// relocations. Platform-independent; the destination is abstracted behind
// `ImageSink` so the same code drives host tests and (later) a remote process.

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include "cs2/loader/pe.hpp"

namespace cs2::loader::mapping {

enum class Error {
  size_mismatch,
  bad_section,
  relocation_out_of_range,
};

[[nodiscard]] const char* to_string(Error error) noexcept;

// A destination for a mapped image. `view()` exposes exactly `size_of_image`
// bytes starting at `base()`.
class ImageSink {
 public:
  virtual ~ImageSink() = default;
  [[nodiscard]] virtual std::uintptr_t base() const noexcept = 0;
  [[nodiscard]] virtual std::span<std::byte> view() noexcept = 0;
};

// In-memory sink (host tests and offline mapping).
class BufferSink final : public ImageSink {
 public:
  BufferSink(std::uintptr_t base, std::size_t size) : base_{base}, bytes_(size) {}

  [[nodiscard]] std::uintptr_t base() const noexcept override { return base_; }
  [[nodiscard]] std::span<std::byte> view() noexcept override { return bytes_; }
  [[nodiscard]] std::span<const std::byte> bytes() const noexcept { return bytes_; }

 private:
  std::uintptr_t base_;
  std::vector<std::byte> bytes_;
};

// Copy headers and sections into the sink.
[[nodiscard]] std::expected<void, Error> map_sections(const pe::ImageInfo& image,
                                                      std::span<const std::byte> raw,
                                                      ImageSink& sink);

// Apply base relocations for `delta = actual_base - image_base`.
[[nodiscard]] std::expected<void, Error> apply_relocations(const pe::ImageInfo& image,
                                                           ImageSink& sink,
                                                           std::int64_t delta);

// Convenience: map sections then relocate into the sink's base.
[[nodiscard]] std::expected<void, Error> map_image(const pe::ImageInfo& image,
                                                   std::span<const std::byte> raw,
                                                   ImageSink& sink);

}  // namespace cs2::loader::mapping
