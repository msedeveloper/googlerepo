// Copyright 2019 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "renderer.h"
#include <android_native_app_glue.h>
#include <debug_marker.h>
#include <timing.h>

#include "trace.h"
#include "bender_helpers.h"
#include "shader_bindings.h"
#include "constants.h"

using namespace BenderHelpers;

Renderer::Renderer(BenderKit::Device& device)
  : device_(device)
  , default_states_(device) {
  init();
}

Renderer::~Renderer() {
  vkFreeCommandBuffers(getVulkanDevice(), cmd_pool_, cmd_buffer_len_,
                       cmd_buffer_);

  for (int x = 0; x < device_.getDisplayImages().size(); x++){
    vkDestroySemaphore(device_.getDevice(), acquire_image_semaphore_[x], nullptr);
    vkDestroySemaphore(device_.getDevice(), render_finished_semaphore_[x], nullptr);
    vkDestroyFence(device_.getDevice(), fence_[x], nullptr);
  }

  delete[] cmd_buffer_;
  delete[] acquire_image_semaphore_;
  delete[] render_finished_semaphore_;
  delete[] fence_;

  destroyPool();
  vkDestroyDescriptorSetLayout(device_.getDevice(), lights_descriptors_layout_, nullptr);

  vkDestroyPipelineCache(getVulkanDevice(), cache_, nullptr);
  vkDestroyCommandPool(getVulkanDevice(), cmd_pool_, nullptr);
}

void Renderer::beginFrame() {
  Timing::timer.time("vkAcquireNextImageKHR", Timing::OTHER, [this](){
    uint32_t nextIndex;
    CALL_VK(vkAcquireNextImageKHR(device_.getDevice(), device_.getSwapchain(),
                                  UINT64_MAX, acquire_image_semaphore_[getCurrentFrame()], VK_NULL_HANDLE,
                                  &nextIndex));
  });

  Timing::timer.time("vkWaitForFences", Timing::OTHER, [this](){
    CALL_VK(vkWaitForFences(device_.getDevice(), 1, &fence_[getCurrentFrame()], VK_TRUE, 100000000));
    CALL_VK(vkResetFences(device_.getDevice(), 1, &fence_[getCurrentFrame()]));
  });
}

void Renderer::endFrame() {
  VkPipelineStageFlags wait_stage_mask =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

  VkSubmitInfo submit_info = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .pNext = nullptr,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = &acquire_image_semaphore_[getCurrentFrame()],
      .pWaitDstStageMask = &wait_stage_mask,
      .commandBufferCount = 1,
      .pCommandBuffers = &cmd_buffer_[getCurrentFrame()],
      .signalSemaphoreCount = 1,
      .pSignalSemaphores = &render_finished_semaphore_[getCurrentFrame()]};

  Timing::timer.time("vkQueueSubmit", Timing::OTHER, [this, submit_info](){
    CALL_VK(vkQueueSubmit(device_.getQueue(), 1, &submit_info, fence_[getCurrentFrame()]));
  });

  Timing::timer.time("Device::Present", Timing::OTHER, [this](){
    device_.present(&render_finished_semaphore_[getCurrentFrame()]);
  });
}

