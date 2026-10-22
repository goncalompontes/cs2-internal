// Exercise the loader chain without a Vulkan driver or a running game.
#include "../src/renderer.hpp"
#include <vulkan/vk_layer.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

namespace {
unsigned renderer_calls = 0;
unsigned presents = 0;
VkInstance expected_instance = reinterpret_cast<VkInstance>(0x1000);
VkPhysicalDevice expected_gpu = reinterpret_cast<VkPhysicalDevice>(0x2000);
VkDevice expected_device = reinterpret_cast<VkDevice>(0x3000);
VkQueue expected_queue = reinterpret_cast<VkQueue>(0x4000);
void require(bool value, const char* message) {
  if (!value) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
  }
}
VKAPI_ATTR VkResult VKAPI_CALL create_instance(const VkInstanceCreateInfo*,
    const VkAllocationCallbacks*, VkInstance* instance) {
  *instance = expected_instance;
  return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL enumerate(VkInstance instance, uint32_t* count,
    VkPhysicalDevice* devices) {
  require(instance == expected_instance, "enumeration instance");
  *count = 1;
  if (devices) *devices = expected_gpu;
  return devices ? VK_INCOMPLETE : VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL create_device(VkPhysicalDevice gpu,
    const VkDeviceCreateInfo*, const VkAllocationCallbacks*, VkDevice* device) {
  require(gpu == expected_gpu, "device GPU");
  *device = expected_device;
  return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL queue2(VkDevice device, const VkDeviceQueueInfo2* info,
    VkQueue* queue) {
  require(device == expected_device && info->queueFamilyIndex == 7, "queue2 arguments");
  *queue = expected_queue;
}
VKAPI_ATTR VkResult VKAPI_CALL present(VkQueue queue, const VkPresentInfoKHR* info) {
  require(queue == expected_queue && info->waitSemaphoreCount == 0, "present forwarding");
  ++presents;
  return VK_SUBOPTIMAL_KHR;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL device_proc(VkDevice, const char* name) {
  if (std::strcmp(name, "vkGetDeviceQueue2") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(queue2);
  if (std::strcmp(name, "vkQueuePresentKHR") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(present);
  return nullptr;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL instance_proc(VkInstance instance, const char* name) {
  if (std::strcmp(name, "vkCreateInstance") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(create_instance);
  if (std::strcmp(name, "vkEnumeratePhysicalDevices") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(enumerate);
  if (std::strcmp(name, "vkCreateDevice") == 0) {
    require(instance == expected_instance, "VK_INCOMPLETE enumeration retained GPU mapping");
    return reinterpret_cast<PFN_vkVoidFunction>(create_device);
  }
  return device_proc(VK_NULL_HANDLE, name);
}
}  // namespace

namespace cs2::overlay {
void device_created(const DeviceContext&) noexcept { ++renderer_calls; }
void device_destroyed(VkDevice) noexcept { ++renderer_calls; }
void swapchain_created(VkDevice, VkSwapchainKHR, VkFormat, VkExtent2D) noexcept { ++renderer_calls; }
void swapchain_destroyed(VkDevice, VkSwapchainKHR) noexcept { ++renderer_calls; }
auto present(VkDevice, VkQueue, uint32_t, const VkPresentInfoKHR*) noexcept -> VkSemaphore {
  ++renderer_calls;
  return VK_NULL_HANDLE;
}
}  // namespace cs2::overlay

int main() {
  VkNegotiateLayerInterface negotiation{};
  negotiation.sType = LAYER_NEGOTIATE_INTERFACE_STRUCT;
  negotiation.loaderLayerInterfaceVersion = 2;
  require(vkNegotiateLoaderLayerInterfaceVersion(&negotiation) == VK_SUCCESS,
          "interface negotiation");
  require(negotiation.loaderLayerInterfaceVersion == 2, "do not raise loader version");
  auto get = negotiation.pfnGetInstanceProcAddr;
  VkLayerInstanceLink instance_link{};
  instance_link.pfnNextGetInstanceProcAddr = instance_proc;
  VkLayerInstanceCreateInfo instance_chain{};
  instance_chain.sType = VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO;
  instance_chain.function = VK_LAYER_LINK_INFO;
  instance_chain.u.pLayerInfo = &instance_link;
  VkInstanceCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  info.pNext = &instance_chain;
  VkInstance instance{};
  require(reinterpret_cast<PFN_vkCreateInstance>(get(VK_NULL_HANDLE, "vkCreateInstance"))(
              &info, nullptr, &instance) == VK_SUCCESS, "instance chain");
  require(instance_chain.u.pLayerInfo == nullptr, "advance instance link");
  uint32_t count = 1;
  VkPhysicalDevice gpu{};
  require(reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(get(instance,
              "vkEnumeratePhysicalDevices"))(instance, &count, &gpu) == VK_INCOMPLETE,
          "forward incomplete enumeration");
  VkLayerDeviceLink device_link{};
  device_link.pfnNextGetInstanceProcAddr = instance_proc;
  device_link.pfnNextGetDeviceProcAddr = device_proc;
  VkLayerDeviceCreateInfo device_chain{};
  device_chain.sType = VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO;
  device_chain.function = VK_LAYER_LINK_INFO;
  device_chain.u.pLayerInfo = &device_link;
  VkDeviceCreateInfo device_info{};
  device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  device_info.pNext = &device_chain;
  VkDevice device{};
  require(reinterpret_cast<PFN_vkCreateDevice>(get(instance, "vkCreateDevice"))(
              gpu, &device_info, nullptr, &device) == VK_SUCCESS, "device chain");
  require(device_chain.u.pLayerInfo == nullptr, "advance device link");
  auto get_device = negotiation.pfnGetDeviceProcAddr;
  for (const char* name : {"vkGetDeviceQueue", "vkGetDeviceQueue2", "vkQueuePresentKHR",
                           "vkCreateSwapchainKHR", "vkDestroySwapchainKHR", "vkDestroyDevice"}) {
    require(get(instance, name) == get_device(device, name), "both lookup paths intercept");
    require(get(instance, name) != nullptr, "interceptor exists");
  }
  VkDeviceQueueInfo2 queue_info{};
  queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2;
  queue_info.queueFamilyIndex = 7;
  VkQueue queue{};
  reinterpret_cast<PFN_vkGetDeviceQueue2>(get(instance, "vkGetDeviceQueue2"))(
      device, &queue_info, &queue);
  VkPresentInfoKHR present_info{};
  present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  require(reinterpret_cast<PFN_vkQueuePresentKHR>(get_device(device, "vkQueuePresentKHR"))(
              queue, &present_info) == VK_SUBOPTIMAL_KHR, "queue2 present chain");
  require(presents == 1, "exactly one downstream present");
  require(renderer_calls == 0, "non-CS2 application has no renderer or input side effects");
  std::puts("PASS: loader negotiation, both dispatch paths, incomplete enumeration, queue2, non-CS2 forwarding");
}
