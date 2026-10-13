#include "renderer.hpp"

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "cs2/overlay/wire.hpp"
#include "cs2/render/esp_font.hpp"
#include "log.hpp"
#include "transport.hpp"

namespace cs2::overlay {
namespace {

struct Functions {
  PFN_vkGetSwapchainImagesKHR get_swapchain_images = nullptr;
  PFN_vkCreateImageView create_image_view = nullptr;
  PFN_vkDestroyImageView destroy_image_view = nullptr;
  PFN_vkCreateRenderPass create_render_pass = nullptr;
  PFN_vkDestroyRenderPass destroy_render_pass = nullptr;
  PFN_vkCreateFramebuffer create_framebuffer = nullptr;
  PFN_vkDestroyFramebuffer destroy_framebuffer = nullptr;
  PFN_vkCreateCommandPool create_command_pool = nullptr;
  PFN_vkDestroyCommandPool destroy_command_pool = nullptr;
  PFN_vkAllocateCommandBuffers allocate_command_buffers = nullptr;
  PFN_vkFreeCommandBuffers free_command_buffers = nullptr;
  PFN_vkResetCommandBuffer reset_command_buffer = nullptr;
  PFN_vkBeginCommandBuffer begin_command_buffer = nullptr;
  PFN_vkEndCommandBuffer end_command_buffer = nullptr;
  PFN_vkCmdPipelineBarrier cmd_pipeline_barrier = nullptr;
  PFN_vkCmdBeginRenderPass cmd_begin_render_pass = nullptr;
  PFN_vkCmdEndRenderPass cmd_end_render_pass = nullptr;
  PFN_vkCreateSemaphore create_semaphore = nullptr;
  PFN_vkDestroySemaphore destroy_semaphore = nullptr;
  PFN_vkCreateFence create_fence = nullptr;
  PFN_vkDestroyFence destroy_fence = nullptr;
  PFN_vkResetFences reset_fences = nullptr;
  PFN_vkGetFenceStatus get_fence_status = nullptr;
  PFN_vkWaitForFences wait_for_fences = nullptr;
  PFN_vkQueueSubmit queue_submit = nullptr;
  PFN_vkDeviceWaitIdle device_wait_idle = nullptr;
  PFN_vkCreateDescriptorPool create_descriptor_pool = nullptr;
  PFN_vkDestroyDescriptorPool destroy_descriptor_pool = nullptr;
};

struct Draw {
  VkCommandBuffer command_buffer = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;
  VkSemaphore semaphore = VK_NULL_HANDLE;
};

// ImGui must resolve Vulkan entry points through the next layer in the chain,
// not the loader's global exports (invalid / re-entrant from within a layer).
struct LoaderContext {
  VkInstance instance = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  PFN_vkGetInstanceProcAddr get_instance_proc_addr = nullptr;
  PFN_vkGetDeviceProcAddr get_device_proc_addr = nullptr;
};

PFN_vkVoidFunction vulkan_loader(const char* name, void* user) {
  auto* context = static_cast<LoaderContext*>(user);
  PFN_vkVoidFunction fn = nullptr;
  if (context->get_device_proc_addr != nullptr) {
    fn = context->get_device_proc_addr(context->device, name);
  }
  if (fn == nullptr && context->get_instance_proc_addr != nullptr) {
    fn = context->get_instance_proc_addr(context->instance, name);
  }
  return fn;
}

struct SwapchainData {
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkExtent2D extent{};
  std::vector<VkImage> images;
  std::vector<VkImageView> views;
  std::vector<VkFramebuffer> framebuffers;
  // One set of present resources per swapchain image. Keying by image index
  // means a command buffer/semaphore is only reused after the app re-acquires
  // that image (i.e. its previous present completed) — never while a present
  // still waits on the semaphore.
  std::vector<VkCommandBuffer> command_buffers;
  std::vector<VkSemaphore> semaphores;
  std::vector<VkFence> fences;
};

struct Device {
  DeviceContext context{};
  Functions fn{};
  uint32_t queue_family = 0;
  VkQueue queue = VK_NULL_HANDLE;
  VkCommandPool command_pool = VK_NULL_HANDLE;
  VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
  std::vector<Draw> draws;
  // One render pass per format, reused across swapchain recreations so ImGui's
  // pipeline (built against it) stays valid.
  VkRenderPass render_pass = VK_NULL_HANDLE;
  VkFormat render_pass_format = VK_FORMAT_UNDEFINED;
  // Last swapchain create info seen, used when building per-swapchain resources.
  VkFormat swapchain_hint_format = VK_FORMAT_UNDEFINED;
  VkExtent2D swapchain_hint_extent{};
  LoaderContext loader{};
  bool imgui_ready = false;
};

std::recursive_mutex g_lock;
std::unordered_map<VkDevice, Device> g_devices;
std::unordered_map<VkSwapchainKHR, SwapchainData> g_swapchains;
VkDevice g_imgui_device = VK_NULL_HANDLE;
ImFont* g_pixel_font = nullptr;
std::uint64_t g_frame = 0;

template <typename T>
T load(PFN_vkGetDeviceProcAddr gdpa, VkDevice device, const char* name) {
  return reinterpret_cast<T>(gdpa(device, name));
}

void check(VkResult result, const char* what) noexcept {
  if (result != VK_SUCCESS) {
    std::fprintf(stderr, "[cs2-overlay] %s failed: %d\n", what, static_cast<int>(result));
  }
}

Functions load_functions(PFN_vkGetDeviceProcAddr gdpa, VkDevice device) {
  Functions fn;
  fn.get_swapchain_images = load<PFN_vkGetSwapchainImagesKHR>(gdpa, device, "vkGetSwapchainImagesKHR");
  fn.create_image_view = load<PFN_vkCreateImageView>(gdpa, device, "vkCreateImageView");
  fn.destroy_image_view = load<PFN_vkDestroyImageView>(gdpa, device, "vkDestroyImageView");
  fn.create_render_pass = load<PFN_vkCreateRenderPass>(gdpa, device, "vkCreateRenderPass");
  fn.destroy_render_pass = load<PFN_vkDestroyRenderPass>(gdpa, device, "vkDestroyRenderPass");
  fn.create_framebuffer = load<PFN_vkCreateFramebuffer>(gdpa, device, "vkCreateFramebuffer");
  fn.destroy_framebuffer = load<PFN_vkDestroyFramebuffer>(gdpa, device, "vkDestroyFramebuffer");
  fn.create_command_pool = load<PFN_vkCreateCommandPool>(gdpa, device, "vkCreateCommandPool");
  fn.destroy_command_pool = load<PFN_vkDestroyCommandPool>(gdpa, device, "vkDestroyCommandPool");
  fn.allocate_command_buffers =
      load<PFN_vkAllocateCommandBuffers>(gdpa, device, "vkAllocateCommandBuffers");
  fn.free_command_buffers = load<PFN_vkFreeCommandBuffers>(gdpa, device, "vkFreeCommandBuffers");
  fn.reset_command_buffer = load<PFN_vkResetCommandBuffer>(gdpa, device, "vkResetCommandBuffer");
  fn.begin_command_buffer = load<PFN_vkBeginCommandBuffer>(gdpa, device, "vkBeginCommandBuffer");
  fn.end_command_buffer = load<PFN_vkEndCommandBuffer>(gdpa, device, "vkEndCommandBuffer");
  fn.cmd_pipeline_barrier = load<PFN_vkCmdPipelineBarrier>(gdpa, device, "vkCmdPipelineBarrier");
  fn.cmd_begin_render_pass = load<PFN_vkCmdBeginRenderPass>(gdpa, device, "vkCmdBeginRenderPass");
  fn.cmd_end_render_pass = load<PFN_vkCmdEndRenderPass>(gdpa, device, "vkCmdEndRenderPass");
  fn.create_semaphore = load<PFN_vkCreateSemaphore>(gdpa, device, "vkCreateSemaphore");
  fn.destroy_semaphore = load<PFN_vkDestroySemaphore>(gdpa, device, "vkDestroySemaphore");
  fn.create_fence = load<PFN_vkCreateFence>(gdpa, device, "vkCreateFence");
  fn.destroy_fence = load<PFN_vkDestroyFence>(gdpa, device, "vkDestroyFence");
  fn.reset_fences = load<PFN_vkResetFences>(gdpa, device, "vkResetFences");
  fn.get_fence_status = load<PFN_vkGetFenceStatus>(gdpa, device, "vkGetFenceStatus");
  fn.wait_for_fences = load<PFN_vkWaitForFences>(gdpa, device, "vkWaitForFences");
  fn.queue_submit = load<PFN_vkQueueSubmit>(gdpa, device, "vkQueueSubmit");
  fn.device_wait_idle = load<PFN_vkDeviceWaitIdle>(gdpa, device, "vkDeviceWaitIdle");
  fn.create_descriptor_pool =
      load<PFN_vkCreateDescriptorPool>(gdpa, device, "vkCreateDescriptorPool");
  fn.destroy_descriptor_pool =
      load<PFN_vkDestroyDescriptorPool>(gdpa, device, "vkDestroyDescriptorPool");
  return fn;
}

VkRenderPass create_render_pass(const Functions& fn, VkDevice device, VkFormat format) {
  VkAttachmentDescription color{};
  color.format = format;
  color.samples = VK_SAMPLE_COUNT_1_BIT;
  color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;  // keep the game's rendered image
  color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  color.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  color.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkAttachmentReference reference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &reference;

  VkSubpassDependency dependency{};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  VkRenderPassCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  info.attachmentCount = 1;
  info.pAttachments = &color;
  info.subpassCount = 1;
  info.pSubpasses = &subpass;
  info.dependencyCount = 1;
  info.pDependencies = &dependency;

  VkRenderPass pass = VK_NULL_HANDLE;
  check(fn.create_render_pass(device, &info, nullptr, &pass), "vkCreateRenderPass");
  return pass;
}

bool ensure_command_pool(Device& device, uint32_t queue_family) {
  if (device.command_pool != VK_NULL_HANDLE) return true;
  VkCommandPoolCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  info.queueFamilyIndex = queue_family;
  check(device.fn.create_command_pool(device.context.device, &info, nullptr, &device.command_pool),
        "vkCreateCommandPool");
  return device.command_pool != VK_NULL_HANDLE;
}

VkRenderPass ensure_render_pass(Device& device, VkFormat format) {
  if (device.render_pass != VK_NULL_HANDLE && device.render_pass_format == format) {
    return device.render_pass;
  }
  // A format change invalidates ImGui's pipeline (built against the render pass).
  if (device.imgui_ready) {
    ImGui_ImplVulkan_Shutdown();
    ImGui::DestroyContext();
    device.imgui_ready = false;
    g_imgui_device = VK_NULL_HANDLE;
  }
  if (device.render_pass != VK_NULL_HANDLE) {
    device.fn.destroy_render_pass(device.context.device, device.render_pass, nullptr);
    device.render_pass = VK_NULL_HANDLE;
  }
  device.render_pass = create_render_pass(device.fn, device.context.device, format);
  device.render_pass_format = format;
  return device.render_pass;
}

SwapchainData* ensure_swapchain(Device& device, VkSwapchainKHR swapchain) {
  const auto found = g_swapchains.find(swapchain);
  if (found != g_swapchains.end()) return &found->second;

  const Functions& fn = device.fn;
  const VkDevice vk_device = device.context.device;
  SwapchainData data{};
  data.format = device.swapchain_hint_format;
  data.extent = device.swapchain_hint_extent;
  if (data.format == VK_FORMAT_UNDEFINED) return nullptr;

  uint32_t image_count = 0;
  check(fn.get_swapchain_images(vk_device, swapchain, &image_count, nullptr), "vkGetSwapchainImagesKHR");
  if (image_count == 0) return nullptr;
  data.images.resize(image_count);
  check(fn.get_swapchain_images(vk_device, swapchain, &image_count, data.images.data()),
        "vkGetSwapchainImagesKHR");

  const VkRenderPass pass = ensure_render_pass(device, data.format);
  data.views.resize(image_count, VK_NULL_HANDLE);
  data.framebuffers.resize(image_count, VK_NULL_HANDLE);
  for (uint32_t i = 0; i < image_count; ++i) {
    VkImageViewCreateInfo view{};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = data.images[i];
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = data.format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    check(fn.create_image_view(vk_device, &view, nullptr, &data.views[i]), "vkCreateImageView");

    VkFramebufferCreateInfo framebuffer{};
    framebuffer.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer.renderPass = pass;
    framebuffer.attachmentCount = 1;
    framebuffer.pAttachments = &data.views[i];
    framebuffer.width = data.extent.width;
    framebuffer.height = data.extent.height;
    framebuffer.layers = 1;
    check(fn.create_framebuffer(vk_device, &framebuffer, nullptr, &data.framebuffers[i]),
          "vkCreateFramebuffer");
  }

  VkCommandBufferAllocateInfo command_alloc{};
  command_alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  command_alloc.commandPool = device.command_pool;
  command_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  command_alloc.commandBufferCount = image_count;
  data.command_buffers.resize(image_count, VK_NULL_HANDLE);
  check(fn.allocate_command_buffers(vk_device, &command_alloc, data.command_buffers.data()),
        "vkAllocateCommandBuffers");

  data.semaphores.resize(image_count, VK_NULL_HANDLE);
  data.fences.resize(image_count, VK_NULL_HANDLE);
  for (uint32_t i = 0; i < image_count; ++i) {
    VkSemaphoreCreateInfo semaphore{};
    semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    check(fn.create_semaphore(vk_device, &semaphore, nullptr, &data.semaphores[i]),
          "vkCreateSemaphore");
    VkFenceCreateInfo fence{};
    fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // first wait over this image passes
    check(fn.create_fence(vk_device, &fence, nullptr, &data.fences[i]), "vkCreateFence");
  }

  return &g_swapchains.emplace(swapchain, std::move(data)).first->second;
}

bool ensure_imgui(Device& device, VkQueue queue, uint32_t queue_family, VkRenderPass pass) {
  if (device.imgui_ready) return true;
  if (g_imgui_device != VK_NULL_HANDLE) return false;  // one ImGui context per process

  device.queue = queue;
  device.queue_family = queue_family;

  VkDescriptorPoolSize pool_size{};
  pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  pool_size.descriptorCount = 8;
  VkDescriptorPoolCreateInfo pool{};
  pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pool.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  pool.maxSets = 8;
  pool.poolSizeCount = 1;
  pool.pPoolSizes = &pool_size;
  check(device.fn.create_descriptor_pool(device.context.device, &pool, nullptr,
                                         &device.descriptor_pool),
        "vkCreateDescriptorPool");

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();
  ImGui::GetIO().IniFilename = nullptr;

  ImFontConfig font_config;
  font_config.FontDataOwnedByAtlas = false;
  font_config.PixelSnapH = true;
  font_config.OversampleH = 1;
  font_config.OversampleV = 1;
  g_pixel_font = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(
      const_cast<unsigned char*>(cs2::render::k_esp_font_ttf),
      static_cast<int>(cs2::render::k_esp_font_ttf_size), 16.0F, &font_config);

  std::fprintf(stderr,
               "[cs2-overlay] imgui init instance=%p physdev=%p device=%p queue=%p family=%u pass=%p\n",
               static_cast<void*>(device.context.instance),
               static_cast<void*>(device.context.physical_device),
               static_cast<void*>(device.context.device), static_cast<void*>(queue), queue_family,
               static_cast<void*>(pass));

  device.loader = LoaderContext{device.context.instance, device.context.device,
                                device.context.get_instance_proc_addr,
                                device.context.get_device_proc_addr};
  ImGui_ImplVulkan_LoadFunctions(vulkan_loader, &device.loader);

  ImGui_ImplVulkan_InitInfo init{};
  init.Instance = device.context.instance;
  init.PhysicalDevice = device.context.physical_device;
  init.Device = device.context.device;
  init.QueueFamily = queue_family;
  init.Queue = queue;
  init.DescriptorPool = device.descriptor_pool;
  init.RenderPass = pass;
  init.MinImageCount = 2;
  init.ImageCount = 3;
  init.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
  if (!ImGui_ImplVulkan_Init(&init)) {
    std::fputs("[cs2-overlay] ImGui_ImplVulkan_Init failed\n", stderr);
    return false;
  }
  if (!ImGui_ImplVulkan_CreateFontsTexture()) {
    std::fputs("[cs2-overlay] font texture creation failed\n", stderr);
    return false;
  }

  device.imgui_ready = true;
  g_imgui_device = device.context.device;
  return true;
}

void image_barrier(const Functions& fn, VkCommandBuffer command_buffer, VkImage image,
                   VkImageLayout from, VkImageLayout to, uint32_t family) {
  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  barrier.oldLayout = from;
  barrier.newLayout = to;
  barrier.srcQueueFamilyIndex = family;
  barrier.dstQueueFamilyIndex = family;
  barrier.image = image;
  barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  fn.cmd_pipeline_barrier(command_buffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1,
                          &barrier);
}

void draw_overlay_model(const wire::Model& model) {
  ImDrawList* draw = ImGui::GetBackgroundDrawList();
  const uint32_t draw_count = model.draw_count < static_cast<uint32_t>(wire::k_max_draws)
                                  ? model.draw_count
                                  : static_cast<uint32_t>(wire::k_max_draws);
  for (uint32_t i = 0; i < draw_count; ++i) {
    const wire::Draw& command = model.draws[i];
    const ImU32 color = IM_COL32(command.r, command.g, command.b, command.a);
    const ImVec2 p0(command.x0, command.y0);
    const ImVec2 p1(command.x1, command.y1);
    switch (command.kind) {
      case wire::DrawKind::rect:
        draw->AddRect(p0, p1, color, 0.0F, 0, command.thickness);
        break;
      case wire::DrawKind::filled_rect:
        draw->AddRectFilled(p0, p1, color);
        break;
      case wire::DrawKind::line:
        draw->AddLine(p0, p1, color, command.thickness);
        break;
      case wire::DrawKind::text: {
        ImFont* font =
            (command.font == 1 && g_pixel_font != nullptr) ? g_pixel_font : ImGui::GetFont();
        const float size = font->FontSize;
        ImVec2 position(command.x0, command.y0);
        if (command.centered != 0) {
          const ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.0F, command.text);
          position.x -= extent.x * 0.5F;
        }
        if (command.shadow != 0) {
          draw->AddText(font, size, ImVec2(position.x + 1.0F, position.y + 1.0F),
                        IM_COL32(0, 0, 0, command.a), command.text);
        }
        draw->AddText(font, size, position, color, command.text);
        break;
      }
    }
  }

