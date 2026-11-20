#include "xr_display.h"

#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>

#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace vibr {

namespace {

const std::uint32_t kLineVert[] =
#include "line.vert.inc"
    ;
const std::uint32_t kLineFrag[] =
#include "line.frag.inc"
    ;

struct PushConstants {  // as line.vert and line.frag declare it
  Mat4 mvp;
  float viewport[2];
  float width;
  float feather;
};

constexpr std::uint32_t kMaxLines = 32768;

void vk_check(VkResult r, const char* what) {
  if (r != VK_SUCCESS) throw std::runtime_error(std::string(what) + " failed with VkResult " + std::to_string(int(r)));
}

// The line colours are display values (as a monitor shows them); a headset's
// compositor takes sRGB images as light, so they go in decoded and come out as authored.
float to_linear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }

// The view matrix of an eye: the inverse of its pose.
Mat4 view_from_pose(const XrPosef& p) {
  const float x = p.orientation.x, y = p.orientation.y, z = p.orientation.z, w = p.orientation.w;
  // Rotation of the pose, column-major.
  const float r[9] = {1 - 2 * (y * y + z * z), 2 * (x * y + z * w),     2 * (x * z - y * w),
                      2 * (x * y - z * w),     1 - 2 * (x * x + z * z), 2 * (y * z + x * w),
                      2 * (x * z + y * w),     2 * (y * z - x * w),     1 - 2 * (x * x + y * y)};
  // Inverse: transpose the rotation, rotate the position back.
  Mat4 v;
  for (int c = 0; c < 3; ++c)
    for (int row = 0; row < 3; ++row) v.m[c * 4 + row] = r[row * 3 + c];
  const float px = p.position.x, py = p.position.y, pz = p.position.z;
  for (int row = 0; row < 3; ++row) v.m[12 + row] = -(v.m[row] * px + v.m[4 + row] * py + v.m[8 + row] * pz);
  return v;
}

// An eye's projection for Vulkan (clip Y down, depth 0..1) from its four angles.
Mat4 projection(const XrFovf& fov, float z_near, float z_far) {
  const float l = std::tan(fov.angleLeft), r = std::tan(fov.angleRight);
  const float u = std::tan(fov.angleUp), d = std::tan(fov.angleDown);
  const float w = r - l, h = d - u;
  Mat4 m;
  m.m[0] = 2 / w;
  m.m[5] = 2 / h;
  m.m[8] = (r + l) / w;
  m.m[9] = (u + d) / h;
  m.m[10] = -z_far / (z_far - z_near);
  m.m[11] = -1;
  m.m[14] = -(z_far * z_near) / (z_far - z_near);
  m.m[15] = 0;
  return m;
}

Mat4 scaling(float x, float y, float z) {
  Mat4 m;
  m.m[0] = x;
  m.m[5] = y;
  m.m[10] = z;
  return m;
}

// The narrowest camera of play (25 degrees). A narrower one (the intro's first
// shot is a 3 degree telephoto from 23 m) would look tiny and far off at the
// headset's own field of view, so its scene comes closer by the ratio of the two,
// framed as the original framed it.
// How far the zoom goes: 4 times closer, 24 times further away than at the start.
constexpr float kZoomIn = 4.0f, kZoomOut = 1.0f / 24.0f;

constexpr float kNarrowestFov = 25.0f * 3.14159265f / 180.0f;

float depth_for(float fov_y) {
  const float k = std::tan(0.5f * fov_y) / std::tan(0.5f * kNarrowestFov);
  return std::clamp(k, 0.02f, 1.0f);
}

}  // namespace

class XrDisplay::Impl {
 public:
  explicit Impl(float scale) : scale_(scale) {
    create_instance();
    create_vulkan();
    create_session();
    create_swapchains();
    create_pipeline();
    create_actions();
  }