void Renderer::beginPrimaryCommandBufferRecording() {
  VkCommandBufferBeginInfo cmd_buffer_beginInfo{
          .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
          .pNext = nullptr,
          .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
          .pInheritanceInfo = nullptr,
  };
  CALL_VK(vkBeginCommandBuffer(getCurrentCommandBuffer(),
                               &cmd_buffer_beginInfo));

  // transition the display image to color attachment layout
  setImageLayout(getCurrentCommandBuffer(),
                 device_.getCurrentDisplayImage(),
                 VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                 VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
}

void Renderer::endPrimaryCommandBufferRecording() {
  setImageLayout(getCurrentCommandBuffer(),
                 getCurrentDisplayImage(),
                 VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                 VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

  CALL_VK(vkEndCommandBuffer(getCurrentCommandBuffer()));
}

void Renderer::init() {
  VkCommandPoolCreateInfo cmd_pool_createInfo{
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .pNext = nullptr,
      .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
      .queueFamilyIndex = 0,
  };
  CALL_VK(vkCreateCommandPool(device_.getDevice(), &cmd_pool_createInfo, nullptr,
                              &cmd_pool_));

  cmd_buffer_len_ = device_.getSwapchainLength();
  cmd_buffer_ = new VkCommandBuffer[device_.getSwapchainLength()];
  acquire_image_semaphore_ = new VkSemaphore[device_.getSwapchainLength()];
  render_finished_semaphore_ = new VkSemaphore[device_.getSwapchainLength()];
  fence_ = new VkFence[device_.getSwapchainLength()];

  for (int i = 0; i < device_.getSwapchainLength(); ++i) {
    VkCommandBufferAllocateInfo cmd_buffer_createInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = cmd_pool_,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    CALL_VK(vkAllocateCommandBuffers(device_.getDevice(), &cmd_buffer_createInfo,
                                     &cmd_buffer_[i]));

    DebugMarker::setObjectName(device_.getDevice(), (uint64_t)cmd_buffer_[i],
        VK_DEBUG_REPORT_OBJECT_TYPE_COMMAND_BUFFER_EXT,
        ("TEST NAME: COMMAND BUFFER" + std::to_string(i)).c_str());

    VkSemaphoreCreateInfo semaphore_create_info{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
    };

    CALL_VK(vkCreateSemaphore(device_.getDevice(),
                              &semaphore_create_info,
                              nullptr,
                              &acquire_image_semaphore_[i]));

    CALL_VK(vkCreateSemaphore(device_.getDevice(),
                              &semaphore_create_info,
                              nullptr,
                              &render_finished_semaphore_[i]));

    VkFenceCreateInfo fenceCreateInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    CALL_VK(
        vkCreateFence(device_.getDevice(),
                      &fenceCreateInfo,
                      nullptr,
                      &fence_[i]));
  }

  lights_buffer_ = std::make_unique<UniformBufferObject<LightBlock>>(device_);

  VkPipelineCacheCreateInfo pipelineCacheInfo {
          .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
          .pNext = nullptr,
          .initialDataSize = 0,
          .pInitialData = nullptr,
          .flags = 0,  // reserved, must be 0
  };

  CALL_VK(vkCreatePipelineCache(getVulkanDevice(), &pipelineCacheInfo, nullptr,
                                &cache_));

  createPool();
  createLightsDescriptorSetLayout();
  createLightsDescriptors();
}

void Renderer::updateLights(glm::vec3& camera) {
  lights_buffer_->update(getCurrentFrame(), [&camera](auto &lightsBuffer) {
      lightsBuffer.pointLight.position = {0.0f, 0.0f, 6.0f};
      lightsBuffer.pointLight.color = {1.0f, 1.0f, 1.0f};
      lightsBuffer.pointLight.intensity = 1.0f;
      lightsBuffer.ambientLight.color = {1.0f, 1.0f, 1.0f};
      lightsBuffer.ambientLight.intensity = 0.1f;
      lightsBuffer.cameraPos = camera;
  });
}

void Renderer::createPool() {
  uint32_t maxMvpBuffers = MAX_MESHES * device_.getDisplayImages().size();
  uint32_t maxLightsBuffers = MAX_LIGHTS * device_.getDisplayImages().size();
  uint32_t maxSamplers = MAX_SAMPLERS * device_.getDisplayImages().size();
  uint32_t maxTexts = MAX_LINES_TEXTS * device_.getDisplayImages().size();

  std::array<VkDescriptorPoolSize, 2> poolSizes = {};
  poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  poolSizes[0].descriptorCount = maxSamplers + maxTexts;
  poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  poolSizes[1].descriptorCount = maxMvpBuffers + maxLightsBuffers + maxTexts;

  VkDescriptorPoolCreateInfo poolInfo = {};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.poolSizeCount = poolSizes.size();
  poolInfo.pPoolSizes = poolSizes.data();
  poolInfo.maxSets = maxMvpBuffers + maxLightsBuffers + maxSamplers + maxTexts;

  CALL_VK(vkCreateDescriptorPool(device_.getDevice(), &poolInfo, nullptr, &descriptor_pool_));
}

void Renderer::destroyPool() {
    vkDestroyDescriptorPool(device_.getDevice(), descriptor_pool_, nullptr);
}

void Renderer::createLightsDescriptorSetLayout() {
  VkDescriptorSetLayoutBinding lightBlockLayoutBinding = {};
  lightBlockLayoutBinding.binding = FRAGMENT_BINDING_LIGHTS;
  lightBlockLayoutBinding.descriptorCount = 1;
  lightBlockLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  lightBlockLayoutBinding.pImmutableSamplers = nullptr;
  lightBlockLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  std::array<VkDescriptorSetLayoutBinding, 1> bindings = {lightBlockLayoutBinding};

  VkDescriptorSetLayoutCreateInfo layoutInfo = {};
  layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
  layoutInfo.pBindings = bindings.data();

  CALL_VK(vkCreateDescriptorSetLayout(device_.getDevice(), &layoutInfo, nullptr,
                                      &lights_descriptors_layout_));
}

void Renderer::createLightsDescriptors() {
  std::vector<VkDescriptorSetLayout> layouts(device_.getDisplayImages().size(), lights_descriptors_layout_);

  VkDescriptorSetAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = descriptor_pool_;
  allocInfo.descriptorSetCount = static_cast<uint32_t>(device_.getDisplayImages().size());
  allocInfo.pSetLayouts = layouts.data();

  lights_descriptor_sets_.resize(device_.getDisplayImages().size());
  CALL_VK(vkAllocateDescriptorSets(device_.getDevice(), &allocInfo, lights_descriptor_sets_.data()));

  for (size_t i = 0; i < device_.getDisplayImages().size(); i++) {
    VkDescriptorBufferInfo lightBlockInfo = {};
    lightBlockInfo.buffer = lights_buffer_->getBuffer(i);
    lightBlockInfo.offset = 0;
    lightBlockInfo.range = sizeof(LightBlock);

    std::array<VkWriteDescriptorSet, 1> descriptorWrites = {};

    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = lights_descriptor_sets_[i];
    descriptorWrites[0].dstBinding = FRAGMENT_BINDING_LIGHTS;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pBufferInfo = &lightBlockInfo;

    vkUpdateDescriptorSets(device_.getDevice(), descriptorWrites.size(), descriptorWrites.data(),
                           0, nullptr);
  }
}

VkGraphicsPipelineCreateInfo Renderer::getDefaultPipelineInfo(
        VkPipelineLayout layout,
        VkRenderPass render_pass) const {
  VkGraphicsPipelineCreateInfo pipeline_info {
          .layout = layout,
          .renderPass = render_pass,
  };

  getDefaultStates().fillDefaultPipelineCreateInfo(&pipeline_info);

  return pipeline_info;
}

VkCommandBuffer Renderer::getCurrentCommandBuffer() const {
  return cmd_buffer_[getCurrentFrame()];
}

uint32_t Renderer::getCurrentFrame() const {
  return device_.getCurrentFrameIndex();
}

VkImage Renderer::getCurrentDisplayImage() const {
  return device_.getCurrentDisplayImage();
}