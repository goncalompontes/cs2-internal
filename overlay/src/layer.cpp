// cs2-overlay — portable Vulkan present layer (ADR-0016).
//
// A Vulkan implicit layer that loads into the target process (native Windows,
// or Proton where DXVK presents through the Vulkan loader) and intercepts
// `vkQueuePresentKHR`. It is the "external renderer": it never issues D3D11
// commands inside the game's D3D11 Present path (which is what wedged DXVK),
// and it is platform-neutral.
//
// This milestone implements the loader handshake and the present/swapchain
// interception with logging. Rendering the shared-memory overlay model is added
// on top of the same interception points.

#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>

#include "renderer.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>

// The layer's loader entry points must be exported. `__attribute__((visibility))`
// is a no-op on Windows, where the DLL needs `__declspec(dllexport)`.
#if defined(_WIN32)
#define CS2_OVERLAY_EXPORT __declspec(dllexport)
#else
#define CS2_OVERLAY_EXPORT __attribute__((visibility("default")))
#endif

namespace {

constexpr const char* k_layer_name = "VK_LAYER_CS2_overlay";

std::mutex g_lock;
std::unordered_map<VkInstance, PFN_vkGetInstanceProcAddr> g_instance_gipa;
std::unordered_map<VkInstance, PFN_GetPhysicalDeviceProcAddr> g_instance_pdpa;
std::unordered_map<VkPhysicalDevice, VkInstance> g_physdev_instance;
std::unordered_map<VkDevice, PFN_vkGetDeviceProcAddr> g_device_gdpa;
std::unordered_map<VkDevice, PFN_vkQueuePresentKHR> g_device_present;
std::unordered_map<VkQueue, VkDevice> g_queue_device;
std::unordered_map<VkQueue, uint32_t> g_queue_family;
unsigned g_present_count = 0;

// The layer runs inside the game process (and, under Proton, inside the Steam
// runtime container), so logging goes to an explicit path that we can read from
// the host, overridable with CS2_OVERLAY_LOG.
void log_line(const char* format, ...) noexcept {
  static std::mutex log_mutex;
  const std::lock_guard<std::mutex> lock(log_mutex);
  const char* path = std::getenv("CS2_OVERLAY_LOG");
  static char fallback[512];
  if (path == nullptr) {
    // The layer runs inside the game process (and, under Proton, inside the
    // Steam runtime container); $HOME is shared with the host, /tmp may not be.
    const char* home = std::getenv("HOME");
    std::snprintf(fallback, sizeof(fallback), "%s/cs2-overlay-layer.log",
                  home != nullptr ? home : "/tmp");
    path = fallback;
  }
  std::FILE* file = std::fopen(path, "a");
  if (file == nullptr) return;
  va_list args;
  va_start(args, format);
  std::vfprintf(file, format, args);
  va_end(args);
  std::fputc('\n', file);
  std::fclose(file);
}

auto find_instance_link(VkInstanceCreateInfo* create_info) noexcept -> VkLayerInstanceCreateInfo* {
  auto* chain = reinterpret_cast<VkLayerInstanceCreateInfo*>(const_cast<void*>(create_info->pNext));
  while (chain != nullptr && (chain->sType != VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO ||
                              chain->function != VK_LAYER_LINK_INFO)) {
    chain = reinterpret_cast<VkLayerInstanceCreateInfo*>(const_cast<void*>(chain->pNext));
  }
  return chain;
}

auto find_device_link(VkDeviceCreateInfo* create_info) noexcept -> VkLayerDeviceCreateInfo* {
  auto* chain = reinterpret_cast<VkLayerDeviceCreateInfo*>(const_cast<void*>(create_info->pNext));
  while (chain != nullptr && (chain->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO ||
                              chain->function != VK_LAYER_LINK_INFO)) {
    chain = reinterpret_cast<VkLayerDeviceCreateInfo*>(const_cast<void*>(chain->pNext));
  }
  return chain;
}

PFN_vkGetInstanceProcAddr instance_gipa(VkInstance instance) noexcept {
  const std::lock_guard<std::mutex> lock(g_lock);
  const auto it = g_instance_gipa.find(instance);
  return it == g_instance_gipa.end() ? nullptr : it->second;
}

PFN_vkGetDeviceProcAddr device_gdpa(VkDevice device) noexcept {
  const std::lock_guard<std::mutex> lock(g_lock);
  const auto it = g_device_gdpa.find(device);
  return it == g_device_gdpa.end() ? nullptr : it->second;
}

// --- Intercepted Vulkan entry points -------------------------------------

VKAPI_ATTR VkResult VKAPI_CALL layer_create_instance(const VkInstanceCreateInfo* create_info,
                                                     const VkAllocationCallbacks* allocator,
                                                     VkInstance* instance) noexcept {
  auto* link = find_instance_link(const_cast<VkInstanceCreateInfo*>(create_info));
  if (link == nullptr) return VK_ERROR_INITIALIZATION_FAILED;
  const PFN_vkGetInstanceProcAddr next_get = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
  const PFN_GetPhysicalDeviceProcAddr next_pdpa =
      link->u.pLayerInfo->pfnNextGetPhysicalDeviceProcAddr;
  link->u.pLayerInfo = link->u.pLayerInfo->pNext;

  const auto next = reinterpret_cast<PFN_vkCreateInstance>(next_get(nullptr, "vkCreateInstance"));
  const VkResult result = next(create_info, allocator, instance);
  if (result == VK_SUCCESS) {
    const std::lock_guard<std::mutex> lock(g_lock);
    g_instance_gipa[*instance] = next_get;
    g_instance_pdpa[*instance] = next_pdpa;
    log_line("cs2-overlay: vkCreateInstance ok instance=%p", static_cast<void*>(*instance));
  }
  return result;
}

VKAPI_ATTR void VKAPI_CALL layer_destroy_instance(VkInstance instance,
                                                  const VkAllocationCallbacks* allocator) noexcept {
  PFN_vkGetInstanceProcAddr get = nullptr;
  {
    const std::lock_guard<std::mutex> lock(g_lock);
    const auto it = g_instance_gipa.find(instance);
    if (it != g_instance_gipa.end()) {
      get = it->second;
      g_instance_gipa.erase(it);
    }
    g_instance_pdpa.erase(instance);
  }
  if (get != nullptr) {
    reinterpret_cast<PFN_vkDestroyInstance>(get(instance, "vkDestroyInstance"))(instance, allocator);
  }
}

VKAPI_ATTR VkResult VKAPI_CALL layer_enumerate_physical_devices(VkInstance instance,
                                                                uint32_t* count,
                                                                VkPhysicalDevice* devices) noexcept {
  const PFN_vkGetInstanceProcAddr get = instance_gipa(instance);
  if (get == nullptr) return VK_ERROR_INITIALIZATION_FAILED;
  const auto next = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
      get(instance, "vkEnumeratePhysicalDevices"));
  const VkResult result = next(instance, count, devices);
  if (result == VK_SUCCESS && devices != nullptr) {
    const std::lock_guard<std::mutex> lock(g_lock);
    for (uint32_t i = 0; i < *count; ++i) g_physdev_instance[devices[i]] = instance;
  }
  return result;
}

