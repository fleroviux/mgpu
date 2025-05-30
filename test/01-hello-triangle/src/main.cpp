
#include <mgpu/mgpu.h>

#include <atom/integer.hpp>
#include <atom/panic.hpp>
#include <SDL.h>
#include <SDL_syswm.h>
#include <optional>
#include <vector>

#include "shader/triangle.frag.h"
#include "shader/triangle.vert.h"

#undef main

#define MGPU_CHECK(result_expression) \
  do { \
    MGPUResult result = result_expression; \
    if(result != MGPU_SUCCESS) \
      ATOM_PANIC("MGPU error: {} ({})", "" # result_expression, mgpuResultCodeToString(result)); \
  } while(0)

#ifdef SDL_VIDEO_DRIVER_COCOA
  extern "C" CAMetalLayer* TMP_Cocoa_CreateMetalLayer(NSWindow* ns_window);
#endif

int main() {
  SDL_Init(SDL_INIT_VIDEO);

  SDL_Window* sdl_window = SDL_CreateWindow(
    "test-01-hello-triangle",
    SDL_WINDOWPOS_CENTERED,
    SDL_WINDOWPOS_CENTERED,
    1600,
    900,
    SDL_WINDOW_VULKAN
  );

  MGPUInstance mgpu_instance{};
  MGPU_CHECK(mgpuCreateInstance(MGPU_BACKEND_TYPE_VULKAN, &mgpu_instance));

  MGPUSurface mgpu_surface{};
  {
    SDL_SysWMinfo wm_info{};
    SDL_GetWindowWMInfo(sdl_window, &wm_info);

    MGPUSurfaceCreateInfo surface_create_info{};

#if defined(SDL_VIDEO_DRIVER_WINDOWS)
    surface_create_info.win32 = {
      .hinstance = wm_info.info.win.hinstance,
      .hwnd = wm_info.info.win.window
    };
#elif defined(SDL_VIDEO_DRIVER_COCOA)
    surface_create_info.metal = {
      .metal_layer = TMP_Cocoa_CreateMetalLayer(wm_info.info.cocoa.window)
    };
#else
  #error "Unsupported SDL video driver"
#endif

    MGPU_CHECK(mgpuInstanceCreateSurface(mgpu_instance, &surface_create_info, &mgpu_surface));
  }

  uint32_t mgpu_physical_device_count{};
  std::vector<MGPUPhysicalDevice> mgpu_physical_devices{};
  MGPU_CHECK(mgpuInstanceEnumeratePhysicalDevices(mgpu_instance, &mgpu_physical_device_count, nullptr));
  mgpu_physical_devices.resize(mgpu_physical_device_count);
  MGPU_CHECK(mgpuInstanceEnumeratePhysicalDevices(mgpu_instance, &mgpu_physical_device_count, mgpu_physical_devices.data()));

  std::optional<MGPUPhysicalDevice> discrete_gpu{};
  std::optional<MGPUPhysicalDevice> integrated_gpu{};
  std::optional<MGPUPhysicalDevice> virtual_gpu{};

  for(MGPUPhysicalDevice mgpu_physical_device : mgpu_physical_devices) {
    MGPUPhysicalDeviceInfo info{};
    MGPU_CHECK(mgpuPhysicalDeviceGetInfo(mgpu_physical_device, &info));
    switch(info.device_type) {
      case MGPU_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:     discrete_gpu = mgpu_physical_device; break;
      case MGPU_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: integrated_gpu = mgpu_physical_device; break;
      case MGPU_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:       virtual_gpu = mgpu_physical_device; break;
    }
  }

  MGPUPhysicalDevice mgpu_physical_device = discrete_gpu.value_or(
    integrated_gpu.value_or(
      virtual_gpu.value_or((MGPUPhysicalDevice)MGPU_NULL_HANDLE)));

  if(mgpu_physical_device == MGPU_NULL_HANDLE) {
    ATOM_PANIC("failed to find a suitable physical device");
  }

  MGPUDevice mgpu_device{};
  MGPU_CHECK(mgpuPhysicalDeviceCreateDevice(mgpu_physical_device, &mgpu_device));

  MGPUSwapChain mgpu_swap_chain{};
  std::vector<MGPUTexture> mgpu_swap_chain_textures{};
  std::vector<MGPUTextureView> mgpu_swap_chain_texture_views{};

  // Swap Chain creation
  {
    uint32_t surface_format_count{};
    std::vector<MGPUSurfaceFormat> surface_formats{};

    MGPU_CHECK(mgpuPhysicalDeviceEnumerateSurfaceFormats(mgpu_physical_device, mgpu_surface, &surface_format_count, nullptr));
    surface_formats.resize(surface_format_count);
    MGPU_CHECK(mgpuPhysicalDeviceEnumerateSurfaceFormats(mgpu_physical_device, mgpu_surface, &surface_format_count, surface_formats.data()));

    bool got_required_surface_format = false;

    for(const MGPUSurfaceFormat& surface_format : surface_formats) {
      if(surface_format.format == MGPU_TEXTURE_FORMAT_B8G8R8A8_SRGB && surface_format.color_space == MGPU_COLOR_SPACE_SRGB_NONLINEAR) {
        got_required_surface_format = true;
      }
    }

    if(!got_required_surface_format) {
      ATOM_PANIC("Failed to find a suitable surface format");
    }

    uint32_t present_modes_count{};
    std::vector<MGPUPresentMode> present_modes{};

    MGPU_CHECK(mgpuPhysicalDeviceEnumerateSurfacePresentModes(mgpu_physical_device, mgpu_surface, &present_modes_count, nullptr));
    present_modes.resize(present_modes_count);
    MGPU_CHECK(mgpuPhysicalDeviceEnumerateSurfacePresentModes(mgpu_physical_device, mgpu_surface, &present_modes_count, present_modes.data()));

    MGPUSurfaceCapabilities surface_capabilities{};
    MGPU_CHECK(mgpuPhysicalDeviceGetSurfaceCapabilities(mgpu_physical_device, mgpu_surface, &surface_capabilities));

    const MGPUSwapChainCreateInfo swap_chain_create_info{
      .surface = mgpu_surface,
      .format = MGPU_TEXTURE_FORMAT_B8G8R8A8_SRGB,
      .color_space = MGPU_COLOR_SPACE_SRGB_NONLINEAR,
      .present_mode = MGPU_PRESENT_MODE_FIFO,
      .usage = MGPU_TEXTURE_USAGE_RENDER_ATTACHMENT,
      .extent = surface_capabilities.current_extent,
      .min_texture_count = 2u,
      .old_swap_chain = nullptr
    };
    MGPU_CHECK(mgpuDeviceCreateSwapChain(mgpu_device, &swap_chain_create_info, &mgpu_swap_chain));

    u32 texture_count{};
    MGPU_CHECK(mgpuSwapChainEnumerateTextures(mgpu_swap_chain, &texture_count, nullptr));
    mgpu_swap_chain_textures.resize(texture_count);
    MGPU_CHECK(mgpuSwapChainEnumerateTextures(mgpu_swap_chain, &texture_count, mgpu_swap_chain_textures.data()));

    for(MGPUTexture texture : mgpu_swap_chain_textures) {
      const MGPUTextureViewCreateInfo texture_view_create_info{
        .type = MGPU_TEXTURE_VIEW_TYPE_2D,
        .format = MGPU_TEXTURE_FORMAT_B8G8R8A8_SRGB,
        .aspect = MGPU_TEXTURE_ASPECT_COLOR,
        .base_mip = 0u,
        .mip_count = 1u,
        .base_array_layer = 0u,
        .array_layer_count = 1u
      };

      MGPUTextureView texture_view{};
      MGPU_CHECK(mgpuTextureCreateView(texture, &texture_view_create_info, &texture_view));
      mgpu_swap_chain_texture_views.push_back(texture_view);
    }
  }

  const float vertices[] {
    -0.5f,  0.5f, 0.0f,   1.0f, 0.0f, 0.0f,
     0.5f,  0.5f, 0.0f,   0.0f, 1.0f, 0.0f,
     0.0f, -0.5f, 0.0f,   0.0f, 0.0f, 1.0f
  };
  const MGPUBufferCreateInfo vbo_create_info{
    .size = sizeof(vertices),
    .usage = MGPU_BUFFER_USAGE_VERTEX_BUFFER,
    .flags = MGPU_BUFFER_FLAGS_HOST_VISIBLE
  };
  MGPUBuffer mgpu_vbo{};
  MGPU_CHECK(mgpuDeviceCreateBuffer(mgpu_device, &vbo_create_info, &mgpu_vbo));
  void* vbo_address{};
  MGPU_CHECK(mgpuBufferMap(mgpu_vbo, &vbo_address));
  std::memcpy(vbo_address, vertices, sizeof(vertices));
  MGPU_CHECK(mgpuBufferFlushRange(mgpu_vbo, 0u, MGPU_WHOLE_SIZE));
  MGPU_CHECK(mgpuBufferUnmap(mgpu_vbo));

  MGPUShaderModule mgpu_vert_shader{};
  MGPUShaderModule mgpu_frag_shader{};
  MGPU_CHECK(mgpuDeviceCreateShaderModule(mgpu_device, triangle_vert, sizeof(triangle_vert), &mgpu_vert_shader));
  MGPU_CHECK(mgpuDeviceCreateShaderModule(mgpu_device, triangle_frag, sizeof(triangle_frag), &mgpu_frag_shader));

  const MGPUShaderStageCreateInfo shader_stages[2] {
    {
      .stage = MGPU_SHADER_STAGE_VERTEX,
      .module = mgpu_vert_shader,
      .entrypoint = "main"
    },
    {
      .stage = MGPU_SHADER_STAGE_FRAGMENT,
      .module = mgpu_frag_shader,
      .entrypoint = "main"
    }
  };
  const MGPUShaderProgramCreateInfo shader_program_create_info{
    .shader_stage_count = 2u,
    .shader_stages = shader_stages
  };
  MGPUShaderProgram mgpu_shader_program{};
  MGPU_CHECK(mgpuDeviceCreateShaderProgram(mgpu_device, &shader_program_create_info, &mgpu_shader_program));

  const MGPUVertexBinding vertex_input_binding{
    .binding = 0u,
    .stride = sizeof(float) * 6,
    .input_rate = MGPU_VERTEX_INPUT_RATE_VERTEX
  };
  const MGPUVertexAttribute vertex_input_attributes[]{
    {
      .location = 0u,
      .binding = 0u,
      .format = MGPU_VERTEX_FORMAT_STUB_XYZ323232,
      .offset = 0u
    },
    {
      .location = 1u,
      .binding = 0u,
      .format = MGPU_VERTEX_FORMAT_STUB_XYZ323232,
      .offset = sizeof(float) * 3
    }
  };
  const MGPUVertexInputStateCreateInfo vertex_input_state_create_info{
    .binding_count = 1u,
    .bindings = &vertex_input_binding,
    .attribute_count = sizeof(vertex_input_attributes) / sizeof(MGPUVertexAttribute),
    .attributes = vertex_input_attributes
  };
  MGPUVertexInputState mgpu_vertex_input_state{};
  MGPU_CHECK(mgpuDeviceCreateVertexInputState(mgpu_device, &vertex_input_state_create_info, &mgpu_vertex_input_state));

  MGPUCommandList mgpu_cmd_list{};
  MGPU_CHECK(mgpuDeviceCreateCommandList(mgpu_device, &mgpu_cmd_list));

  MGPUQueue mgpu_queue = mgpuDeviceGetQueue(mgpu_device, MGPU_QUEUE_TYPE_GRAPHICS_COMPUTE);

  SDL_Event sdl_event{};

  while(true) {
    u32 texture_index;
    MGPU_CHECK(mgpuSwapChainAcquireNextTexture(mgpu_swap_chain, &texture_index));

    MGPU_CHECK(mgpuCommandListClear(mgpu_cmd_list));

    const MGPURenderPassColorAttachment render_pass_color_attachments[1] {
      {
        .texture_view = mgpu_swap_chain_texture_views[texture_index],
        .load_op = MGPU_LOAD_OP_CLEAR,
        .store_op = MGPU_STORE_OP_STORE,
        .clear_color = {.r = 0.3f, .g = 0.f, .b = 0.9f, .a = 1.f}
      }
    };
    const MGPURenderPassBeginInfo render_pass_info{
      .color_attachment_count = 1u,
      .color_attachments = render_pass_color_attachments,
      .depth_stencil_attachment = nullptr
    };
    MGPURenderCommandEncoder render_cmd_encoder = mgpuCommandListCmdBeginRenderPass(mgpu_cmd_list, &render_pass_info);
    mgpuRenderCommandEncoderCmdUseShaderProgram(render_cmd_encoder, mgpu_shader_program);
    mgpuRenderCommandEncoderCmdUseVertexInputState(render_cmd_encoder, mgpu_vertex_input_state);
    mgpuRenderCommandEncoderCmdBindVertexBuffer(render_cmd_encoder, 0u, mgpu_vbo, 0u);
    mgpuRenderCommandEncoderCmdDraw(render_cmd_encoder, 3u, 1u, 0u, 0u);
    mgpuRenderCommandEncoderClose(render_cmd_encoder);

    MGPU_CHECK(mgpuQueueSubmitCommandList(mgpu_queue, mgpu_cmd_list));
    MGPU_CHECK(mgpuSwapChainPresent(mgpu_swap_chain));

    while(SDL_PollEvent(&sdl_event)) {
      if(sdl_event.type == SDL_QUIT) {
        goto done;
      }
    }
  }

done:
  mgpuCommandListDestroy(mgpu_cmd_list);
  mgpuVertexInputStateDestroy(mgpu_vertex_input_state);
  mgpuShaderProgramDestroy(mgpu_shader_program);
  mgpuShaderModuleDestroy(mgpu_frag_shader);
  mgpuShaderModuleDestroy(mgpu_vert_shader);
  mgpuBufferDestroy(mgpu_vbo);
  for(MGPUTextureView texture_view : mgpu_swap_chain_texture_views) mgpuTextureViewDestroy(texture_view);
  mgpuSwapChainDestroy(mgpu_swap_chain);
  mgpuDeviceDestroy(mgpu_device);
  mgpuSurfaceDestroy(mgpu_surface);
  mgpuInstanceDestroy(mgpu_instance);
  SDL_DestroyWindow(sdl_window);
  return 0;
}
