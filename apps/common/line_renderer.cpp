#include "line_renderer.h"

#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <deque>
#include <stdexcept>
#include <string>

namespace vibr {

namespace {

const std::uint32_t kLineVert[] =
#include "line.vert.inc"
    ;
const std::uint32_t kLineFrag[] =
#include "line.frag.inc"
    ;

}  // namespace

// ---------------------------------------------------------------- GPU data

namespace {
struct PushConstants {
  Mat4 mvp;
  float viewport[2];
  float width;
  float feather;
};

constexpr std::uint32_t kMaxLines = 131072;  // (the headset's line can run the whole song)

void check(VkResult r, const char* what) {
  if (r != VK_SUCCESS) throw std::runtime_error(std::string(what) + " failed with VkResult " + std::to_string(int(r)));
}

VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
                                              const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
  if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) std::fprintf(stderr, "[vulkan] %s\n", data->pMessage);
  return VK_FALSE;
}

struct Buffer {
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  void* mapped = nullptr;
};

}  // namespace

class Renderer::Impl {
 public:
  float background[3] = {0.0f, 0.0f, 0.0f};  // (from Display::set_background, each draw)
  explicit Impl(SDL_Window* window) : window_(window) {
    create_instance();
    if (!SDL_Vulkan_CreateSurface(window_, instance_, nullptr, &surface_))
      throw std::runtime_error(std::string("SDL_Vulkan_CreateSurface: ") + SDL_GetError());
    pick_device();
    create_device();
    create_render_pass();
    create_pipelines();
    lines_ = create_buffer(sizeof(LineInstance) * kMaxLines);
    create_commands();
    create_swapchain();
  }