VKAPI_ATTR VkResult VKAPI_CALL layer_create_device(VkPhysicalDevice gpu,
                                                   const VkDeviceCreateInfo* create_info,
                                                   const VkAllocationCallbacks* allocator,
                                                   VkDevice* device) noexcept {
  auto* link = find_device_link(const_cast<VkDeviceCreateInfo*>(create_info));
  if (link == nullptr) return VK_ERROR_INITIALIZATION_FAILED;
  const PFN_vkGetInstanceProcAddr next_get = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
  const PFN_vkGetDeviceProcAddr next_device_get = link->u.pLayerInfo->pfnNextGetDeviceProcAddr;
  link->u.pLayerInfo = link->u.pLayerInfo->pNext;

  VkInstance instance = VK_NULL_HANDLE;
  {
    const std::lock_guard<std::mutex> lock(g_lock);
    const auto it = g_physdev_instance.find(gpu);
    if (it != g_physdev_instance.end()) instance = it->second;
  }

  const auto next = reinterpret_cast<PFN_vkCreateDevice>(next_get(instance, "vkCreateDevice"));
  const VkResult result = next(gpu, create_info, allocator, device);
  if (result == VK_SUCCESS) {
    const std::lock_guard<std::mutex> lock(g_lock);
    g_device_gdpa[*device] = next_device_get;
    g_device_present[*device] =
        reinterpret_cast<PFN_vkQueuePresentKHR>(next_device_get(*device, "vkQueuePresentKHR"));
    log_line("cs2-overlay: vkCreateDevice ok device=%p", static_cast<void*>(*device));
  }
  if (result == VK_SUCCESS) {
    cs2::overlay::device_created(
        cs2::overlay::DeviceContext{instance, gpu, *device, next_device_get, next_get});
  }
  return result;
}