  ~Impl() {
    // Ask the runtime to end the session and let it, before taking it apart (a
    // change of language starts a new one straight after). Never throws.
    tearing_down_ = true;  // (its end is ours: not the player closing the game)
    if (session_ && running_) {
      try {
        xrRequestExitSession(session_);
        for (int i = 0; i < 120 && running_; ++i) {
          poll_events();
          if (!running_) break;
          XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};
          XrFrameState state{XR_TYPE_FRAME_STATE};
          if (XR_FAILED(xrWaitFrame(session_, &wait, &state))) break;
          XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
          if (XR_FAILED(xrBeginFrame(session_, &begin))) break;
          XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
          end.displayTime = state.predictedDisplayTime;
          end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
          if (XR_FAILED(xrEndFrame(session_, &end))) break;
        }
      } catch (...) {
      }
    }
    if (device_) vkDeviceWaitIdle(device_);
    for (Eye& e : eyes_) {
      for (VkFramebuffer f : e.framebuffers) vkDestroyFramebuffer(device_, f, nullptr);
      for (VkImageView v : e.views) vkDestroyImageView(device_, v, nullptr);
      if (e.swapchain) xrDestroySwapchain(e.swapchain);
    }
    if (pipeline_) vkDestroyPipeline(device_, pipeline_, nullptr);
    if (layout_) vkDestroyPipelineLayout(device_, layout_, nullptr);
    if (render_pass_) vkDestroyRenderPass(device_, render_pass_, nullptr);
    if (lines_) vkDestroyBuffer(device_, lines_, nullptr);
    if (lines_memory_) vkFreeMemory(device_, lines_memory_, nullptr);
    if (fence_) vkDestroyFence(device_, fence_, nullptr);
    if (pool_) vkDestroyCommandPool(device_, pool_, nullptr);
    if (head_) xrDestroySpace(head_);
    if (space_) xrDestroySpace(space_);
    if (actions_) xrDestroyActionSet(actions_);
    if (session_) xrDestroySession(session_);
    if (device_) vkDestroyDevice(device_, nullptr);
    if (vk_instance_) vkDestroyInstance(vk_instance_, nullptr);
    if (instance_) xrDestroyInstance(instance_);
  }

  const char* gpu_name() const { return gpu_name_.c_str(); }
  const char* runtime_name() const { return runtime_name_.c_str(); }
  int height() const { return eyes_[0].height; }
  void recentre() { recentre_ = true; }
  // In a moment: a head just arrived moves before it settles.
  void recentre_soon() { recentre_at_ns_ = SDL_GetTicksNS() + 700'000'000; }
  float zoom() const { return zoom_; }
  void set_zoom(float z) { zoom_ = std::clamp(z, kZoomOut, kZoomIn); }
  float scale() const { return scale_; }
  void set_scale(float s) { scale_ = s; }

  void draw(const std::vector<Layer>& layers, float line_width) {
    poll_events();
    if (!running_) {
      SDL_Delay(10);  // the headset is not showing us yet
      return;
    }
    XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState state{XR_TYPE_FRAME_STATE};
    xr_check(xrWaitFrame(session_, &wait, &state), "xrWaitFrame");
    XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
    xr_check(xrBeginFrame(session_, &begin), "xrBeginFrame");
    poll_actions();

    std::array<XrCompositionLayerProjectionView, 2> projected{};
    XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    bool drawn = false;
    if (state.shouldRender) {
      XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};
      locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
      locate.displayTime = state.predictedDisplayTime;
      locate.space = space_;
      XrViewState view_state{XR_TYPE_VIEW_STATE};
      std::array<XrView, 2> views{};
      for (XrView& v : views) v.type = XR_TYPE_VIEW;
      std::uint32_t count = 0;
      xr_check(xrLocateViews(session_, &locate, &view_state, 2, &count, views.data()), "xrLocateViews");
      if (recentre_at_ns_ && SDL_GetTicksNS() >= recentre_at_ns_) {
        recentre_at_ns_ = 0;
        recentre_ = true;
      }
      if (recentre_) place_scene(state.predictedDisplayTime);
      if ((view_state.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) &&
          (view_state.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) {
        render(layers, line_width, views, projected);
        layer.space = space_;
        layer.viewCount = 2;
        layer.views = projected.data();
        drawn = true;
      }
    }
    const XrCompositionLayerBaseHeader* submitted[] = {reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer)};
    XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
    end.displayTime = state.predictedDisplayTime;
    end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    end.layerCount = drawn ? 1 : 0;
    end.layers = submitted;
    xr_check(xrEndFrame(session_, &end), "xrEndFrame");
  }

 private:
  struct Eye {
    XrSwapchain swapchain = XR_NULL_HANDLE;
    int width = 0, height = 0;
    std::vector<VkImageView> views;
    std::vector<VkFramebuffer> framebuffers;
  };
  // A controller button and the gamepad button it stands for.
  struct Button {
    XrAction action = XR_NULL_HANDLE;
    SDL_GamepadButton pad;
    bool down = false;
  };

  void xr_check(XrResult r, const char* what) {
    if (XR_FAILED(r)) {
      char text[XR_MAX_RESULT_STRING_SIZE] = "";
      if (instance_) xrResultToString(instance_, r, text);
      throw std::runtime_error(std::string(what) + " failed: " + (text[0] ? text : std::to_string(int(r))));
    }
  }

  template <typename F>
  F xr_function(const char* name) {
    PFN_xrVoidFunction f = nullptr;
    xr_check(xrGetInstanceProcAddr(instance_, name, &f), name);
    return reinterpret_cast<F>(f);
  }

  void create_instance() {
    // Vulkan, and (where the runtime has it) word of the headset being put on.
    std::uint32_t n = 0;
    xrEnumerateInstanceExtensionProperties(nullptr, 0, &n, nullptr);
    std::vector<XrExtensionProperties> available(n, {XR_TYPE_EXTENSION_PROPERTIES});
    xrEnumerateInstanceExtensionProperties(nullptr, n, &n, available.data());
    std::vector<const char*> exts = {XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME};
    for (const auto& e : available)
      if (std::strcmp(e.extensionName, XR_EXT_USER_PRESENCE_EXTENSION_NAME) == 0) {
        exts.push_back(XR_EXT_USER_PRESENCE_EXTENSION_NAME);
        presence_ = true;
      }
    XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};
    std::strncpy(info.applicationInfo.applicationName, "Vib-Ribbon VR", XR_MAX_APPLICATION_NAME_SIZE - 1);
    info.applicationInfo.applicationVersion = 1;
    std::strncpy(info.applicationInfo.engineName, "vibcore", XR_MAX_ENGINE_NAME_SIZE - 1);
    info.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    info.enabledExtensionCount = std::uint32_t(exts.size());
    info.enabledExtensionNames = exts.data();
    const XrResult r = xrCreateInstance(&info, &instance_);
    if (XR_FAILED(r))
      throw std::runtime_error(
          "no OpenXR runtime answered (is the headset connected, and Meta Quest Link or SteamVR set as the OpenXR runtime?)");
    XrInstanceProperties props{XR_TYPE_INSTANCE_PROPERTIES};
    xrGetInstanceProperties(instance_, &props);
    runtime_name_ = props.runtimeName;
    std::printf("Headset wear sensor: %s\n", presence_ ? "yes (recentres when the headset is put on)" : "not offered by this runtime");
    XrSystemGetInfo sys{XR_TYPE_SYSTEM_GET_INFO};
    sys.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    xr_check(xrGetSystem(instance_, &sys, &system_), "xrGetSystem (is the headset on and connected?)");
  }

  void create_vulkan() {
    auto requirements = xr_function<PFN_xrGetVulkanGraphicsRequirements2KHR>("xrGetVulkanGraphicsRequirements2KHR");
    auto create_instance = xr_function<PFN_xrCreateVulkanInstanceKHR>("xrCreateVulkanInstanceKHR");
    auto get_device = xr_function<PFN_xrGetVulkanGraphicsDevice2KHR>("xrGetVulkanGraphicsDevice2KHR");
    auto create_device = xr_function<PFN_xrCreateVulkanDeviceKHR>("xrCreateVulkanDeviceKHR");
    XrGraphicsRequirementsVulkan2KHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR};
    xr_check(requirements(instance_, system_, &req), "xrGetVulkanGraphicsRequirements2KHR");

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Vib-Ribbon VR";
    app.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo vi{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    vi.pApplicationInfo = &app;
    XrVulkanInstanceCreateInfoKHR xi{XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};
    xi.systemId = system_;
    xi.pfnGetInstanceProcAddr = &vkGetInstanceProcAddr;
    xi.vulkanCreateInfo = &vi;
    VkResult vr = VK_SUCCESS;
    xr_check(create_instance(instance_, &xi, &vk_instance_, &vr), "xrCreateVulkanInstanceKHR");
    vk_check(vr, "vkCreateInstance (through OpenXR)");

    XrVulkanGraphicsDeviceGetInfoKHR gi{XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};
    gi.systemId = system_;
    gi.vulkanInstance = vk_instance_;
    xr_check(get_device(instance_, &gi, &gpu_), "xrGetVulkanGraphicsDevice2KHR");
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(gpu_, &props);
    gpu_name_ = props.deviceName;

    std::uint32_t qcount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu_, &qcount, nullptr);
    std::vector<VkQueueFamilyProperties> families(qcount);
    vkGetPhysicalDeviceQueueFamilyProperties(gpu_, &qcount, families.data());
    queue_family_ = UINT32_MAX;
    for (std::uint32_t q = 0; q < qcount; ++q)
      if (families[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
        queue_family_ = q;
        break;
      }
    if (queue_family_ == UINT32_MAX) throw std::runtime_error("the headset's GPU has no graphics queue");
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qi.queueFamilyIndex = queue_family_;
    qi.queueCount = 1;
    qi.pQueuePriorities = &priority;
    VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    di.queueCreateInfoCount = 1;
    di.pQueueCreateInfos = &qi;
    XrVulkanDeviceCreateInfoKHR xd{XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};
    xd.systemId = system_;
    xd.pfnGetInstanceProcAddr = &vkGetInstanceProcAddr;
    xd.vulkanPhysicalDevice = gpu_;
    xd.vulkanCreateInfo = &di;
    xr_check(create_device(instance_, &xd, &device_, &vr), "xrCreateVulkanDeviceKHR");
    vk_check(vr, "vkCreateDevice (through OpenXR)");
    vkGetDeviceQueue(device_, queue_family_, 0, &queue_);

    VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pi.queueFamilyIndex = queue_family_;
    vk_check(vkCreateCommandPool(device_, &pi, nullptr, &pool_), "vkCreateCommandPool");
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = pool_;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    vk_check(vkAllocateCommandBuffers(device_, &ai, &cmd_), "vkAllocateCommandBuffers");
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vk_check(vkCreateFence(device_, &fi, nullptr, &fence_), "vkCreateFence");

    // The lines of a frame, written by the CPU and read by the GPU.
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = sizeof(LineInstance) * kMaxLines;
    bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    vk_check(vkCreateBuffer(device_, &bi, nullptr, &lines_), "vkCreateBuffer");
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(device_, lines_, &mr);
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(gpu_, &mp);
    const VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    std::uint32_t type = UINT32_MAX;
    for (std::uint32_t i = 0; i < mp.memoryTypeCount; ++i)
      if ((mr.memoryTypeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) {
        type = i;
        break;
      }
    if (type == UINT32_MAX) throw std::runtime_error("no suitable GPU memory type");
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = mr.size;
    alloc.memoryTypeIndex = type;
    vk_check(vkAllocateMemory(device_, &alloc, nullptr, &lines_memory_), "vkAllocateMemory");
    vk_check(vkBindBufferMemory(device_, lines_, lines_memory_, 0), "vkBindBufferMemory");
    vk_check(vkMapMemory(device_, lines_memory_, 0, bi.size, 0, &mapped_), "vkMapMemory");
  }

  void create_session() {
    XrGraphicsBindingVulkan2KHR binding{XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR};
    binding.instance = vk_instance_;
    binding.physicalDevice = gpu_;
    binding.device = device_;
    binding.queueFamilyIndex = queue_family_;
    binding.queueIndex = 0;
    XrSessionCreateInfo info{XR_TYPE_SESSION_CREATE_INFO};
    info.next = &binding;
    info.systemId = system_;
    xr_check(xrCreateSession(instance_, &info, &session_), "xrCreateSession");
    // Seated play: the starting head position is the origin (recentre with the headset's own button).
    XrReferenceSpaceCreateInfo space{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    space.poseInReferenceSpace.orientation.w = 1;
    xr_check(xrCreateReferenceSpace(session_, &space, &space_), "xrCreateReferenceSpace");
    space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    xr_check(xrCreateReferenceSpace(session_, &space, &head_), "xrCreateReferenceSpace (view)");
  }

  // The scene's place: where the head was at the last recentre, turned only about
  // the vertical so the scene stays upright.
  void place_scene(XrTime time) {
    XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
    if (XR_FAILED(xrLocateSpace(head_, space_, time, &loc))) return;
    const XrSpaceLocationFlags need = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    if ((loc.locationFlags & need) != need) return;
    const XrQuaternionf& q = loc.pose.orientation;
    // Where the head looks: its -z axis.
    const float fx = -2 * (q.x * q.z + q.y * q.w), fz = -(1 - 2 * (q.x * q.x + q.y * q.y));
    const float yaw = std::atan2(-fx, -fz);
    const float c = std::cos(yaw), s = std::sin(yaw);
    Mat4 m;
    m.m[0] = c;
    m.m[2] = -s;
    m.m[8] = s;
    m.m[10] = c;
    m.m[12] = loc.pose.position.x;
    m.m[13] = loc.pose.position.y;
    m.m[14] = loc.pose.position.z;
    scene_place_ = m;
    recentre_ = false;
    std::printf("Recentred\n");
  }

  void create_swapchains() {
    // sRGB images: the compositor reads them as display values (see to_linear).
    std::uint32_t count = 0;
    xr_check(xrEnumerateSwapchainFormats(session_, 0, &count, nullptr), "xrEnumerateSwapchainFormats");
    std::vector<std::int64_t> formats(count);
    xr_check(xrEnumerateSwapchainFormats(session_, count, &count, formats.data()), "xrEnumerateSwapchainFormats");
    format_ = VK_FORMAT_UNDEFINED;
    for (VkFormat want : {VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB})
      if (std::find(formats.begin(), formats.end(), std::int64_t(want)) != formats.end()) {
        format_ = want;
        break;
      }
    if (format_ == VK_FORMAT_UNDEFINED) {
      if (formats.empty()) throw std::runtime_error("the headset offers no image formats");
      format_ = VkFormat(formats[0]);
      linear_colours_ = false;  // not an sRGB format: write the colours as they are
    }
    create_render_pass();

    std::uint32_t nviews = 0;
    xr_check(xrEnumerateViewConfigurationViews(instance_, system_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &nviews, nullptr),
             "xrEnumerateViewConfigurationViews");
    std::vector<XrViewConfigurationView> configs(nviews, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
    xr_check(xrEnumerateViewConfigurationViews(instance_, system_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, nviews, &nviews,
                                               configs.data()),
             "xrEnumerateViewConfigurationViews");
    if (nviews != 2) throw std::runtime_error("the headset does not show two views");
    for (std::size_t i = 0; i < 2; ++i) {
      Eye& e = eyes_[i];
      e.width = int(configs[i].recommendedImageRectWidth);
      e.height = int(configs[i].recommendedImageRectHeight);
      XrSwapchainCreateInfo sc{XR_TYPE_SWAPCHAIN_CREATE_INFO};
      sc.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
      sc.format = format_;
      sc.sampleCount = 1;
      sc.width = std::uint32_t(e.width);
      sc.height = std::uint32_t(e.height);
      sc.faceCount = 1;
      sc.arraySize = 1;
      sc.mipCount = 1;
      xr_check(xrCreateSwapchain(session_, &sc, &e.swapchain), "xrCreateSwapchain");
      std::uint32_t n = 0;
      xr_check(xrEnumerateSwapchainImages(e.swapchain, 0, &n, nullptr), "xrEnumerateSwapchainImages");
      std::vector<XrSwapchainImageVulkan2KHR> images(n, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR});
      xr_check(xrEnumerateSwapchainImages(e.swapchain, n, &n, reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())),
               "xrEnumerateSwapchainImages");
      for (const auto& img : images) {
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = img.image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = format_;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView view = VK_NULL_HANDLE;
        vk_check(vkCreateImageView(device_, &vi, nullptr, &view), "vkCreateImageView");
        e.views.push_back(view);
        VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fi.renderPass = render_pass_;
        fi.attachmentCount = 1;
        fi.pAttachments = &view;
        fi.width = std::uint32_t(e.width);
        fi.height = std::uint32_t(e.height);
        fi.layers = 1;
        VkFramebuffer fb = VK_NULL_HANDLE;
        vk_check(vkCreateFramebuffer(device_, &fi, nullptr, &fb), "vkCreateFramebuffer");
        e.framebuffers.push_back(fb);
      }
    }
  }

  void create_render_pass() {
    // Lines only, drawn in order, no depth (the desktop's lines do not write it either).
    VkAttachmentDescription att{};
    att.format = format_;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;  // as OpenXR takes them back
    const VkAttachmentReference colour{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &colour;
    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    info.attachmentCount = 1;
    info.pAttachments = &att;
    info.subpassCount = 1;
    info.pSubpasses = &sub;
    info.dependencyCount = 1;
    info.pDependencies = &dep;
    vk_check(vkCreateRenderPass(device_, &info, nullptr, &render_pass_), "vkCreateRenderPass");
  }

  VkShaderModule shader(const std::uint32_t* code, std::size_t bytes) {
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = bytes;
    info.pCode = code;
    VkShaderModule m = VK_NULL_HANDLE;
    vk_check(vkCreateShaderModule(device_, &info, nullptr, &m), "vkCreateShaderModule");
    return m;
  }

  void create_pipeline() {
    // The desktop renderer's line pipeline: instanced quads, alpha-blended feathered edges.
    const VkPushConstantRange range{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants)};
    VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    li.pushConstantRangeCount = 1;
    li.pPushConstantRanges = &range;
    vk_check(vkCreatePipelineLayout(device_, &li, nullptr, &layout_), "vkCreatePipelineLayout");
    const VkShaderModule vert = shader(kLineVert, sizeof kLineVert), frag = shader(kLineFrag, sizeof kLineFrag);
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";
    const VkVertexInputBindingDescription binding{0, sizeof(LineInstance), VK_VERTEX_INPUT_RATE_INSTANCE};
    const VkVertexInputAttributeDescription attrs[] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(LineInstance, a)},
                                                       {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(LineInstance, b)},
                                                       {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(LineInstance, colour)}};
    VkPipelineVertexInputStateCreateInfo vin{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vin.vertexBindingDescriptionCount = 1;
    vin.pVertexBindingDescriptions = &binding;
    vin.vertexAttributeDescriptionCount = 3;
    vin.pVertexAttributeDescriptions = attrs;
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
    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cba.blendEnable = VK_TRUE;
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
    info.pColorBlendState = &cb;
    info.pDynamicState = &dyn;
    info.layout = layout_;
    info.renderPass = render_pass_;
    vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline_), "vkCreateGraphicsPipelines");
    vkDestroyShaderModule(device_, vert, nullptr);
    vkDestroyShaderModule(device_, frag, nullptr);
  }

  XrPath path(const char* p) {
    XrPath out = XR_NULL_PATH;
    xr_check(xrStringToPath(instance_, p, &out), p);
    return out;
  }

  void create_actions() {
    XrActionSetCreateInfo set{XR_TYPE_ACTION_SET_CREATE_INFO};
    std::strncpy(set.actionSetName, "play", XR_MAX_ACTION_SET_NAME_SIZE - 1);
    std::strncpy(set.localizedActionSetName, "Play", XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE - 1);
    xr_check(xrCreateActionSet(instance_, &set, &actions_), "xrCreateActionSet");
    auto action = [&](const char* name, const char* label, XrActionType type) {
      XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
      info.actionType = type;
      std::strncpy(info.actionName, name, XR_MAX_ACTION_NAME_SIZE - 1);
      std::strncpy(info.localizedActionName, label, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
      XrAction a = XR_NULL_HANDLE;
      xr_check(xrCreateAction(actions_, &info, &a), name);
      return a;
    };
    buttons_ = {Button{action("block", "Block", XR_ACTION_TYPE_BOOLEAN_INPUT), SDL_GAMEPAD_BUTTON_LEFT_SHOULDER},
                Button{action("loop", "Loop", XR_ACTION_TYPE_BOOLEAN_INPUT), SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
                Button{action("pit", "Pit", XR_ACTION_TYPE_BOOLEAN_INPUT), SDL_GAMEPAD_BUTTON_DPAD_DOWN},
                Button{action("wave", "Wave and confirm", XR_ACTION_TYPE_BOOLEAN_INPUT), SDL_GAMEPAD_BUTTON_SOUTH},
                Button{action("back", "Back", XR_ACTION_TYPE_BOOLEAN_INPUT), SDL_GAMEPAD_BUTTON_EAST},
                Button{action("start", "Start", XR_ACTION_TYPE_BOOLEAN_INPUT), SDL_GAMEPAD_BUTTON_START}};
    stick_ = action("menu_stick", "Menu direction", XR_ACTION_TYPE_VECTOR2F_INPUT);
    recentre_action_ = action("recentre", "Recentre the view", XR_ACTION_TYPE_BOOLEAN_INPUT);
    zoom_stick_ = action("zoom", "Zoom (testing)", XR_ACTION_TYPE_FLOAT_INPUT);


    // Touch controllers (Quest), and the plainest controller any runtime offers.
    std::vector<XrActionSuggestedBinding> touch = {
        {buttons_[0].action, path("/user/hand/left/input/trigger/value")},
        {buttons_[1].action, path("/user/hand/right/input/trigger/value")},
        {buttons_[2].action, path("/user/hand/left/input/x/click")},
        {buttons_[2].action, path("/user/hand/left/input/y/click")},
        {buttons_[3].action, path("/user/hand/right/input/a/click")},
        {buttons_[4].action, path("/user/hand/right/input/b/click")},
        {buttons_[5].action, path("/user/hand/left/input/menu/click")},
        {stick_, path("/user/hand/left/input/thumbstick")},
        {recentre_action_, path("/user/hand/right/input/thumbstick/click")},
        {zoom_stick_, path("/user/hand/right/input/thumbstick/y")},

    };
    XrInteractionProfileSuggestedBinding sb{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    sb.interactionProfile = path("/interaction_profiles/oculus/touch_controller");
    sb.suggestedBindings = touch.data();
    sb.countSuggestedBindings = std::uint32_t(touch.size());
    xr_check(xrSuggestInteractionProfileBindings(instance_, &sb), "xrSuggestInteractionProfileBindings (Touch)");
    std::vector<XrActionSuggestedBinding> simple = {
        {buttons_[3].action, path("/user/hand/right/input/select/click")},
        {buttons_[5].action, path("/user/hand/left/input/menu/click")},
    };
    sb.interactionProfile = path("/interaction_profiles/khr/simple_controller");
    sb.suggestedBindings = simple.data();
    sb.countSuggestedBindings = std::uint32_t(simple.size());
    xr_check(xrSuggestInteractionProfileBindings(instance_, &sb), "xrSuggestInteractionProfileBindings (simple)");

    XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attach.countActionSets = 1;
    attach.actionSets = &actions_;
    xr_check(xrAttachSessionActionSets(session_, &attach), "xrAttachSessionActionSets");
  }

  static void push_button(SDL_GamepadButton b, bool down) {
    SDL_Event e{};
    e.type = down ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
    e.gbutton.which = 0;
    e.gbutton.button = Uint8(b);
    e.gbutton.down = down;
    // Timestamped now: within a frame of the press (the controllers' own times later).
    e.gbutton.timestamp = SDL_GetTicksNS();
    SDL_PushEvent(&e);
  }

  void poll_actions() {
    if (!focused_) return;
    XrActiveActionSet active{actions_, XR_NULL_PATH};
    XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
    sync.countActiveActionSets = 1;
    sync.activeActionSets = &active;
    if (XR_FAILED(xrSyncActions(session_, &sync))) return;
    for (Button& b : buttons_) {
      XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
      gi.action = b.action;
      XrActionStateBoolean st{XR_TYPE_ACTION_STATE_BOOLEAN};
      if (XR_FAILED(xrGetActionStateBoolean(session_, &gi, &st))) continue;
      const bool down = st.isActive && st.currentState;
      if (down != b.down) push_button(b.pad, down);
      b.down = down;
    }
    // R3: recentre.
    {
      XrActionStateGetInfo ri{XR_TYPE_ACTION_STATE_GET_INFO};
      ri.action = recentre_action_;
      XrActionStateBoolean rs{XR_TYPE_ACTION_STATE_BOOLEAN};
      if (XR_SUCCEEDED(xrGetActionStateBoolean(session_, &ri, &rs)) && rs.isActive && rs.changedSinceLastSync && rs.currentState)
        recentre_ = true;
    }
    // The right stick held up or down: zoom in or out, smoothly (twice as close
    // or as far for each second at full tilt), handled here rather than through
    // the game's events.
    {
      const std::uint64_t now = SDL_GetTicksNS();
      const float seconds = zoom_ns_ ? float(double(now - zoom_ns_) * 1e-9) : 0.0f;
      zoom_ns_ = now;
      // The Touch right stick's y (a gamepad's right stick is read by the game).
      XrActionStateGetInfo zi{XR_TYPE_ACTION_STATE_GET_INFO};
      zi.action = zoom_stick_;
      XrActionStateFloat zs{XR_TYPE_ACTION_STATE_FLOAT};
      const float y = XR_SUCCEEDED(xrGetActionStateFloat(session_, &zi, &zs)) && zs.isActive ? zs.currentState : 0.0f;
      if (std::abs(y) > 0.2f) {
        const float t = (std::abs(y) - 0.2f) / 0.8f * (y > 0 ? 1.0f : -1.0f);
        zoom_ = std::clamp(zoom_ * std::exp2(t * std::min(seconds, 0.1f)), kZoomOut, kZoomIn);
        zooming_ = true;
      } else if (zooming_) {
        zooming_ = false;
        std::printf("Zoom %.2f\n", double(zoom_));
      }
    }
    // The stick as the d-pad, with a little hysteresis.
    XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
    gi.action = stick_;
    XrActionStateVector2f st{XR_TYPE_ACTION_STATE_VECTOR2F};
    if (XR_FAILED(xrGetActionStateVector2f(session_, &gi, &st)) || !st.isActive) st.currentState = {0, 0};
    const float values[4] = {st.currentState.y, -st.currentState.y, -st.currentState.x, st.currentState.x};
    const SDL_GamepadButton dirs[4] = {SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT,
                                       SDL_GAMEPAD_BUTTON_DPAD_RIGHT};
    for (int i = 0; i < 4; ++i) {
      const bool down = stick_down_[i] ? values[i] > 0.4f : values[i] > 0.7f;
      if (down != stick_down_[i]) push_button(dirs[i], down);
      stick_down_[i] = down;
    }
  }

  void poll_events() {
    XrEventDataBuffer ev{XR_TYPE_EVENT_DATA_BUFFER};
    while (xrPollEvent(instance_, &ev) == XR_SUCCESS) {
      if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
        const auto& sc = *reinterpret_cast<XrEventDataSessionStateChanged*>(&ev);
        const bool was_focused = focused_;
        focused_ = sc.state == XR_SESSION_STATE_FOCUSED;
        if (focused_ && !was_focused) recentre_soon();  // the player has the game in front of them
        switch (sc.state) {
          case XR_SESSION_STATE_READY: {
            XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO};
            bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            xr_check(xrBeginSession(session_, &bi), "xrBeginSession");
            running_ = true;
            std::printf("Headset: showing the game\n");
            break;
          }
          case XR_SESSION_STATE_STOPPING:
            xrEndSession(session_);
            running_ = false;
            break;
          case XR_SESSION_STATE_EXITING:
          case XR_SESSION_STATE_LOSS_PENDING: {
            running_ = false;
            if (!tearing_down_) {
              SDL_Event quit{};
              quit.type = SDL_EVENT_QUIT;
              SDL_PushEvent(&quit);
            }
            break;
          }
          default: break;
        }
      } else if (ev.type == XR_TYPE_EVENT_DATA_USER_PRESENCE_CHANGED_EXT) {
        // The headset has just been put on: recentre once it has settled on the head.
        if (reinterpret_cast<XrEventDataUserPresenceChangedEXT*>(&ev)->isUserPresent) recentre_soon();
      } else if (ev.type == XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED) {
        // Which controllers the runtime says are in use (for the console, while the input is tested).
        for (const char* hand : {"/user/hand/left", "/user/hand/right"}) {
          XrInteractionProfileState st{XR_TYPE_INTERACTION_PROFILE_STATE};
          if (XR_FAILED(xrGetCurrentInteractionProfile(session_, path(hand), &st))) continue;
          char name[XR_MAX_PATH_LENGTH] = "none";
          std::uint32_t n = 0;
          if (st.interactionProfile != XR_NULL_PATH) xrPathToString(instance_, st.interactionProfile, sizeof name, &n, name);
          std::printf("Controllers: %s uses %s\n", hand, name);
        }
      } else if (ev.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
        SDL_Event quit{};
        quit.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&quit);
      }
      ev = {XR_TYPE_EVENT_DATA_BUFFER};
    }
  }

  void render(const std::vector<Layer>& layers, float line_width, const std::array<XrView, 2>& views,
              std::array<XrCompositionLayerProjectionView, 2>& projected) {
    vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX);
    vkResetFences(device_, 1, &fence_);
    // Every layer's lines into the one buffer, the colours as the headset wants them.
    std::vector<std::uint32_t> firsts, counts;
    std::uint32_t used = 0;
    auto* out = static_cast<LineInstance*>(mapped_);
    for (const Layer& layer : layers) {
      const std::uint32_t n = layer.lines ? std::min<std::uint32_t>(std::uint32_t(layer.lines->size()), kMaxLines - used) : 0;
      for (std::uint32_t i = 0; i < n; ++i) {
        LineInstance l = (*layer.lines)[i];
        if (linear_colours_)
          for (int c = 0; c < 3; ++c) l.colour[c] = to_linear(l.colour[c]);
        out[used + i] = l;
      }
      firsts.push_back(used);
      counts.push_back(n);
      used += n;
    }

    std::array<std::uint32_t, 2> image{};
    for (std::size_t e = 0; e < 2; ++e) {
      XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
      xr_check(xrAcquireSwapchainImage(eyes_[e].swapchain, &ai, &image[e]), "xrAcquireSwapchainImage");
      XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
      wi.timeout = XR_INFINITE_DURATION;
      xr_check(xrWaitSwapchainImage(eyes_[e].swapchain, &wi), "xrWaitSwapchainImage");
    }

    vkResetCommandBuffer(cmd_, 0);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vk_check(vkBeginCommandBuffer(cmd_, &begin), "vkBeginCommandBuffer");

    for (std::size_t e = 0; e < 2; ++e) {
      const Eye& eye = eyes_[e];
      VkClearValue clear{};
      clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
      VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
      rp.renderPass = render_pass_;
      rp.framebuffer = eye.framebuffers[image[e]];
      rp.renderArea.extent = {std::uint32_t(eye.width), std::uint32_t(eye.height)};
      rp.clearValueCount = 1;
      rp.pClearValues = &clear;
      vkCmdBeginRenderPass(cmd_, &rp, VK_SUBPASS_CONTENTS_INLINE);
      const VkViewport vp{0, 0, float(eye.width), float(eye.height), 0, 1};
      const VkRect2D scissor{{0, 0}, rp.renderArea.extent};
      vkCmdSetViewport(cmd_, 0, 1, &vp);
      vkCmdSetScissor(cmd_, 0, 1, &scissor);
      vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
      const VkDeviceSize zero = 0;
      vkCmdBindVertexBuffers(cmd_, 0, 1, &lines_, &zero);
      // Each layer's camera stands at the headset's starting place, in metres; the eye looks from where it is now.
      const Mat4 eye_view = mul(mul(projection(views[e].fov, 0.05f, 2000.0f), view_from_pose(views[e].pose)), scene_place_);
      for (std::size_t i = 0; i < layers.size(); ++i) {
        if (!counts[i]) continue;
        PushConstants pc;
        pc.mvp = mul(eye_view, mul(scaling(scale_, scale_, scale_ * depth_for(layers[i].fov_y)), layers[i].view));
        pc.viewport[0] = float(eye.width);
        pc.viewport[1] = float(eye.height);
        pc.width = line_width;
        pc.feather = 0.75f;
        vkCmdPushConstants(cmd_, layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof pc, &pc);
        vkCmdDraw(cmd_, 6, counts[i], 0, firsts[i]);
      }
      vkCmdEndRenderPass(cmd_);
      projected[e] = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
      projected[e].pose = views[e].pose;
      projected[e].fov = views[e].fov;
      projected[e].subImage.swapchain = eye.swapchain;
      projected[e].subImage.imageRect.extent = {eye.width, eye.height};
    }
    vk_check(vkEndCommandBuffer(cmd_), "vkEndCommandBuffer");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd_;
    vk_check(vkQueueSubmit(queue_, 1, &submit, fence_), "vkQueueSubmit");
    for (const Eye& eye : eyes_) {
      XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
      xr_check(xrReleaseSwapchainImage(eye.swapchain, &ri), "xrReleaseSwapchainImage");
    }
  }

  float scale_;
  XrInstance instance_ = XR_NULL_HANDLE;
  XrSystemId system_ = XR_NULL_SYSTEM_ID;
  XrSession session_ = XR_NULL_HANDLE;
  XrSpace space_ = XR_NULL_HANDLE, head_ = XR_NULL_HANDLE;
  Mat4 scene_place_;  // the scene in the headset's starting space (identity until recentred)
  // Recentred as soon as the headset is tracked at start (after a change of
  // language too, which starts the game again), and a moment after the game gets
  // the headset's focus or the headset is put on, so the scene is in front of the
  // player however long the game ran before they put it on.
  bool recentre_ = true;
  std::uint64_t recentre_at_ns_ = 0;
  bool presence_ = false;
  XrAction recentre_action_ = XR_NULL_HANDLE, zoom_stick_ = XR_NULL_HANDLE;
  float zoom_ = 1.0f;
  bool zooming_ = false;
  std::uint64_t zoom_ns_ = 0;
  XrActionSet actions_ = XR_NULL_HANDLE;
  std::vector<Button> buttons_;
  XrAction stick_ = XR_NULL_HANDLE;
  bool stick_down_[4] = {};
  bool running_ = false, focused_ = false, tearing_down_ = false;
  std::string runtime_name_, gpu_name_;

  VkInstance vk_instance_ = VK_NULL_HANDLE;
  VkPhysicalDevice gpu_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VkQueue queue_ = VK_NULL_HANDLE;
  std::uint32_t queue_family_ = 0;
  VkCommandPool pool_ = VK_NULL_HANDLE;
  VkCommandBuffer cmd_ = VK_NULL_HANDLE;
  VkFence fence_ = VK_NULL_HANDLE;
  VkBuffer lines_ = VK_NULL_HANDLE;
  VkDeviceMemory lines_memory_ = VK_NULL_HANDLE;
  void* mapped_ = nullptr;
  VkFormat format_ = VK_FORMAT_UNDEFINED;
  bool linear_colours_ = true;
  VkRenderPass render_pass_ = VK_NULL_HANDLE;
  VkPipelineLayout layout_ = VK_NULL_HANDLE;
  VkPipeline pipeline_ = VK_NULL_HANDLE;
  std::array<Eye, 2> eyes_;
};

XrDisplay::XrDisplay(float metres_per_unit) : impl_(std::make_unique<Impl>(metres_per_unit)) {}
XrDisplay::~XrDisplay() = default;
const char* XrDisplay::gpu_name() const { return impl_->gpu_name(); }
int XrDisplay::height() const { return impl_->height(); }
void XrDisplay::draw(const std::vector<Layer>& layers, float line_width) { impl_->draw(layers, line_width); }
void XrDisplay::recentre() { impl_->recentre(); }
float XrDisplay::zoom() const { return impl_->zoom(); }
void XrDisplay::set_zoom(float z) { impl_->set_zoom(z); }
const char* XrDisplay::runtime_name() const { return impl_->runtime_name(); }
void XrDisplay::set_scale(float metres_per_unit) { impl_->set_scale(metres_per_unit); }
float XrDisplay::scale() const { return impl_->scale(); }

}  // namespace vibr