  ~Impl() {
    if (!device_) return;
    vkDeviceWaitIdle(device_);
    destroy_swapchain();
    for (Buffer* b : {&lines_}) {
      if (b->buffer) vkDestroyBuffer(device_, b->buffer, nullptr);
      if (b->memory) vkFreeMemory(device_, b->memory, nullptr);
    }
    if (line_pipeline_) vkDestroyPipeline(device_, line_pipeline_, nullptr);
    if (layout_) vkDestroyPipelineLayout(device_, layout_, nullptr);
    if (render_pass_) vkDestroyRenderPass(device_, render_pass_, nullptr);
    if (image_ready_) vkDestroySemaphore(device_, image_ready_, nullptr);
    if (render_done_) vkDestroySemaphore(device_, render_done_, nullptr);
    if (frame_fence_) vkDestroyFence(device_, frame_fence_, nullptr);
    if (pool_) vkDestroyCommandPool(device_, pool_, nullptr);
    vkDestroyDevice(device_, nullptr);
    if (surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (messenger_) {
      auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
      if (destroy) destroy(instance_, messenger_, nullptr);
    }
    vkDestroyInstance(instance_, nullptr);
  }

  const char* gpu_name() const { return gpu_name_.c_str(); }
  // The picture inside the window: all of it, or the shape asked for, centred, with bars.
  VkRect2D picture() const {
    VkRect2D r{{0, 0}, extent_};
    if (shape_ <= 0 || extent_.width == 0 || extent_.height == 0) return r;
    const float window = float(extent_.width) / float(extent_.height);
    if (window > shape_) {  // wider than the picture: bars at the sides
      r.extent.width = std::max(1u, std::uint32_t(std::lround(float(extent_.height) * shape_)));
      r.offset.x = std::int32_t((extent_.width - r.extent.width) / 2);
    } else {  // taller: above and below
      r.extent.height = std::max(1u, std::uint32_t(std::lround(float(extent_.width) / shape_)));
      r.offset.y = std::int32_t((extent_.height - r.extent.height) / 2);
    }
    return r;
  }
  float aspect() const {
    const VkRect2D p = picture();
    return p.extent.height ? float(p.extent.width) / float(p.extent.height) : 0.0f;
  }
  int height() const { return int(picture().extent.height); }
  void request_resize() { resize_ = true; }
  void set_vsync(bool on) {
    if (on == vsync_) return;
    vsync_ = on;
    resize_ = true;  // (a new swapchain with the other present mode)
  }
  void set_shape(float aspect) { shape_ = aspect; }
  void surface_changed() { surface_lost_ = true; }
  // A new surface for the window (the old one gone with its swapchain); false if the
  // window has none yet (still in the background): drawing waits.
  bool remake_surface() {
    vkDeviceWaitIdle(device_);
    destroy_swapchain();
    if (surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
    surface_ = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(window_, instance_, nullptr, &surface_)) {
      surface_ = VK_NULL_HANDLE;
      return false;
    }
    VkBool32 can_present = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(gpu_, queue_family_, surface_, &can_present);
    if (!can_present) return false;
    surface_lost_ = false;
    resize_ = true;
    pending_.clear();
    last_waited_ = 0;
    std::printf("Display: a new surface (back from the background)\n");
    return true;
  }

  FrameTiming timing() const { return timing_; }

  void draw(const std::vector<Layer>& layers, float line_width) {
    const std::uint64_t start_ns = SDL_GetTicksNS();
    // (Measured at every return, a frame drawn or not; the interval is between frames handed over.)
    struct Measure {
      Impl& r;
      std::uint64_t start, waited = 0;
      ~Measure() {
        const std::uint64_t end = SDL_GetTicksNS();
        r.timing_.wait_ms = double(waited) * 1e-6;
        r.timing_.submit_ms = double(end - start - waited) * 1e-6;
        r.timing_.interval_ms = r.last_frame_ns_ ? double(end - r.last_frame_ns_) * 1e-6 : 0.0;
        r.last_frame_ns_ = end;
        const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(r.window_));
        r.timing_.period_ms = mode && mode->refresh_rate > 0 ? 1000.0 / double(mode->refresh_rate) : 0.0;
      }
    } measure{*this, start_ns};
    // Frames handed over earlier that have reached the screen since (a poll: their time is
    // known to within this moment).
    if (present_wait_ok_) poll_presented();
    // The window's surface was replaced (a phone back from the background) or lost: a new one.
    if (surface_lost_ && !remake_surface()) return;
    if (resize_ || extent_.width == 0) {
      vkDeviceWaitIdle(device_);
      create_swapchain();
      resize_ = false;
      if (extent_.width == 0) return;  // minimised
    }
    vkWaitForFences(device_, 1, &frame_fence_, VK_TRUE, UINT64_MAX);
    std::uint32_t image = 0;
    VkResult r = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, image_ready_, VK_NULL_HANDLE, &image);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) { resize_ = true; return; }
    if (r == VK_ERROR_SURFACE_LOST_KHR) { surface_lost_ = true; return; }
    if (r != VK_SUBOPTIMAL_KHR) check(r, "vkAcquireNextImageKHR");
    vkResetFences(device_, 1, &frame_fence_);
    measure.waited = SDL_GetTicksNS() - start_ns;  // the fence and the next image: vsync's wait
    if (present_wait_ok_) poll_presented();  // (an image comes free as one is shown: the closest moment to poll)

    // Every layer's lines go into the one buffer, back to back.
    std::vector<std::uint32_t> firsts, counts;
    std::uint32_t used = 0;
    for (const Layer& layer : layers) {
      const std::uint32_t n = layer.lines ? std::min<std::uint32_t>(std::uint32_t(layer.lines->size()), kMaxLines - used) : 0;
      if (n) std::memcpy(static_cast<LineInstance*>(lines_.mapped) + used, layer.lines->data(), sizeof(LineInstance) * n);
      firsts.push_back(used);
      counts.push_back(n);
      used += n;
    }
    const VkRect2D pic = picture();
    const float aspect = float(pic.extent.width) / float(pic.extent.height);

    vkResetCommandBuffer(cmd_, 0);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    check(vkBeginCommandBuffer(cmd_, &begin), "vkBeginCommandBuffer");
    VkClearValue clear[2];
    clear[0].color = {{background[0], background[1], background[2], 1.0f}};
    clear[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass = render_pass_;
    rp.framebuffer = framebuffers_[image];
    rp.renderArea.extent = extent_;
    rp.clearValueCount = 2;
    rp.pClearValues = clear;
    vkCmdBeginRenderPass(cmd_, &rp, VK_SUBPASS_CONTENTS_INLINE);
    // (The bars, if any, keep the clear colour: the background.)
    const VkViewport vp{float(pic.offset.x), float(pic.offset.y), float(pic.extent.width), float(pic.extent.height), 0, 1};
    const VkRect2D scissor = pic;
    vkCmdSetViewport(cmd_, 0, 1, &vp);
    vkCmdSetScissor(cmd_, 0, 1, &scissor);
    const VkDeviceSize zero = 0;
    vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, line_pipeline_);
    vkCmdBindVertexBuffers(cmd_, 0, 1, &lines_.buffer, &zero);
    for (std::size_t i = 0; i < layers.size(); ++i) {
      if (!counts[i]) continue;
      PushConstants pc;
      pc.mvp = mul(perspective(layers[i].fov_y, aspect, 10.0f, 100000.0f), layers[i].view);
      pc.viewport[0] = float(pic.extent.width);
      pc.viewport[1] = float(pic.extent.height);
      const float glow = layers[i].glow;  // (a glow pass: wide, fading all the way out)
      pc.width = glow > 0 ? glow * std::max(line_width, 1.0f) : line_width;
      pc.feather = glow > 0 ? 0.5f * pc.width : 0.75f;
      vkCmdPushConstants(cmd_, layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof pc, &pc);
      vkCmdDraw(cmd_, 6, counts[i], 0, firsts[i]);
    }
    vkCmdEndRenderPass(cmd_);
    check(vkEndCommandBuffer(cmd_), "vkEndCommandBuffer");

    const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &image_ready_;
    submit.pWaitDstStageMask = &wait_stage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd_;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &render_done_;
    check(vkQueueSubmit(queue_, 1, &submit, frame_fence_), "vkQueueSubmit");

    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &render_done_;
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain_;
    present.pImageIndices = &image;
    const std::uint64_t id = ++present_id_;
    VkPresentIdKHR present_id{VK_STRUCTURE_TYPE_PRESENT_ID_KHR};
    if (present_wait_ok_) {
      present_id.swapchainCount = 1;
      present_id.pPresentIds = &id;
      present.pNext = &present_id;
      pending_.push_back({id, start_ns});
    }
    r = vkQueuePresentKHR(queue_, &present);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) resize_ = true;
    else if (r == VK_SUBOPTIMAL_KHR) resize_ = suboptimal_needs_new_swapchain();
    else if (r == VK_ERROR_SURFACE_LOST_KHR) surface_lost_ = true;
    else check(r, "vkQueuePresentKHR");
    // Low latency with present wait: wait here until the frame before this one is on the
    // screen, so at most this frame waits in line and the next is built close to its turn.
    if (present_wait_ok_ && low_latency_ && !resize_ && id > 1) wait_presented(id - 1, 100'000'000);
  }

  // Display lead (task 4.16 F): how long from the start of a draw() until its frame
  // reaches the screen, smoothed; measured with present wait, else estimated from the
  // frames that can wait in line (FIFO: the images but one; otherwise one frame).
  double display_lead_ms() const {
    if (lead_ms_ > 0) return lead_ms_;
    const double period = timing_.period_ms > 0 ? timing_.period_ms : 1000.0 / 60.0;
    if (present_mode_ == VK_PRESENT_MODE_FIFO_KHR) return period * double(std::max(1u, image_count_ - 1));
    return period;
  }
  bool lead_measured() const { return lead_ms_ > 0; }
  void set_low_latency(bool on) {
    if (on == low_latency_) return;
    low_latency_ = on;
    resize_ = true;
  }

 private:
  void create_instance() {
    Uint32 ext_count = 0;
    const char* const* sdl_exts = SDL_Vulkan_GetInstanceExtensions(&ext_count);
    if (!sdl_exts) throw std::runtime_error(std::string("SDL_Vulkan_GetInstanceExtensions: ") + SDL_GetError());
    std::vector<const char*> exts(sdl_exts, sdl_exts + ext_count);

    // Use the validation layer when the SDK provides it; its messages go to stderr.
    std::uint32_t layer_count = 0;
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
    std::vector<VkLayerProperties> layers(layer_count);
    vkEnumerateInstanceLayerProperties(&layer_count, layers.data());
    const char* validation = "VK_LAYER_KHRONOS_validation";
    bool have_validation = false;
    for (const auto& l : layers) have_validation |= std::strcmp(l.layerName, validation) == 0;
    if (have_validation) exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Vib-Ribbon VR";
    // 1.1 where the loader has it (for VK_KHR_present_wait's feature query; task 4.16 F).
    std::uint32_t loader = VK_API_VERSION_1_0;
    if (auto version = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion")))
      version(&loader);
    instance_api_ = loader >= VK_API_VERSION_1_1 ? VK_API_VERSION_1_1 : VK_API_VERSION_1_0;
    app.apiVersion = instance_api_;
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = std::uint32_t(exts.size());
    info.ppEnabledExtensionNames = exts.data();
    if (have_validation) {
      info.enabledLayerCount = 1;
      info.ppEnabledLayerNames = &validation;
    }
    check(vkCreateInstance(&info, nullptr, &instance_), "vkCreateInstance");

    if (have_validation) {
      auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
      VkDebugUtilsMessengerCreateInfoEXT dbg{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
      dbg.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
      dbg.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
      dbg.pfnUserCallback = debug_callback;
      if (create) create(instance_, &dbg, nullptr, &messenger_);
    }
    std::printf("Vulkan validation layer: %s\n", have_validation ? "on" : "not available");
  }

  void pick_device() {
    std::uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance_, &count, devices.data());
    int best_score = -1;
    for (VkPhysicalDevice d : devices) {
      std::uint32_t qcount = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(d, &qcount, nullptr);
      std::vector<VkQueueFamilyProperties> families(qcount);
      vkGetPhysicalDeviceQueueFamilyProperties(d, &qcount, families.data());
      for (std::uint32_t q = 0; q < qcount; ++q) {
        VkBool32 can_present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(d, q, surface_, &can_present);
        if (!(families[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) || !can_present) continue;
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(d, &props);
        // Prefer the discrete GPU: this PC also has an integrated one.
        const int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2 : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 1 : 0;
        if (score > best_score) {
          best_score = score;
          gpu_ = d;
          queue_family_ = q;
          gpu_name_ = props.deviceName;
        }
        break;
      }
    }
    if (best_score < 0) throw std::runtime_error("no Vulkan device can draw to this window");
  }

  void create_device() {
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    q.queueFamilyIndex = queue_family_;
    q.queueCount = 1;
    q.pQueuePriorities = &priority;
    std::vector<const char*> exts = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    // When each frame reached the screen (task 4.16 F): VK_KHR_present_id and
    // VK_KHR_present_wait, where the driver has both (needs Vulkan 1.1 for the query).
    VkPhysicalDevicePresentIdFeaturesKHR id_features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR};
    VkPhysicalDevicePresentWaitFeaturesKHR wait_features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR};
    {
      VkPhysicalDeviceProperties props;
      vkGetPhysicalDeviceProperties(gpu_, &props);
      std::uint32_t n = 0;
      vkEnumerateDeviceExtensionProperties(gpu_, nullptr, &n, nullptr);
      std::vector<VkExtensionProperties> have(n);
      vkEnumerateDeviceExtensionProperties(gpu_, nullptr, &n, have.data());
      auto has = [&](const char* name) {
        return std::any_of(have.begin(), have.end(), [&](const VkExtensionProperties& e) { return std::strcmp(e.extensionName, name) == 0; });
      };
      auto features2 = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(vkGetInstanceProcAddr(instance_, "vkGetPhysicalDeviceFeatures2"));
      if (instance_api_ >= VK_API_VERSION_1_1 && props.apiVersion >= VK_API_VERSION_1_1 && features2 &&
          has(VK_KHR_PRESENT_ID_EXTENSION_NAME) && has(VK_KHR_PRESENT_WAIT_EXTENSION_NAME)) {
        VkPhysicalDeviceFeatures2 f{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        f.pNext = &id_features;
        id_features.pNext = &wait_features;
        features2(gpu_, &f);
        if (id_features.presentId && wait_features.presentWait) {
          exts.push_back(VK_KHR_PRESENT_ID_EXTENSION_NAME);
          exts.push_back(VK_KHR_PRESENT_WAIT_EXTENSION_NAME);
          id_features.pNext = &wait_features;
          wait_features.pNext = nullptr;
          info.pNext = &id_features;
          present_wait_ok_ = true;
        }
      }
    }
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &q;
    info.enabledExtensionCount = std::uint32_t(exts.size());
    info.ppEnabledExtensionNames = exts.data();
    check(vkCreateDevice(gpu_, &info, nullptr, &device_), "vkCreateDevice");
    vkGetDeviceQueue(device_, queue_family_, 0, &queue_);
    if (present_wait_ok_) {
      wait_for_present_ = reinterpret_cast<PFN_vkWaitForPresentKHR>(vkGetDeviceProcAddr(device_, "vkWaitForPresentKHR"));
      present_wait_ok_ = wait_for_present_ != nullptr;
    }
    std::printf("Monitor: %s\n", present_wait_ok_ ? "frames' display times measured (present wait)"
                                                  : "frames' display times estimated (no present wait on this driver)");

    std::uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu_, surface_, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu_, surface_, &count, formats.data());
    if (formats.empty()) throw std::runtime_error("surface reports no formats");
    surface_format_ = formats[0];
    for (const auto& f : formats)  // plain 8-bit output: line colours are authored display values
      if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        surface_format_ = f;
        break;
      }
    for (VkFormat f : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM}) {
      VkFormatProperties p;
      vkGetPhysicalDeviceFormatProperties(gpu_, f, &p);
      if (p.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) { depth_format_ = f; break; }
    }
    if (depth_format_ == VK_FORMAT_UNDEFINED) throw std::runtime_error("no usable depth format");
  }

  void create_render_pass() {
    VkAttachmentDescription att[2]{};
    att[0].format = surface_format_.format;
    att[0].samples = VK_SAMPLE_COUNT_1_BIT;
    att[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    att[1] = att[0];
    att[1].format = depth_format_;
    att[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    const VkAttachmentReference colour{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    const VkAttachmentReference depth{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &colour;
    sub.pDepthStencilAttachment = &depth;
    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstStageMask = dep.srcStageMask;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    info.attachmentCount = 2;
    info.pAttachments = att;
    info.subpassCount = 1;
    info.pSubpasses = &sub;
    info.dependencyCount = 1;
    info.pDependencies = &dep;
    check(vkCreateRenderPass(device_, &info, nullptr, &render_pass_), "vkCreateRenderPass");
  }

  VkShaderModule shader(const std::uint32_t* code, std::size_t bytes) {
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = bytes;
    info.pCode = code;
    VkShaderModule m = VK_NULL_HANDLE;
    check(vkCreateShaderModule(device_, &info, nullptr, &m), "vkCreateShaderModule");
    return m;
  }

  VkPipeline pipeline(VkShaderModule vert, VkShaderModule frag, const VkVertexInputBindingDescription& binding,
                      const std::vector<VkVertexInputAttributeDescription>& attrs, bool blend, bool depth_write) {
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";
    VkPipelineVertexInputStateCreateInfo vin{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vin.vertexBindingDescriptionCount = 1;
    vin.pVertexBindingDescriptions = &binding;
    vin.vertexAttributeDescriptionCount = std::uint32_t(attrs.size());
    vin.pVertexAttributeDescriptions = attrs.data();
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = depth_write ? VK_TRUE : VK_FALSE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cba.blendEnable = blend ? VK_TRUE : VK_FALSE;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.colorBlendOp = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.alphaBlendOp = VK_BLEND_OP_ADD;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;
    const VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dyn_states;
    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vin;
    info.pInputAssemblyState = &ia;
    info.pViewportState = &vp;
    info.pRasterizationState = &rs;
    info.pMultisampleState = &ms;
    info.pDepthStencilState = &ds;
    info.pColorBlendState = &cb;
    info.pDynamicState = &dyn;
    info.layout = layout_;
    info.renderPass = render_pass_;
    VkPipeline p = VK_NULL_HANDLE;
    check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr, &p), "vkCreateGraphicsPipelines");
    return p;
  }

  void create_pipelines() {
    const VkPushConstantRange range{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants)};
    VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    li.pushConstantRangeCount = 1;
    li.pPushConstantRanges = &range;
    check(vkCreatePipelineLayout(device_, &li, nullptr, &layout_), "vkCreatePipelineLayout");

    const VkShaderModule lv = shader(kLineVert, sizeof kLineVert), lf = shader(kLineFrag, sizeof kLineFrag);
    line_pipeline_ = pipeline(lv, lf, {0, sizeof(LineInstance), VK_VERTEX_INPUT_RATE_INSTANCE},
                              {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(LineInstance, a)},
                               {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(LineInstance, b)},
                               {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(LineInstance, colour)}},
                              true, false);
    for (VkShaderModule m : {lv, lf}) vkDestroyShaderModule(device_, m, nullptr);
  }

  std::uint32_t memory_type(std::uint32_t allowed, VkMemoryPropertyFlags want) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(gpu_, &mp);
    for (std::uint32_t i = 0; i < mp.memoryTypeCount; ++i)
      if ((allowed & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) return i;
    throw std::runtime_error("no suitable GPU memory type");
  }

  Buffer create_buffer(VkDeviceSize size) {
    Buffer b;
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    check(vkCreateBuffer(device_, &info, nullptr, &b.buffer), "vkCreateBuffer");
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device_, b.buffer, &req);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(vkAllocateMemory(device_, &alloc, nullptr, &b.memory), "vkAllocateMemory");
    check(vkBindBufferMemory(device_, b.buffer, b.memory, 0), "vkBindBufferMemory");
    check(vkMapMemory(device_, b.memory, 0, size, 0, &b.mapped), "vkMapMemory");
    return b;
  }

  void create_commands() {
    VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pi.queueFamilyIndex = queue_family_;
    check(vkCreateCommandPool(device_, &pi, nullptr, &pool_), "vkCreateCommandPool");
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = pool_;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    check(vkAllocateCommandBuffers(device_, &ai, &cmd_), "vkAllocateCommandBuffers");
    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    check(vkCreateSemaphore(device_, &si, nullptr, &image_ready_), "vkCreateSemaphore");
    check(vkCreateSemaphore(device_, &si, nullptr, &render_done_), "vkCreateSemaphore");
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    check(vkCreateFence(device_, &fi, nullptr, &frame_fence_), "vkCreateFence");
  }

  // Android calls every present suboptimal while the system turns our images (see
  // create_swapchain); a new swapchain only when the screen's turn has changed.
  bool suboptimal_needs_new_swapchain() {
#ifdef __ANDROID__
    VkSurfaceCapabilitiesKHR caps;
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu_, surface_, &caps) != VK_SUCCESS) return true;
    return caps.currentTransform != made_transform_;