VKAPI_ATTR void VKAPI_CALL layer_destroy_device(VkDevice device,
                                                const VkAllocationCallbacks* allocator) noexcept {
  cs2::overlay::device_destroyed(device);
  PFN_vkGetDeviceProcAddr get = nullptr;
  {
    const std::lock_guard<std::mutex> lock(g_lock);
    const auto it = g_device_gdpa.find(device);
    if (it != g_device_gdpa.end()) {
      get = it->second;
      g_device_gdpa.erase(it);
    }
    g_device_present.erase(device);
  }
  if (get != nullptr) {
    reinterpret_cast<PFN_vkDestroyDevice>(get(device, "vkDestroyDevice"))(device, allocator);
  }
}

VKAPI_ATTR void VKAPI_CALL layer_get_device_queue(VkDevice device, uint32_t family_index,
                                                  uint32_t queue_index, VkQueue* queue) noexcept {
  const PFN_vkGetDeviceProcAddr get = device_gdpa(device);
  if (get == nullptr) return;
  reinterpret_cast<PFN_vkGetDeviceQueue>(get(device, "vkGetDeviceQueue"))(device, family_index,
                                                                          queue_index, queue);
  const std::lock_guard<std::mutex> lock(g_lock);
  g_queue_device[*queue] = device;
  g_queue_family[*queue] = family_index;
}

VKAPI_ATTR VkResult VKAPI_CALL layer_create_swapchain(VkDevice device,
                                                      const VkSwapchainCreateInfoKHR* create_info,
                                                      const VkAllocationCallbacks* allocator,
                                                      VkSwapchainKHR* swapchain) noexcept {
  const PFN_vkGetDeviceProcAddr get = device_gdpa(device);
  if (get == nullptr) return VK_ERROR_INITIALIZATION_FAILED;
  const auto next = reinterpret_cast<PFN_vkCreateSwapchainKHR>(
      get(device, "vkCreateSwapchainKHR"));
  const VkResult result = next(device, create_info, allocator, swapchain);
  if (result == VK_SUCCESS) {
    log_line("cs2-overlay: vkCreateSwapchainKHR %ux%u fmt=%d", create_info->imageExtent.width,
             create_info->imageExtent.height, static_cast<int>(create_info->imageFormat));
    cs2::overlay::swapchain_created(device, *swapchain, create_info->imageFormat,
                                    create_info->imageExtent);
  }
  return result;
}

VKAPI_ATTR void VKAPI_CALL layer_destroy_swapchain(VkDevice device, VkSwapchainKHR swapchain,
                                                   const VkAllocationCallbacks* allocator) noexcept {
  log_line("cs2-overlay: vkDestroySwapchainKHR");
  cs2::overlay::swapchain_destroyed(device, swapchain);
  const PFN_vkGetDeviceProcAddr get = device_gdpa(device);
  if (get == nullptr) return;
  reinterpret_cast<PFN_vkDestroySwapchainKHR>(get(device, "vkDestroySwapchainKHR"))(
      device, swapchain, allocator);
}

