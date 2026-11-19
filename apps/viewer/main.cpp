// vibview: draws a model from your own Vib-Ribbon disc as lines and plays its animations.
//
//   vibview <disc.cue> [<pak> [<model.tmd> [<anim.anm>]]]
//   defaults: GAME/02_FILES.PAK  CHARA/PEELOO/MODEL.TMD  first .ANM beside the model
//
//   Left mouse drag: orbit     Wheel: zoom        R: reset view
//   Space: pause/resume        , and . : step one frame while paused
//   Left/Right: previous/next animation in the model's folder
//   Up/Down: line width        Esc: quit

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

#include "vib/disc_image.h"
#include "vib/iso9660.h"
#include "vib/motion.h"
#include "vib/pak.h"
#include "vib/pose.h"
#include "vib/tmd.h"

namespace {

// ---------------------------------------------------------------- shaders

const std::uint32_t kLineVert[] =
#include "line.vert.inc"
    ;
const std::uint32_t kLineFrag[] =
#include "line.frag.inc"
    ;
const std::uint32_t kFillVert[] =
#include "fill.vert.inc"
    ;
const std::uint32_t kFillFrag[] =
#include "fill.frag.inc"
    ;

// ---------------------------------------------------------------- maths

struct Mat4 {
  float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};  // column-major
};

Mat4 mul(const Mat4& a, const Mat4& b) {
  Mat4 r;
  for (int c = 0; c < 4; ++c)
    for (int row = 0; row < 4; ++row) {
      float s = 0;
      for (int k = 0; k < 4; ++k) s += a.m[k * 4 + row] * b.m[c * 4 + k];
      r.m[c * 4 + row] = s;
    }
  return r;
}

// Vulkan clip space: Y down, depth 0..1. The view space used here is Y up, looking down -Z.
Mat4 perspective(float fov_y, float aspect, float z_near, float z_far) {
  const float f = 1.0f / std::tan(fov_y * 0.5f);
  Mat4 r;
  r.m[0] = f / aspect;
  r.m[5] = -f;
  r.m[10] = z_far / (z_near - z_far);
  r.m[11] = -1;
  r.m[14] = z_near * z_far / (z_near - z_far);
  r.m[15] = 0;
  return r;
}

struct V3 {
  float x, y, z;
};
V3 sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 norm(V3 a) {
  const float l = std::sqrt(dot(a, a));
  return l > 0 ? V3{a.x / l, a.y / l, a.z / l} : V3{0, 0, 1};
}

Mat4 look_at(V3 eye, V3 target, V3 up) {
  const V3 f = norm(sub(target, eye)), s = norm(cross(f, up)), u = cross(s, f);
  Mat4 r;
  r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z; r.m[12] = -dot(s, eye);
  r.m[1] = u.x; r.m[5] = u.y; r.m[9] = u.z; r.m[13] = -dot(u, eye);
  r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z; r.m[14] = dot(f, eye);
  return r;
}

// ---------------------------------------------------------------- GPU data

struct LineInstance {
  float a[3];
  float b[3];
  float colour[4];
};
struct FillVertex {
  float position[3];
  float colour[4];
};
struct PushConstants {
  Mat4 mvp;
  float viewport[2];
  float width;
  float feather;
};

constexpr std::uint32_t kMaxLines = 32768;
constexpr std::uint32_t kMaxFillVertices = 8192;

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

class Renderer {
 public:
  explicit Renderer(SDL_Window* window) : window_(window) {
    create_instance();
    if (!SDL_Vulkan_CreateSurface(window_, instance_, nullptr, &surface_))
      throw std::runtime_error(std::string("SDL_Vulkan_CreateSurface: ") + SDL_GetError());
    pick_device();
    create_device();
    create_render_pass();
    create_pipelines();
    lines_ = create_buffer(sizeof(LineInstance) * kMaxLines);
    fills_ = create_buffer(sizeof(FillVertex) * kMaxFillVertices);
    create_commands();
    create_swapchain();
  }