#else
    return true;
#endif
  }

  void destroy_swapchain() {
    for (VkFramebuffer f : framebuffers_) vkDestroyFramebuffer(device_, f, nullptr);
    for (VkImageView v : views_) vkDestroyImageView(device_, v, nullptr);
    framebuffers_.clear();
    views_.clear();
    if (depth_view_) vkDestroyImageView(device_, depth_view_, nullptr);
    if (depth_image_) vkDestroyImage(device_, depth_image_, nullptr);
    if (depth_memory_) vkFreeMemory(device_, depth_memory_, nullptr);
    depth_view_ = VK_NULL_HANDLE; depth_image_ = VK_NULL_HANDLE; depth_memory_ = VK_NULL_HANDLE;
    if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
  }

  void create_swapchain() {
    destroy_swapchain();
    VkSurfaceCapabilitiesKHR caps;
    check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu_, surface_, &caps), "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    extent_ = caps.currentExtent;
    if (extent_.width == UINT32_MAX) extent_ = {std::uint32_t(std::max(w, 0)), std::uint32_t(std::max(h, 0))};
    VkSurfaceTransformFlagBitsKHR transform = caps.currentTransform;
#ifdef __ANDROID__
    // Android gives the surface in the screen's natural (upright, on a phone) shape with a
    // turn to apply. We draw upright pictures and let the system turn them: the images
    // take the turned shape (sideways on a sideways phone) and no turn of our own.
    made_transform_ = caps.currentTransform;
    if (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) {
      transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
      if (caps.currentTransform & (VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR | VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR))
        std::swap(extent_.width, extent_.height);
    }
