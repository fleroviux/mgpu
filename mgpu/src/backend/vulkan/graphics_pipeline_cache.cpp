
#include <atom/hash.hpp>
#include <atom/integer.hpp>

#include "backend/vulkan/lib/vulkan_result.hpp"
#include "pipeline_state/color_blend_state.hpp"
#include "pipeline_state/depth_stencil_state.hpp"
#include "pipeline_state/input_assembly_state.hpp"
#include "pipeline_state/shader_program.hpp"
#include "pipeline_state/rasterizer_state.hpp"
#include "pipeline_state/vertex_input_state.hpp"
#include "graphics_pipeline_cache.hpp"

namespace mgpu::vulkan {

size_t GraphicsPipelineQuery::Hasher::operator()(const GraphicsPipelineQuery& query) const {
  size_t hash = std::hash<const ShaderProgram*>{}(query.m_shader_program);
  atom::hash_combine(hash, query.m_rasterizer_state);
  atom::hash_combine(hash, query.m_input_assembly_state);
  atom::hash_combine(hash, query.m_color_blend_state);
  atom::hash_combine(hash, query.m_vertex_input_state);
  atom::hash_combine(hash, query.m_depth_stencil_state);
  atom::hash_combine(hash, query.m_vk_render_pass);
  return hash;
}

bool GraphicsPipelineQuery::operator==(const GraphicsPipelineQuery& other_query) const {
  return m_shader_program == other_query.m_shader_program
      && m_rasterizer_state == other_query.m_rasterizer_state
      && m_input_assembly_state == other_query.m_input_assembly_state
      && m_color_blend_state == other_query.m_color_blend_state
      && m_vertex_input_state == other_query.m_vertex_input_state
      && m_depth_stencil_state == other_query.m_depth_stencil_state
      && m_vk_render_pass == other_query.m_vk_render_pass;
}

GraphicsPipelineCache::GraphicsPipelineCache(VkDevice vk_device, std::shared_ptr<DeleterQueue> deleter_queue)
    : m_vk_device{vk_device}
    , m_deleter_queue{std::move(deleter_queue)} {
}

GraphicsPipelineCache::~GraphicsPipelineCache() {
  for(const auto& [query_key, vk_pipeline] : m_query_to_vk_pipeline) {
    // Defer deletion of underlying Vulkan resources until the currently recorded frame has been fully processed on the GPU.
    VkDevice vk_device = m_vk_device;
    m_deleter_queue->Schedule([vk_device, vk_pipeline]() {
      vkDestroyPipeline(vk_device, vk_pipeline, nullptr);
    });
  }
}

Result<VkPipeline> GraphicsPipelineCache::GetPipeline(const GraphicsPipelineQuery& query) {
  VkPipeline vk_pipeline = m_query_to_vk_pipeline[query];

  if(vk_pipeline != nullptr) {
    return vk_pipeline;
  }

  const std::span<const VkPipelineShaderStageCreateInfo> vk_shader_stages = query.m_shader_program->GetVkShaderStages();

  // TODO(fleroviux): do not recreate these structures from scratch on every pipeline generation.

  const VkPipelineViewportStateCreateInfo vk_viewport_state_create_info{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
    .viewportCount = 1u,
    .pViewports = nullptr,
    .scissorCount = 1u,
    .pScissors = nullptr
  };

  const VkPipelineMultisampleStateCreateInfo vk_multisample_state_create_info{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
    .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    .sampleShadingEnable = VK_FALSE,
    .minSampleShading = 0.f,
    .pSampleMask = nullptr,
    .alphaToCoverageEnable = VK_FALSE,
    .alphaToOneEnable = VK_FALSE
  };

  const VkDynamicState vk_dynamic_states[] { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

  const VkPipelineDynamicStateCreateInfo vk_dynamic_state_create_info{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
    .dynamicStateCount = sizeof(vk_dynamic_states) / sizeof(VkDynamicState),
    .pDynamicStates = vk_dynamic_states
  };

  const VkGraphicsPipelineCreateInfo vk_graphics_pipeline_create_info{
    .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
    .stageCount = (u32)vk_shader_stages.size(),
    .pStages = vk_shader_stages.data(),
    .pVertexInputState = &query.m_vertex_input_state->GetVkVertexInputState(),
    .pInputAssemblyState = &query.m_input_assembly_state->GetVkInputAssemblyState(),
    .pTessellationState = nullptr,
    .pViewportState = &vk_viewport_state_create_info,
    .pRasterizationState = &query.m_rasterizer_state->GetVkRasterizationState(),
    .pMultisampleState = &vk_multisample_state_create_info,
    .pDepthStencilState = &query.m_depth_stencil_state->GetVkDepthStencilState(),
    .pColorBlendState = &query.m_color_blend_state->GetVkColorBlendState(),
    .pDynamicState = &vk_dynamic_state_create_info,
    .layout = query.m_shader_program->GetVkPipelineLayout(),
    .renderPass = query.m_vk_render_pass,
    .subpass = 0,
    .basePipelineHandle = VK_NULL_HANDLE,
    .basePipelineIndex = 0
  };

  MGPU_VK_FORWARD_ERROR(vkCreateGraphicsPipelines(m_vk_device, nullptr, 1u, &vk_graphics_pipeline_create_info, nullptr, &vk_pipeline));
  m_query_to_vk_pipeline[query] = vk_pipeline;
  return vk_pipeline;
}


} // namespace mgpu::vulkan