  const uint32_t section_count = model.section_count < static_cast<uint32_t>(wire::k_max_sections)
                                     ? model.section_count
                                     : static_cast<uint32_t>(wire::k_max_sections);
  if (section_count == 0 && model.title[0] == '\0') return;

  ImGui::SetNextWindowSize(ImVec2(460, 560), ImGuiCond_FirstUseEver);
  if (ImGui::Begin(model.title[0] != '\0' ? model.title : "cs2-internal")) {
    for (uint32_t s = 0; s < section_count; ++s) {
      const wire::Section& section = model.sections[s];
      if (section.title[0] == '\0') {
        for (uint32_t l = 0; l < section.line_count; ++l) {
          ImGui::Text("%s: %s", section.lines[l].label, section.lines[l].value);
        }
        continue;
      }
      if (ImGui::CollapsingHeader(section.title, ImGuiTreeNodeFlags_DefaultOpen)) {
        for (uint32_t l = 0; l < section.line_count; ++l) {
          ImGui::Text("%s: %s", section.lines[l].label, section.lines[l].value);
        }
      }
    }
  }
  ImGui::End();
}

}  // namespace

void device_created(const DeviceContext& context) noexcept {
  if (context.device == VK_NULL_HANDLE || context.get_device_proc_addr == nullptr) return;
  std::lock_guard lock(g_lock);
  Device device{};
  device.context = context;
  device.fn = load_functions(context.get_device_proc_addr, context.device);
  g_devices.emplace(context.device, std::move(device));
}

