#pragma once

// Vulkan overlay renderer for the cs2-overlay present layer (ADR-0016).
//
// It draws into the swapchain image from inside `vkQueuePresentKHR`, below DXVK
// and outside the game's D3D11 present path, so it cannot reproduce the
// ADR-0015 DXVK deadlock. Today it draws a fixed test window; the shared-memory
// `OverlayModel` consumer replaces that content next.

#include <vulkan/vulkan.h>

namespace cs2::overlay {

struct DeviceContext {
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice physical_device = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  PFN_vkGetDeviceProcAddr get_device_proc_addr = nullptr;
  PFN_vkGetInstanceProcAddr get_instance_proc_addr = nullptr;
};

// Record the game's device (called from `vkCreateDevice`).
void device_created(const DeviceContext& context) noexcept;

// Forget a device and all of its swapchains (called from `vkDestroyDevice`).
void device_destroyed(VkDevice device) noexcept;

// Record swapchain format/extent (called from `vkCreateSwapchainKHR`).
void swapchain_created(VkDevice device, VkSwapchainKHR swapchain, VkFormat format,
                       VkExtent2D extent) noexcept;

// Release per-swapchain resources (called from `vkDestroySwapchainKHR`).
void swapchain_destroyed(VkDevice device, VkSwapchainKHR swapchain) noexcept;

// Draw the overlay into the image about to be presented. Returns a semaphore
// the real present must wait on (the caller replaces the app's wait list with
// it), or VK_NULL_HANDLE if nothing was drawn.
[[nodiscard]] VkSemaphore present(VkDevice device, VkQueue queue, uint32_t queue_family,
                                  const VkPresentInfoKHR* present_info) noexcept;

}  // namespace cs2::overlay