#endif
    extent_.width = std::clamp(extent_.width, caps.minImageExtent.width, caps.maxImageExtent.width);
    extent_.height = std::clamp(extent_.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    if (extent_.width == 0 || extent_.height == 0) { extent_ = {0, 0}; return; }

    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = surface_;
    // Low latency (task 4.16 F): as few images as the display allows, so fewer frames wait in line.
    info.minImageCount = low_latency_ ? std::max(2u, caps.minImageCount) : caps.minImageCount + 1;
    if (caps.maxImageCount && info.minImageCount > caps.maxImageCount) info.minImageCount = caps.maxImageCount;
    info.imageFormat = surface_format_.format;
    info.imageColorSpace = surface_format_.colorSpace;
    info.imageExtent = extent_;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = transform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;  // vsync; always available
    if (!vsync_) {  // as fast as frames come: mailbox (no tearing) if offered, else immediate
      std::uint32_t n = 0;
      vkGetPhysicalDeviceSurfacePresentModesKHR(gpu_, surface_, &n, nullptr);
      std::vector<VkPresentModeKHR> modes(n);
      vkGetPhysicalDeviceSurfacePresentModesKHR(gpu_, surface_, &n, modes.data());
      auto has = [&](VkPresentModeKHR m) { return std::find(modes.begin(), modes.end(), m) != modes.end(); };
      if (has(VK_PRESENT_MODE_MAILBOX_KHR)) info.presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
      else if (has(VK_PRESENT_MODE_IMMEDIATE_KHR)) info.presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    }
    if (info.presentMode != present_mode_)
      std::printf("Monitor: %s\n", info.presentMode == VK_PRESENT_MODE_FIFO_KHR      ? "vsync on"
                                   : info.presentMode == VK_PRESENT_MODE_MAILBOX_KHR ? "vsync off (mailbox: no tearing)"
                                                                                       : "vsync off (immediate)");
    present_mode_ = info.presentMode;
    info.clipped = VK_TRUE;
    check(vkCreateSwapchainKHR(device_, &info, nullptr, &swapchain_), "vkCreateSwapchainKHR");
    pending_.clear();  // (a new swapchain counts its presents afresh)
    last_waited_ = 0;

    std::uint32_t count = 0;
    vkGetSwapchainImagesKHR(device_, swapchain_, &count, nullptr);
    image_count_ = count;
    std::vector<VkImage> images(count);
    vkGetSwapchainImagesKHR(device_, swapchain_, &count, images.data());

    VkImageCreateInfo di{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    di.imageType = VK_IMAGE_TYPE_2D;
    di.format = depth_format_;
    di.extent = {extent_.width, extent_.height, 1};
    di.mipLevels = 1;
    di.arrayLayers = 1;
    di.samples = VK_SAMPLE_COUNT_1_BIT;
    di.tiling = VK_IMAGE_TILING_OPTIMAL;
    di.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    check(vkCreateImage(device_, &di, nullptr, &depth_image_), "vkCreateImage");
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device_, depth_image_, &req);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(device_, &alloc, nullptr, &depth_memory_), "vkAllocateMemory");
    check(vkBindImageMemory(device_, depth_image_, depth_memory_, 0), "vkBindImageMemory");

    auto make_view = [&](VkImage image, VkFormat format, VkImageAspectFlags aspect) {
      VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
      vi.image = image;
      vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
      vi.format = format;
      vi.subresourceRange = {aspect, 0, 1, 0, 1};
      VkImageView v = VK_NULL_HANDLE;
      check(vkCreateImageView(device_, &vi, nullptr, &v), "vkCreateImageView");
      return v;
    };
    depth_view_ = make_view(depth_image_, depth_format_, VK_IMAGE_ASPECT_DEPTH_BIT);
    for (VkImage image : images) {
      views_.push_back(make_view(image, surface_format_.format, VK_IMAGE_ASPECT_COLOR_BIT));
      const VkImageView att[2] = {views_.back(), depth_view_};
      VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
      fi.renderPass = render_pass_;
      fi.attachmentCount = 2;
      fi.pAttachments = att;
      fi.width = extent_.width;
      fi.height = extent_.height;
      fi.layers = 1;
      VkFramebuffer fb = VK_NULL_HANDLE;
      check(vkCreateFramebuffer(device_, &fi, nullptr, &fb), "vkCreateFramebuffer");
      framebuffers_.push_back(fb);
    }
  }

  SDL_Window* window_;
  FrameTiming timing_;
  std::uint64_t last_frame_ns_ = 0;
  VkInstance instance_ = VK_NULL_HANDLE;
  VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
  VkSurfaceKHR surface_ = VK_NULL_HANDLE;
  VkPhysicalDevice gpu_ = VK_NULL_HANDLE;
  std::string gpu_name_;
  std::uint32_t queue_family_ = 0;
  VkDevice device_ = VK_NULL_HANDLE;
  VkQueue queue_ = VK_NULL_HANDLE;
  VkSurfaceFormatKHR surface_format_{};
  VkFormat depth_format_ = VK_FORMAT_UNDEFINED;
  VkRenderPass render_pass_ = VK_NULL_HANDLE;
  VkPipelineLayout layout_ = VK_NULL_HANDLE;
  VkPipeline line_pipeline_ = VK_NULL_HANDLE;
  Buffer lines_;
  VkCommandPool pool_ = VK_NULL_HANDLE;
  VkCommandBuffer cmd_ = VK_NULL_HANDLE;
  VkSemaphore image_ready_ = VK_NULL_HANDLE, render_done_ = VK_NULL_HANDLE;
  VkFence frame_fence_ = VK_NULL_HANDLE;
  VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
  VkExtent2D extent_{0, 0};
  std::vector<VkImageView> views_;
  std::vector<VkFramebuffer> framebuffers_;
  VkImage depth_image_ = VK_NULL_HANDLE;
  VkDeviceMemory depth_memory_ = VK_NULL_HANDLE;
  VkImageView depth_view_ = VK_NULL_HANDLE;
  bool resize_ = false;
  VkSurfaceTransformFlagBitsKHR made_transform_ = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;  // (Android: the screen's turn when the swapchain was made)
  bool vsync_ = true;
  bool low_latency_ = false;
  bool surface_lost_ = false;
  std::uint32_t instance_api_ = VK_API_VERSION_1_0, image_count_ = 2;
  bool present_wait_ok_ = false;
  PFN_vkWaitForPresentKHR wait_for_present_ = nullptr;
  std::uint64_t present_id_ = 0, last_waited_ = 0;
  struct Pending {
    std::uint64_t id, start_ns;
  };
  std::deque<Pending> pending_;
  double lead_ms_ = 0;
  // Waits up to `timeout` ns for present `id`; on success every pending frame up to it is
  // on the screen, and the lead is updated from the newest of them.
  bool wait_presented(std::uint64_t id, std::uint64_t timeout) {
    if (id <= last_waited_) return true;
    const VkResult r = wait_for_present_(device_, swapchain_, id, timeout);
    if (r != VK_SUCCESS) return false;
    const std::uint64_t now = SDL_GetTicksNS();
    last_waited_ = id;
    std::uint64_t start = 0;
    while (!pending_.empty() && pending_.front().id <= id) start = pending_.front().start_ns, pending_.pop_front();
    if (start && now > start) {
      const double ms = double(now - start) * 1e-6;
      if (ms < 200.0) lead_ms_ = lead_ms_ > 0 ? lead_ms_ + (ms - lead_ms_) * 0.05 : ms;
    }
    return true;
  }
  void poll_presented() {
    while (!pending_.empty() && wait_presented(pending_.front().id, 0)) {
    }
    while (pending_.size() > 16) pending_.pop_front();  // (never shown, e.g. while minimised)
  }
  float shape_ = 0.0f;  // the picture's width / height; 0: the whole window
  VkPresentModeKHR present_mode_ = VK_PRESENT_MODE_FIFO_KHR;
};


