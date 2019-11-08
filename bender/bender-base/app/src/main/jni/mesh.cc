//
// Created by mattkw on 10/31/2019.
//

#include <glm/gtc/matrix_transform.hpp>

#include "mesh.h"
#include "material.h"
#include "shader_bindings.h"

Mesh::Mesh(Renderer &renderer, Material &material, std::shared_ptr<Geometry> geometry) :
    renderer_(renderer), material_(material), geometry_(geometry) {
  position_ = glm::vec3(0.0f, 0.0f, 0.0f);
  rotation_ = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
  scale_ = glm::vec3(1.0f, 1.0f, 1.0f);

  mesh_buffer_ = std::make_unique<UniformBufferObject<ModelViewProjection>>(renderer_.getDevice());
  createMeshDescriptorSetLayout();
  createMeshDescriptors();
}

Mesh::Mesh(Renderer &renderer, Material &material, const std::vector<float> &vertexData,
           const std::vector<uint16_t> &indexData) :
    Mesh(renderer,
         material,
         std::make_shared<Geometry>(renderer.getDevice(), vertexData, indexData)) {}


Mesh::~Mesh() {
  vkDestroyPipeline(renderer_.getVulkanDevice(), pipeline_, nullptr);
  vkDestroyPipelineCache(renderer_.getVulkanDevice(), cache_, nullptr);
  vkDestroyPipelineLayout(renderer_.getVulkanDevice(), layout_, nullptr);

  vkDestroyDescriptorSetLayout(renderer_.getVulkanDevice(), mesh_descriptors_layout_, nullptr);
}

void Mesh::createMeshDescriptors() {
  std::vector<VkDescriptorSetLayout> layouts(renderer_.getDevice().getDisplayImagesSize(),
                                             mesh_descriptors_layout_);

  VkDescriptorSetAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = renderer_.getDescriptorPool();
  allocInfo.descriptorSetCount = renderer_.getDevice().getDisplayImagesSize();
  allocInfo.pSetLayouts = layouts.data();

  mesh_descriptor_sets_.resize(renderer_.getDevice().getDisplayImagesSize());
  CALL_VK(vkAllocateDescriptorSets(renderer_.getVulkanDevice(), &allocInfo, mesh_descriptor_sets_.data()));

  for (size_t i = 0; i < renderer_.getDevice().getDisplayImagesSize(); i++) {
    VkDescriptorBufferInfo bufferInfo = {};
    bufferInfo.buffer = mesh_buffer_->getBuffer(i);
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(ModelViewProjection);

    std::array<VkWriteDescriptorSet, 1> descriptorWrites = {};

    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = mesh_descriptor_sets_[i];
    descriptorWrites[0].dstBinding = VERTEX_BINDING_MODEL_VIEW_PROJECTION;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(renderer_.getVulkanDevice(), descriptorWrites.size(), descriptorWrites.data(),
                           0, nullptr);

  }
}

void Mesh::createMeshPipeline(VkRenderPass renderPass) {
  VkViewport viewport{
          .x = 0.0f,
          .y = 0.0f,
          .width = static_cast<float>(renderer_.getDevice().getDisplaySize().width),
          .height = static_cast<float>(renderer_.getDevice().getDisplaySize().height),
          .minDepth = 0.0f,
          .maxDepth = 1.0f,
  };

  VkRect2D scissor{
          .offset = {0, 0},
          .extent = renderer_.getDevice().getDisplaySize(),
  };

  VkPipelineViewportStateCreateInfo pipelineViewportState{
          .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
          .viewportCount = 1,
          .pViewports = &viewport,
          .scissorCount = 1,
          .pScissors = &scissor,
  };

  VkPipelineDepthStencilStateCreateInfo depthStencilState{
          .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
          .depthTestEnable = VK_TRUE,
          .depthWriteEnable = VK_TRUE,
          .depthCompareOp = VK_COMPARE_OP_LESS,
          .depthBoundsTestEnable = VK_FALSE,
          .minDepthBounds = 0.0f,
          .maxDepthBounds = 1.0f,
          .stencilTestEnable = VK_FALSE,
  };

  VkPipelineRasterizationStateCreateInfo pipelineRasterizationState{
          .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
          .depthClampEnable = VK_FALSE,
          .rasterizerDiscardEnable = VK_FALSE,
          .polygonMode = VK_POLYGON_MODE_FILL,
          .lineWidth = 1.0f,
          .cullMode = VK_CULL_MODE_BACK_BIT,
          .frontFace = VK_FRONT_FACE_CLOCKWISE,
          .depthBiasEnable = VK_FALSE,
          .depthBiasConstantFactor = 0.0f,
          .depthBiasClamp = 0.0f,
          .depthBiasSlopeFactor = 0.0f,
  };

  // Multisample anti-aliasing setup
  VkPipelineMultisampleStateCreateInfo pipelineMultisampleState{
          .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
          .pNext = nullptr,
          .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
          .sampleShadingEnable = VK_FALSE,
          .minSampleShading = 0,
          .pSampleMask = nullptr,
          .alphaToCoverageEnable = VK_FALSE,
          .alphaToOneEnable = VK_FALSE,
  };

  // Describes how to blend pixels from past framebuffers to current framebuffers
  // Could be used for transparency or cool screen-space effects
  VkPipelineColorBlendAttachmentState attachmentStates{
          .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
          .blendEnable = VK_FALSE,
  };

  VkPipelineColorBlendStateCreateInfo colorBlendInfo{
          .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
          .pNext = nullptr,
          .logicOpEnable = VK_FALSE,
          .logicOp = VK_LOGIC_OP_COPY,
          .attachmentCount = 1,
          .pAttachments = &attachmentStates,
          .flags = 0,
  };

  std::array<VkDescriptorSetLayout, 3> layouts;

  layouts[BINDING_SET_MESH] = mesh_descriptors_layout_;
  layouts[BINDING_SET_MATERIAL] = material_.getMaterialDescriptorSetLayout();
  layouts[BINDING_SET_LIGHTS] = renderer_.getLightsDescriptorSetLayout();

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{
          .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
          .pNext = nullptr,
          .setLayoutCount = layouts.size(),
          .pSetLayouts = layouts.data(),
          .pushConstantRangeCount = 0,
          .pPushConstantRanges = nullptr,
  };

  CALL_VK(vkCreatePipelineLayout(renderer_.getVulkanDevice(), &pipelineLayoutInfo, nullptr,
                                 &layout_))

  VkPipelineCacheCreateInfo pipelineCacheInfo{
          .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
          .pNext = nullptr,
          .initialDataSize = 0,
          .pInitialData = nullptr,
          .flags = 0,  // reserved, must be 0
  };

  CALL_VK(vkCreatePipelineCache(renderer_.getVulkanDevice(), &pipelineCacheInfo, nullptr,
                                &cache_));

  VkGraphicsPipelineCreateInfo pipelineInfo{
          .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
          .pNext = nullptr,
          .flags = 0,
          .stageCount = 2,
          .pTessellationState = nullptr,
          .pViewportState = &pipelineViewportState,
          .pRasterizationState = &pipelineRasterizationState,
          .pMultisampleState = &pipelineMultisampleState,
          .pDepthStencilState = &depthStencilState,
          .pColorBlendState = &colorBlendInfo,
          .pDynamicState = nullptr,
          .layout = layout_,
          .renderPass = renderPass,
          .subpass = 0,
          .basePipelineHandle = VK_NULL_HANDLE,
          .basePipelineIndex = 0,
  };

  material_.getShaders()->updatePipelineInfo(pipelineInfo);

  CALL_VK(vkCreateGraphicsPipelines(renderer_.getVulkanDevice(), cache_, 1, &pipelineInfo,
                                    nullptr, &pipeline_));
}