void device_destroyed(VkDevice device) noexcept {
  std::lock_guard lock(g_lock);
  const auto it = g_devices.find(device);
  if (it == g_devices.end()) return;
  Device& dev = it->second;
  for (auto sc = g_swapchains.begin(); sc != g_swapchains.end();) {
    sc = g_swapchains.erase(sc);  // per-device teardown happens with the device
  }
  if (dev.imgui_ready) {
    ImGui_ImplVulkan_Shutdown();
    ImGui::DestroyContext();
    g_imgui_device = VK_NULL_HANDLE;
    dev.imgui_ready = false;
  }
  g_devices.erase(it);
}

void swapchain_created(VkDevice device, VkSwapchainKHR swapchain, VkFormat format,
                       VkExtent2D extent) noexcept {
  (void)swapchain;
  std::lock_guard lock(g_lock);
  const auto it = g_devices.find(device);
  if (it == g_devices.end()) return;
  it->second.swapchain_hint_format = format;
  it->second.swapchain_hint_extent = extent;
}

void swapchain_destroyed(VkDevice device, VkSwapchainKHR swapchain) noexcept {
  (void)device;
  std::lock_guard lock(g_lock);
  const auto sc = g_swapchains.find(swapchain);
  if (sc == g_swapchains.end()) return;
  // Do not destroy the framebuffers/views here: our in-flight draw may still
  // reference them, and waiting for idle re-enters DXVK while it holds its
  // swapchain lock. Keep the render pass (format-keyed, reused); the driver
  // reclaims the garbage with the device.
  g_swapchains.erase(sc);
}