VKAPI_ATTR VkResult VKAPI_CALL layer_queue_present(VkQueue queue,
                                                   const VkPresentInfoKHR* present_info) noexcept {
  VkDevice device = VK_NULL_HANDLE;
  uint32_t family = 0;
  PFN_vkQueuePresentKHR next = nullptr;
  unsigned count = 0;
  {
    const std::lock_guard<std::mutex> lock(g_lock);
    count = ++g_present_count;
    const auto q = g_queue_device.find(queue);
    if (q != g_queue_device.end()) {
      device = q->second;
      const auto d = g_device_present.find(device);
      if (d != g_device_present.end()) next = d->second;
    }
    const auto f = g_queue_family.find(queue);
    if (f != g_queue_family.end()) family = f->second;
  }
  if (count == 1 || count % 600 == 0) {
    log_line("cs2-overlay: vkQueuePresentKHR #%u queue=%p", count, static_cast<void*>(queue));
  }
  if (next == nullptr) return VK_ERROR_INITIALIZATION_FAILED;

  // Isolation switch: intercept but do not draw/submit (diagnostic only).
  if (std::getenv("CS2_OVERLAY_NO_DRAW") != nullptr) return next(queue, present_info);

  // Draw the overlay into the image about to be presented (below DXVK, outside
  // the game's D3D11 present path). The renderer submits on this queue and
  // returns a semaphore the real present must now wait on.
  const VkSemaphore overlay_semaphore =
      device != VK_NULL_HANDLE ? cs2::overlay::present(device, queue, family, present_info)
                               : VK_NULL_HANDLE;
  if (overlay_semaphore == VK_NULL_HANDLE) return next(queue, present_info);

  VkPresentInfoKHR modified = *present_info;
  modified.waitSemaphoreCount = 1;
  modified.pWaitSemaphores = &overlay_semaphore;
  return next(queue, &modified);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL layer_get_device_proc_addr(VkDevice device,
                                                                    const char* name) noexcept {
  if (std::strcmp(name, "vkQueuePresentKHR") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(layer_queue_present);
  }
  if (std::strcmp(name, "vkDestroyDevice") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(layer_destroy_device);
  }
  if (std::strcmp(name, "vkGetDeviceQueue") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(layer_get_device_queue);
  }
  if (std::strcmp(name, "vkCreateSwapchainKHR") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(layer_create_swapchain);
  }
  if (std::strcmp(name, "vkDestroySwapchainKHR") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(layer_destroy_swapchain);
  }
  const PFN_vkGetDeviceProcAddr get = device_gdpa(device);
  return get == nullptr ? nullptr : get(device, name);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL layer_get_instance_proc_addr(VkInstance instance,
                                                                      const char* name) noexcept {
  if (std::strcmp(name, "vkGetInstanceProcAddr") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(layer_get_instance_proc_addr);
  }
  if (std::strcmp(name, "vkGetDeviceProcAddr") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(layer_get_device_proc_addr);
  }
  if (std::strcmp(name, "vkCreateInstance") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(layer_create_instance);
  }
  if (std::strcmp(name, "vkEnumeratePhysicalDevices") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(layer_enumerate_physical_devices);
  }
  if (std::strcmp(name, "vkCreateDevice") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(layer_create_device);
  }
  if (std::strcmp(name, "vkDestroyInstance") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(layer_destroy_instance);
  }
  const PFN_vkGetInstanceProcAddr get = instance_gipa(instance);
  return get == nullptr ? nullptr : get(instance, name);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL layer_get_physical_device_proc_addr(
    VkInstance instance, const char* name) noexcept {
  PFN_GetPhysicalDeviceProcAddr next = nullptr;
  {
    const std::lock_guard<std::mutex> lock(g_lock);
    const auto it = g_instance_pdpa.find(instance);
    if (it != g_instance_pdpa.end()) next = it->second;
  }
  return next == nullptr ? nullptr : next(instance, name);
}

}  // namespace

extern "C" {

CS2_OVERLAY_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* version) {
  if (version->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT) {
    return VK_ERROR_INITIALIZATION_FAILED;
  }
  version->loaderLayerInterfaceVersion = CURRENT_LOADER_LAYER_INTERFACE_VERSION;
  version->pfnGetInstanceProcAddr = layer_get_instance_proc_addr;
  version->pfnGetDeviceProcAddr = layer_get_device_proc_addr;
  version->pfnGetPhysicalDeviceProcAddr = layer_get_physical_device_proc_addr;
  log_line("cs2-overlay: layer negotiated (v%u)", version->loaderLayerInterfaceVersion);
  return VK_SUCCESS;
}

CS2_OVERLAY_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetInstanceProcAddr(VkInstance instance, const char* name) {
  return layer_get_instance_proc_addr(instance, name);
}

CS2_OVERLAY_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetDeviceProcAddr(VkDevice device, const char* name) {
  return layer_get_device_proc_addr(device, name);
}

}  // extern "C"