void Mesh::createMeshDescriptorSetLayout() {
  VkDescriptorSetLayoutBinding uboLayoutBinding = {};
  uboLayoutBinding.binding = VERTEX_BINDING_MODEL_VIEW_PROJECTION;
  uboLayoutBinding.descriptorCount = 1;
  uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  uboLayoutBinding.pImmutableSamplers = nullptr;
  uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

  std::array<VkDescriptorSetLayoutBinding, 1> bindings = { uboLayoutBinding };
  VkDescriptorSetLayoutCreateInfo layoutInfo = {};
  layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfo.bindingCount = bindings.size();
  layoutInfo.pBindings = bindings.data();

  CALL_VK(vkCreateDescriptorSetLayout(renderer_.getVulkanDevice(), &layoutInfo, nullptr,
                                    &mesh_descriptors_layout_));
}

void Mesh::updatePipeline(VkRenderPass renderPass) {
  if (pipeline_ != VK_NULL_HANDLE)
    return;

  createMeshPipeline(renderPass);
}

void Mesh::update(uint_t frame_index, glm::vec3 camera, glm::mat4 view, glm::mat4 proj) {
  glm::mat4 model = getTransform();
  glm::mat4 mvp = proj * view * model;

  mesh_buffer_->update(frame_index, [&mvp, &model](auto& ubo) {
    ubo.mvp = mvp;
    ubo.model = model;
    ubo.invTranspose = glm::transpose(glm::inverse(model));
  });
}

void Mesh::submitDraw(VkCommandBuffer commandBuffer, uint_t frame_index) const {
  vkCmdBindPipeline(commandBuffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipeline_);

  geometry_->bind(commandBuffer);

  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          layout_, BINDING_SET_MESH, 1, &mesh_descriptor_sets_[frame_index], 0, nullptr);

  VkDescriptorSet lightsDescriptorSet = renderer_.getLightsDescriptorSet(frame_index);

  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          layout_, BINDING_SET_LIGHTS, 1, &lightsDescriptorSet, 0, nullptr);

  VkDescriptorSet materialDescriptorSet = material_.getMaterialDescriptorSet(frame_index);

  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          layout_, BINDING_SET_MATERIAL, 1, &materialDescriptorSet, 0, nullptr);

  vkCmdDrawIndexed(commandBuffer,
                   geometry_->getIndexCount(),
                   1, 0, 0, 0);
}

void Mesh::translate(glm::vec3 offset) {
  position_ += offset;
}

void Mesh::rotate(glm::vec3 axis, float angle) {
  rotation_ *= glm::angleAxis(glm::radians(angle), axis);
  rotation_ = glm::normalize(rotation_);
}

void Mesh::scale(glm::vec3 scaling) {
  scale_ *= scaling;
}

void Mesh::setPosition(glm::vec3 position) {
  position_ = position;
}

void Mesh::setRotation(glm::vec3 axis, float angle) {
  rotation_ = glm::angleAxis(glm::radians(angle), axis);
}

void Mesh::setScale(glm::vec3 scale) {
  scale_ = scale;
}

glm::vec3 Mesh::getPosition() const {
  return position_;
}

glm::quat Mesh::getRotation() const {
  return rotation_;
}

glm::vec3 Mesh::getScale() const {
  return scale_;
}

glm::mat4 Mesh::getTransform() const {
  glm::mat4 position = glm::translate(glm::mat4(1.0), position_);
  glm::mat4 scale = glm::scale(glm::mat4(1.0), scale_);
  return position * glm::mat4(rotation_) * scale;
}