  ~Renderer() {
    if (!device_) return;
    vkDeviceWaitIdle(device_);
    destroy_swapchain();
    for (Buffer* b : {&lines_, &fills_}) {
      if (b->buffer) vkDestroyBuffer(device_, b->buffer, nullptr);
      if (b->memory) vkFreeMemory(device_, b->memory, nullptr);
    }
    if (line_pipeline_) vkDestroyPipeline(device_, line_pipeline_, nullptr);
    if (fill_pipeline_) vkDestroyPipeline(device_, fill_pipeline_, nullptr);
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
  void request_resize() { resize_ = true; }

  void draw(const std::vector<LineInstance>& lines, const std::vector<FillVertex>& fills, const Mat4& view, float fov_y,
            float line_width) {
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
    if (r != VK_SUBOPTIMAL_KHR) check(r, "vkAcquireNextImageKHR");
    vkResetFences(device_, 1, &frame_fence_);

    const std::uint32_t line_count = std::min<std::uint32_t>(std::uint32_t(lines.size()), kMaxLines);
    const std::uint32_t fill_count = std::min<std::uint32_t>(std::uint32_t(fills.size()), kMaxFillVertices);
    if (line_count) std::memcpy(lines_.mapped, lines.data(), sizeof(LineInstance) * line_count);
    if (fill_count) std::memcpy(fills_.mapped, fills.data(), sizeof(FillVertex) * fill_count);

    PushConstants pc;
    const float aspect = float(extent_.width) / float(extent_.height);
    pc.mvp = mul(perspective(fov_y, aspect, 10.0f, 100000.0f), view);
    pc.viewport[0] = float(extent_.width);
    pc.viewport[1] = float(extent_.height);
    pc.width = line_width;
    pc.feather = 0.75f;

    vkResetCommandBuffer(cmd_, 0);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    check(vkBeginCommandBuffer(cmd_, &begin), "vkBeginCommandBuffer");
    VkClearValue clear[2];
    clear[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clear[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass = render_pass_;
    rp.framebuffer = framebuffers_[image];
    rp.renderArea.extent = extent_;
    rp.clearValueCount = 2;
    rp.pClearValues = clear;
    vkCmdBeginRenderPass(cmd_, &rp, VK_SUBPASS_CONTENTS_INLINE);
    const VkViewport vp{0, 0, float(extent_.width), float(extent_.height), 0, 1};
    const VkRect2D scissor{{0, 0}, extent_};
    vkCmdSetViewport(cmd_, 0, 1, &vp);
    vkCmdSetScissor(cmd_, 0, 1, &scissor);
    vkCmdPushConstants(cmd_, layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof pc, &pc);
    const VkDeviceSize zero = 0;
    if (fill_count) {  // fills first: they write depth so they can hide lines behind them
      vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, fill_pipeline_);
      vkCmdBindVertexBuffers(cmd_, 0, 1, &fills_.buffer, &zero);
      vkCmdDraw(cmd_, fill_count, 1, 0, 0);
    }
    if (line_count) {
      vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, line_pipeline_);
      vkCmdBindVertexBuffers(cmd_, 0, 1, &lines_.buffer, &zero);
      vkCmdDraw(cmd_, 6, line_count, 0, 0);
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
    r = vkQueuePresentKHR(queue_, &present);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) resize_ = true;
    else check(r, "vkQueuePresentKHR");
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
    app.pApplicationName = "vibview";
    app.apiVersion = VK_API_VERSION_1_0;
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
    const char* ext = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &q;
    info.enabledExtensionCount = 1;
    info.ppEnabledExtensionNames = &ext;
    check(vkCreateDevice(gpu_, &info, nullptr, &device_), "vkCreateDevice");
    vkGetDeviceQueue(device_, queue_family_, 0, &queue_);

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
    const VkShaderModule fv = shader(kFillVert, sizeof kFillVert), ff = shader(kFillFrag, sizeof kFillFrag);
    line_pipeline_ = pipeline(lv, lf, {0, sizeof(LineInstance), VK_VERTEX_INPUT_RATE_INSTANCE},
                              {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(LineInstance, a)},
                               {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(LineInstance, b)},
                               {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(LineInstance, colour)}},
                              true, false);
    fill_pipeline_ = pipeline(fv, ff, {0, sizeof(FillVertex), VK_VERTEX_INPUT_RATE_VERTEX},
                              {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(FillVertex, position)},
                               {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(FillVertex, colour)}},
                              false, true);
    for (VkShaderModule m : {lv, lf, fv, ff}) vkDestroyShaderModule(device_, m, nullptr);
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
    extent_.width = std::clamp(extent_.width, caps.minImageExtent.width, caps.maxImageExtent.width);
    extent_.height = std::clamp(extent_.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    if (extent_.width == 0 || extent_.height == 0) { extent_ = {0, 0}; return; }

    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = surface_;
    info.minImageCount = caps.minImageCount + 1;
    if (caps.maxImageCount && info.minImageCount > caps.maxImageCount) info.minImageCount = caps.maxImageCount;
    info.imageFormat = surface_format_.format;
    info.imageColorSpace = surface_format_.colorSpace;
    info.imageExtent = extent_;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;  // vsync; always available
    info.clipped = VK_TRUE;
    check(vkCreateSwapchainKHR(device_, &info, nullptr, &swapchain_), "vkCreateSwapchainKHR");

    std::uint32_t count = 0;
    vkGetSwapchainImagesKHR(device_, swapchain_, &count, nullptr);
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
  VkPipeline line_pipeline_ = VK_NULL_HANDLE, fill_pipeline_ = VK_NULL_HANDLE;
  Buffer lines_, fills_;
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
};

// ---------------------------------------------------------------- scene

std::string upper(std::string s) {
  for (char& c : s) c = c == '\\' ? '/' : static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

// Model space is PlayStation-style (Y down). Flip Y so the viewer's world is Y up.
V3 to_world(const vib::Vec3f& v) { return {v.x, -v.y, v.z}; }

void build_geometry(const vib::TmdModel& model, const std::vector<vib::PosedObject>& posed, std::vector<LineInstance>& lines,
                    std::vector<FillVertex>& fills, V3& lo, V3& hi) {
  lines.clear();
  fills.clear();
  lo = {1e9f, 1e9f, 1e9f};
  hi = {-1e9f, -1e9f, -1e9f};
  for (const auto& p : posed) {
    const vib::TmdObject& obj = model.objects[std::size_t(p.object)];
    for (const auto& v : p.vertices) {
      const V3 w = to_world(v);
      lo = {std::min(lo.x, w.x), std::min(lo.y, w.y), std::min(lo.z, w.z)};
      hi = {std::max(hi.x, w.x), std::max(hi.y, w.y), std::max(hi.z, w.z)};
    }
    for (const auto& l : obj.lines) {
      const V3 a = to_world(p.vertices[l.a]), b = to_world(p.vertices[l.b]);
      LineInstance li{{a.x, a.y, a.z}, {b.x, b.y, b.z}, {l.colour_a.r / 255.0f, l.colour_a.g / 255.0f, l.colour_a.b / 255.0f, 1.0f}};
      lines.push_back(li);
    }
    for (const auto& f : obj.fills) {
      const int order[6] = {0, 1, 2, 1, 3, 2};
      const int n = f.count == 4 ? 6 : 3;
      for (int i = 0; i < n; ++i) {
        const V3 w = to_world(p.vertices[f.v[order[i]]]);
        fills.push_back({{w.x, w.y, w.z}, {f.colour.r / 255.0f, f.colour.g / 255.0f, f.colour.b / 255.0f, 1.0f}});
      }
    }
  }
}

int run(int argc, char** argv) {
  if (argc < 2) {
    std::fputs("usage: vibview <disc.cue> [<pak> [<model.tmd> [<anim.anm>]]]\n"
               "  defaults: GAME/02_FILES.PAK CHARA/PEELOO/MODEL.TMD and the first animation beside the model\n",
               stderr);
    return 2;
  }
  const std::string pak_path = argc > 2 ? argv[2] : "GAME/02_FILES.PAK";
  const std::string model_name = upper(argc > 3 ? argv[3] : "CHARA/PEELOO/MODEL.TMD");

  vib::DiscImage disc = vib::DiscImage::open(vib::path_from_utf8(argv[1]));
  vib::Iso9660 iso(disc);
  const vib::Pak pak(iso.read(pak_path));
  const vib::TmdModel model = vib::parse_tmd(pak.view(model_name));

  // Animations that live in the same folder as the model.
  const std::string folder = model_name.substr(0, model_name.rfind('/') + 1);
  std::vector<std::string> anim_names;
  for (const auto& e : pak.entries()) {
    const std::string n = upper(e.name);
    if (n.size() > 4 && n.compare(0, folder.size(), folder) == 0 && n.compare(n.size() - 4, 4, ".ANM") == 0 &&
        n.find('/', folder.size()) == std::string::npos)
      anim_names.push_back(n);
  }
  int anim_index = anim_names.empty() ? -1 : 0;
  if (argc > 4) {
    const std::string want = upper(argv[4]);
    const auto it = std::find(anim_names.begin(), anim_names.end(), want);
    if (it == anim_names.end()) anim_names.push_back(want), anim_index = int(anim_names.size()) - 1;
    else anim_index = int(it - anim_names.begin());
  }

  if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(std::string("SDL_Init: ") + SDL_GetError());
  SDL_Window* window = SDL_CreateWindow("vibview", 1280, 720, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (!window) throw std::runtime_error(std::string("SDL_CreateWindow: ") + SDL_GetError());

  {
    Renderer renderer(window);
    std::printf("GPU: %s\nModel: %s (%zu objects), %zu animations in its folder\n", renderer.gpu_name(), model_name.c_str(),
                model.objects.size(), anim_names.size());

    vib::ModelAnimation anim;
    auto load_anim = [&]() {
      anim = {};
      if (anim_index >= 0) anim = vib::parse_anm(pak.view(anim_names[std::size_t(anim_index)]));
      const std::string title = "vibview  " + model_name + "  " + (anim_index >= 0 ? anim_names[std::size_t(anim_index)] : std::string("(no animation)")) +
                                "  rate " + std::to_string(anim.rate) + "  frames " + std::to_string(anim.frames.size());
      SDL_SetWindowTitle(window, title.c_str());
      std::printf("%s\n", title.c_str());
    };
    load_anim();

    std::vector<LineInstance> lines;
    std::vector<FillVertex> fills;
    V3 lo{}, hi{};
    float yaw = 0.5f, pitch = 0.15f, distance = 0, line_width = 2.5f;
    V3 centre{0, 0, 0};
    bool framed = false, paused = false, running = true;
    double frame_pos = 0;
    Uint64 last = SDL_GetTicksNS();

    while (running) {
      SDL_Event e;
      while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT) running = false;
        else if (e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || e.type == SDL_EVENT_WINDOW_RESIZED) renderer.request_resize();
        else if (e.type == SDL_EVENT_MOUSE_MOTION && (e.motion.state & SDL_BUTTON_LMASK)) {
          yaw -= e.motion.xrel * 0.008f;
          pitch = std::clamp(pitch + e.motion.yrel * 0.008f, -1.5f, 1.5f);
        } else if (e.type == SDL_EVENT_MOUSE_WHEEL) {
          distance *= std::pow(0.9f, e.wheel.y);
        } else if (e.type == SDL_EVENT_KEY_DOWN) {
          const int frames = int(anim.frames.size());
          switch (e.key.key) {
            case SDLK_ESCAPE: running = false; break;
            case SDLK_SPACE: paused = !paused; break;
            case SDLK_R: framed = false; yaw = 0.5f; pitch = 0.15f; break;
            case SDLK_UP: line_width = std::min(line_width + 0.5f, 12.0f); break;
            case SDLK_DOWN: line_width = std::max(line_width - 0.5f, 1.0f); break;
            case SDLK_PERIOD: if (paused && frames) frame_pos = double((int(frame_pos) + 1) % frames); break;
            case SDLK_COMMA: if (paused && frames) frame_pos = double((int(frame_pos) + frames - 1) % frames); break;
            case SDLK_RIGHT:
            case SDLK_LEFT:
              if (!anim_names.empty()) {
                const int n = int(anim_names.size());
                anim_index = (anim_index + (e.key.key == SDLK_RIGHT ? 1 : n - 1)) % n;
                frame_pos = 0;
                load_anim();
              }
              break;
            default: break;
          }
        }
      }

      const Uint64 now = SDL_GetTicksNS();
      const double dt = double(now - last) * 1e-9;
      last = now;
      // The header's rate field is treated as frames per second (unconfirmed).
      const double fps = anim.rate > 0 ? double(anim.rate) : 30.0;
      if (!paused && !anim.frames.empty()) frame_pos = std::fmod(frame_pos + dt * fps, double(anim.frames.size()));

      const std::vector<vib::PosedObject> posed =
          anim.frames.empty() ? vib::rest_pose(model) : vib::pose(model, anim.frames[std::min(std::size_t(frame_pos), anim.frames.size() - 1)]);
      build_geometry(model, posed, lines, fills, lo, hi);
      if (!framed && !lines.empty()) {
        centre = {(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f};
        const float size = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z, 1.0f});
        distance = size * 1.6f;
        framed = true;
      }

      const float fov = 0.8f;
      const V3 eye{centre.x + distance * std::cos(pitch) * std::sin(yaw), centre.y + distance * std::sin(pitch),
                   centre.z + distance * std::cos(pitch) * std::cos(yaw)};
      renderer.draw(lines, fills, look_at(eye, centre, {0, 1, 0}), fov, line_width);
    }
  }
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    return run(argc, argv);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "vibview: %s\n", e.what());
    return 1;
  }
}