VkSemaphore present(VkDevice vk_device, VkQueue queue, uint32_t queue_family,
                    const VkPresentInfoKHR* present_info) noexcept {
  if (present_info == nullptr || present_info->swapchainCount == 0) return VK_NULL_HANDLE;
  const VkSwapchainKHR swapchain_handle = present_info->pSwapchains[0];
  const uint32_t image_index = present_info->pImageIndices != nullptr ? present_info->pImageIndices[0] : 0;

  std::lock_guard lock(g_lock);
  const auto it = g_devices.find(vk_device);
  if (it == g_devices.end()) return VK_NULL_HANDLE;
  Device& device = it->second;
  const Functions& fn = device.fn;

  if (!ensure_command_pool(device, queue_family)) {
    overlay_log("cs2-overlay: present skip (command pool)");
    return VK_NULL_HANDLE;
  }

  SwapchainData* swapchain = ensure_swapchain(device, swapchain_handle);
  if (swapchain == nullptr) {
    overlay_log("cs2-overlay: present skip (no swapchain)");
    return VK_NULL_HANDLE;
  }
  if (!ensure_imgui(device, queue, queue_family, device.render_pass)) {
    overlay_log("cs2-overlay: present skip (imgui)");
    return VK_NULL_HANDLE;
  }
  if (image_index >= swapchain->framebuffers.size()) {
    overlay_log("cs2-overlay: present skip (image %u/%zu)", image_index,
                swapchain->framebuffers.size());
    return VK_NULL_HANDLE;
  }

  // Per-image resources: safe to reuse because the app must re-acquire the
  // image (completing its previous present) before presenting it again.
  const VkCommandBuffer command_buffer = swapchain->command_buffers[image_index];
  VkFence fence = swapchain->fences[image_index];
  fn.wait_for_fences(device.context.device, 1, &fence, VK_TRUE, UINT64_MAX);
  fn.reset_fences(device.context.device, 1, &fence);

  fn.reset_command_buffer(command_buffer, 0);
  VkCommandBufferBeginInfo begin_info{};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  fn.begin_command_buffer(command_buffer, &begin_info);

  image_barrier(fn, command_buffer, swapchain->images[image_index],
                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, queue_family);

  transport::write_extent(swapchain->extent.width, swapchain->extent.height);

  static wire::Model model;
  if (!transport::read_model(model)) {
    // Torn/incomplete read: keep the previous (static) copy.
  }

  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(static_cast<float>(swapchain->extent.width),
                          static_cast<float>(swapchain->extent.height));
  io.DeltaTime = 1.0f / 60.0f;
  ImGui_ImplVulkan_NewFrame();
  ImGui::NewFrame();
  draw_overlay_model(model);
  ImGui::Render();

  const ImDrawData* draw_data = ImGui::GetDrawData();
  if (g_frame < 3 || g_frame % 300 == 0) {
    overlay_log("cs2-overlay: drew frame %llu vtx=%d idx=%d sec=%u draws=%u",
                static_cast<unsigned long long>(g_frame),
                draw_data != nullptr ? draw_data->TotalVtxCount : -1,
                draw_data != nullptr ? draw_data->TotalIdxCount : -1, model.section_count,
                model.draw_count);
  }

  VkRenderPassBeginInfo render_pass{};
  render_pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  render_pass.renderPass = device.render_pass;
  render_pass.framebuffer = swapchain->framebuffers[image_index];
  render_pass.renderArea = {{0, 0}, swapchain->extent};
  fn.cmd_begin_render_pass(command_buffer, &render_pass, VK_SUBPASS_CONTENTS_INLINE);
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command_buffer);
  fn.cmd_end_render_pass(command_buffer);

  image_barrier(fn, command_buffer, swapchain->images[image_index],
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, queue_family);

  fn.end_command_buffer(command_buffer);

  const VkSemaphore render_semaphore = swapchain->semaphores[image_index];
  std::vector<VkPipelineStageFlags> stages(present_info->waitSemaphoreCount,
                                           VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
  VkSubmitInfo submit{};
  submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit.waitSemaphoreCount = present_info->waitSemaphoreCount;
  submit.pWaitSemaphores = present_info->pWaitSemaphores;
  submit.pWaitDstStageMask = stages.empty() ? nullptr : stages.data();
  if (std::getenv("CS2_OVERLAY_NO_WAIT") != nullptr) {
    submit.waitSemaphoreCount = 0;
    submit.pWaitSemaphores = nullptr;
    submit.pWaitDstStageMask = nullptr;
  }
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &command_buffer;
  submit.signalSemaphoreCount = 1;
  submit.pSignalSemaphores = &render_semaphore;
  const VkResult submit_result = fn.queue_submit(queue, 1, &submit, fence);
  if (submit_result != VK_SUCCESS) {
    overlay_log("cs2-overlay: submit FAILED %d", static_cast<int>(submit_result));
    return VK_NULL_HANDLE;
  }

  ++g_frame;
  // Isolation switch: submit but do not chain into the present's wait list.
  if (std::getenv("CS2_OVERLAY_NO_CHAIN") != nullptr) return VK_NULL_HANDLE;
  return render_semaphore;
}

}  // namespace cs2::overlay