Renderer::Renderer(SDL_Window* window) : impl_(std::make_unique<Impl>(window)) {}
Renderer::~Renderer() = default;
const char* Renderer::gpu_name() const { return impl_->gpu_name(); }
void Renderer::request_resize() { impl_->request_resize(); }
float Renderer::aspect() const { return impl_->aspect(); }
int Renderer::height() const { return impl_->height(); }
void Renderer::draw(const std::vector<LineInstance>& lines, const Mat4& view, float fov_y, float line_width) {
  impl_->draw({Layer{&lines, view, fov_y}}, line_width);
}
void Renderer::draw(const std::vector<Layer>& layers, float line_width) {
  for (int c = 0; c < 3; ++c) impl_->background[c] = background_[c];
  impl_->draw(layers, line_width);
}
FrameTiming Renderer::timing() const { return impl_->timing(); }
void Renderer::set_vsync(bool on) { impl_->set_vsync(on); }
void Renderer::set_picture_aspect(float aspect) { impl_->set_shape(aspect); }
void Renderer::set_low_latency(bool on) { impl_->set_low_latency(on); }
void Renderer::surface_changed() { impl_->surface_changed(); }
double Renderer::display_lead_ms() const { return impl_->display_lead_ms(); }
bool Renderer::lead_measured() const { return impl_->lead_measured(); }

}  // namespace vibr
