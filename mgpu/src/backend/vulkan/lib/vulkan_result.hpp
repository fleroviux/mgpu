
#pragma once

#include <mgpu/mgpu.h>
#include <vulkan/vulkan.h>

namespace mgpu::vulkan {

inline MGPUResult VkResultToMGPUResult(VkResult vk_result) {
  // TODO(fleroviux): handle:
  //   - VK_ERROR_DEVICE_LOST
  //   - VK_ERROR_SURFACE_LOST_KHR
  //   - VK_ERROR_NATIVE_WINDOW_IN_USE_KHR
  //   - VK_ERROR_INITIALIZATION_FAILED (?)
  switch(vk_result) {
    case VK_SUCCESS:                    return MGPU_SUCCESS;
    case VK_ERROR_OUT_OF_HOST_MEMORY:   return MGPU_OUT_OF_MEMORY;
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return MGPU_OUT_OF_MEMORY;
    case VK_ERROR_TOO_MANY_OBJECTS:     return MGPU_OUT_OF_MEMORY;
    case VK_NOT_READY:                  return MGPU_NOT_READY;
    case VK_SUBOPTIMAL_KHR:             return MGPU_SWAP_CHAIN_SUBOPTIMAL;
    default: return MGPU_INTERNAL_ERROR;
  }
}

#define MGPU_VK_FORWARD_ERROR(expression) \
  do { \
    const VkResult vk_result = (expression); \
    if(vk_result != VK_SUCCESS) { \
      return VkResultToMGPUResult(vk_result); \
    } \
  } while(0)

} // namespace mgpu::vulkan
