#include "cs2/render/frame_log_backend.hpp"

#include <cstdio>

namespace cs2::render {

bool FrameLogBackend::initialize(const RenderContext& context) {
  (void)context;
  std::puts("[cs2-render] frame-log backend initialized");
  return true;
}

void FrameLogBackend::render(const RenderContext& context, const OverlayModel& model) {
  (void)context;
  (void)model;
  ++frames_;
  if (frames_ % 600 == 0) {
    std::printf("[cs2-render] %llu frames presented\n",
                static_cast<unsigned long long>(frames_));
  }
}

void FrameLogBackend::shutdown() {
  std::printf("[cs2-render] shutdown after %llu frames\n",
              static_cast<unsigned long long>(frames_));
}

}  // namespace cs2::render
