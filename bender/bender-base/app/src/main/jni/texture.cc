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

#include "texture.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "bender_helpers.h"

#include <cmath>
#include <cstdlib>
#include <vector>

Texture::Texture(Renderer &renderer,
                 uint8_t *img_data,
                 uint32_t img_width,
                 uint32_t img_height,
                 VkFormat texture_format)
    : renderer_(renderer), texture_format_(texture_format) {
  tex_width_ = img_width;
  tex_height_ = img_height;
  mip_levels_ = std::floor(std::log2(std::max(tex_width_, tex_height_))) + 1;

  CALL_VK(CreateUncompressedTexture(img_data, sizeof(uint8_t) * tex_height_ * tex_width_ * 4));
  CreateImageView();
}

Texture::Texture(Renderer &renderer,
                 android_app &android_app_ctx,
                 const std::string &texture_file_name,
                 VkFormat texture_format)
    : renderer_(renderer), texture_format_(texture_format) {
  CALL_VK(CreateTextureFromFile(android_app_ctx, texture_file_name));
  CreateImageView();
}

Texture::~Texture() {
  vkDestroyImageView(renderer_.GetVulkanDevice(), view_, nullptr);
  vkDestroyImage(renderer_.GetVulkanDevice(), image_, nullptr);
  vkFreeMemory(renderer_.GetVulkanDevice(), mem_, nullptr);
}

unsigned char *Texture::LoadFallbackData() {
  tex_width_ = 128;
  tex_height_ = 128;
  unsigned char *img_data = (unsigned char *) malloc(tex_width_ * tex_height_ * 4 * sizeof(unsigned char));
  for (int32_t i = 0; i < tex_height_; i++) {
    for (int32_t j = 0; j < tex_width_; j++) {
      img_data[(i + j * tex_width_) * 4] = 215;
      img_data[(i + j * tex_width_) * 4 + 1] = 95;
      img_data[(i + j * tex_width_) * 4 + 2] = 175;
      img_data[(i + j * tex_width_) * 4 + 3] = 255;
    }
  }
  return img_data;
}

unsigned char *Texture::LoadImageFileData(AAsset *file) {
  size_t file_length = AAsset_getLength(file);
  stbi_uc *file_content = new unsigned char[file_length];
  AAsset_read(file, file_content, file_length);
  AAsset_close(file);

  unsigned char *img_data;
  uint32_t img_width, img_height, n;
  img_data = stbi_load_from_memory(
          file_content, file_length, reinterpret_cast<int *>(&img_width),
          reinterpret_cast<int *>(&img_height), reinterpret_cast<int *>(&n), 4);
  tex_width_ = img_width;
  tex_height_ = img_height;

  delete[] file_content;

  return img_data;
}

unsigned char *Texture::LoadASTCFileData(AAsset *file, ASTCHeader& header, uint32_t& img_bytes) {
  AAsset_read(file, &header, sizeof(ASTCHeader));

  VkFormat format = GetASTCFormat(texture_format_, header.block_dim_x, header.block_dim_y);
  if (!ASTCHeaderIsValid(header) || format == VK_FORMAT_UNDEFINED) {
    AAsset_close(file);
    return LoadFallbackData();
  }

  img_bytes = AAsset_getRemainingLength(file);
  unsigned char *img_data = new unsigned char[img_bytes];
  AAsset_read(file, img_data, img_bytes);
  AAsset_close(file);

  return img_data;
}

VkResult Texture::CreateTextureFromFile(android_app &app, const std::string& texture_file_name) {
  AAsset *file = AAssetManager_open(app.activity->assetManager,
                                    texture_file_name.c_str(), AASSET_MODE_BUFFER);

  uint32_t img_bytes = 0;
  unsigned char *img_data;
  if (file == nullptr) {
    img_data = LoadFallbackData();
    img_bytes = sizeof(uint8_t) * tex_height_ * tex_width_ * 4;
    mip_levels_ = (sizeof(tex_width_) * 8) - __builtin_clz(std::max(tex_width_, tex_height_));
    CALL_VK(CreateUncompressedTexture(img_data, img_bytes));
    stbi_image_free(img_data);
  }
  else if (texture_file_name.find(".astc") != -1) {
    CALL_VK(CreateASTCTexture(app, texture_file_name, file));
  } else {
    img_data = LoadImageFileData(file);
    img_bytes = sizeof(uint8_t) * tex_height_ * tex_width_ * 4;
    mip_levels_ = (sizeof(tex_width_) * 8) - __builtin_clz(std::max(tex_width_, tex_height_));
    CALL_VK(CreateUncompressedTexture(img_data, img_bytes));
    stbi_image_free(img_data);
  }

  return VK_SUCCESS;
}

VkResult Texture::CreateUncompressedTexture(uint8_t *img_data, uint32_t img_bytes) {
  VkFormatProperties props;
  vkGetPhysicalDeviceFormatProperties(renderer_.GetDevice().GetPhysicalDevice(), texture_format_, &props);
  assert((props.linearTilingFeatures | props.optimalTilingFeatures) &
      VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);

  VkMemoryAllocateInfo mem_alloc = {
          .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
          .pNext = nullptr,
          .allocationSize = 0,
          .memoryTypeIndex = 0,
  };
  VkMemoryRequirements mem_reqs;

  VkBuffer staging;
  VkDeviceMemory staging_mem;
  renderer_.GetDevice().CreateBuffer(img_bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging,
          staging_mem);

  void *data;
  CALL_VK(vkMapMemory(renderer_.GetVulkanDevice(), staging_mem, 0,
                      img_bytes, 0, &data));
  memcpy(data, img_data, img_bytes);
  vkUnmapMemory(renderer_.GetVulkanDevice(), staging_mem);

  uint32_t queue_family_index = renderer_.GetDevice().GetQueueFamilyIndex();
  VkImageCreateInfo image_create_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .pNext = nullptr,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = texture_format_,
      .extent = {static_cast<uint32_t>(tex_width_),
                 static_cast<uint32_t>(tex_height_), 1},
      .mipLevels = mip_levels_,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .queueFamilyIndexCount = 1,
      .pQueueFamilyIndices = &queue_family_index,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .flags = 0,
  };

  CALL_VK(vkCreateImage(renderer_.GetVulkanDevice(), &image_create_info, nullptr, &image_));

  vkGetImageMemoryRequirements(renderer_.GetVulkanDevice(), image_, &mem_reqs);

  mem_alloc.allocationSize = mem_reqs.size;
  mem_alloc.memoryTypeIndex = benderhelpers::FindMemoryType(mem_reqs.memoryTypeBits,
                                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                                            renderer_.GetDevice().GetPhysicalDevice());
  assert(mem_alloc.memoryTypeIndex != -1);

  CALL_VK(vkAllocateMemory(renderer_.GetVulkanDevice(), &mem_alloc, nullptr, &mem_));
  CALL_VK(vkBindImageMemory(renderer_.GetVulkanDevice(), image_, mem_, 0));

  VkCommandPoolCreateInfo cmd_pool_create_info{
          .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
          .pNext = nullptr,
          .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
          .queueFamilyIndex = renderer_.GetDevice().GetQueueFamilyIndex(),
  };

  VkCommandPool cmd_pool;
  CALL_VK(vkCreateCommandPool(renderer_.GetVulkanDevice(), &cmd_pool_create_info, nullptr,
                              &cmd_pool));

  VkCommandBuffer cmd_bufs[2];
  const VkCommandBufferAllocateInfo cmd = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .pNext = nullptr,
      .commandPool = cmd_pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 2,
  };

  CALL_VK(vkAllocateCommandBuffers(renderer_.GetVulkanDevice(), &cmd, cmd_bufs));

  VkCommandBufferBeginInfo cmd_buf_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .pNext = nullptr,
      .flags = 0,
      .pInheritanceInfo = nullptr};

  CALL_VK(vkBeginCommandBuffer(cmd_bufs[0], &cmd_buf_info));

  benderhelpers::SetImageLayout(cmd_bufs[0], image_, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

  VkBufferImageCopy buffer_copy_region = {
          .imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
          .imageSubresource.mipLevel = 0,
          .imageSubresource.baseArrayLayer = 0,
          .imageSubresource.layerCount = 1,
          .imageExtent.width = static_cast<uint32_t>(tex_width_),
          .imageExtent.height = static_cast<uint32_t>(tex_height_),
          .imageExtent.depth = 1,
  };
  vkCmdCopyBufferToImage(cmd_bufs[0], staging, image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &buffer_copy_region);

  benderhelpers::SetImageLayout(cmd_bufs[0], image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

  CALL_VK(vkEndCommandBuffer(cmd_bufs[0]));

  VkFenceCreateInfo fence_info = {
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
  };
  VkFence fence;
  CALL_VK(vkCreateFence(renderer_.GetVulkanDevice(), &fence_info, nullptr, &fence));

  VkCommandBufferBeginInfo mipmaps_info = {
          .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
          .pNext = nullptr,
          .flags = 0,
          .pInheritanceInfo = nullptr
  };
  CALL_VK(vkBeginCommandBuffer(cmd_bufs[1], &mipmaps_info));
  GenerateMipmaps(cmd_bufs[1]);
  CALL_VK(vkEndCommandBuffer(cmd_bufs[1]));

  CALL_VK(vkCreateFence(renderer_.GetVulkanDevice(), &fence_info, nullptr, &fence));

  VkSubmitInfo submit_info = {
    .pNext = nullptr,
    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .waitSemaphoreCount = 0,
    .pWaitSemaphores = nullptr,
    .pWaitDstStageMask = nullptr,
    .commandBufferCount = 2,
    .pCommandBuffers = cmd_bufs,
    .signalSemaphoreCount = 0,
    .pSignalSemaphores = nullptr,
  };
  CALL_VK(vkQueueSubmit(renderer_.GetDevice().GetWorkerQueue(), 1, &submit_info, fence) != VK_SUCCESS);
  CALL_VK(vkWaitForFences(renderer_.GetVulkanDevice(), 1, &fence, VK_TRUE, 100000000) !=
          VK_SUCCESS);
  vkDestroyFence(renderer_.GetVulkanDevice(), fence, nullptr);

  vkFreeMemory(renderer_.GetVulkanDevice(), staging_mem, nullptr);
  vkDestroyBuffer(renderer_.GetVulkanDevice(), staging, nullptr);

  vkFreeCommandBuffers(renderer_.GetVulkanDevice(), cmd_pool, 2, cmd_bufs);
  vkDestroyCommandPool(renderer_.GetVulkanDevice(), cmd_pool, nullptr);
  return VK_SUCCESS;
}

VkResult Texture::CreateASTCTexture(android_app& app, const std::string& texture_file_name, AAsset *file) {
  ASTCHeader header;
  uint32_t img_bytes = 0;
  unsigned char *img_data = LoadASTCFileData(file, header, img_bytes);

  tex_width_ = header.x_size[0] | (header.x_size[1] << 8) | (header.x_size[2] << 16);
  tex_height_ = header.y_size[0] | (header.y_size[1] << 8) | (header.y_size[2] << 16);
  texture_format_ = GetASTCFormat(texture_format_, header.block_dim_x, header.block_dim_y);
  mip_levels_ = (sizeof(tex_width_) * 8) - __builtin_clz(std::max(tex_width_, tex_height_));

  VkFormatProperties props;
  vkGetPhysicalDeviceFormatProperties(renderer_.GetDevice().GetPhysicalDevice(), texture_format_, &props);
  assert((props.linearTilingFeatures | props.optimalTilingFeatures) &
         VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);

  VkMemoryAllocateInfo mem_alloc = {
          .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
          .pNext = nullptr,
          .allocationSize = 0,
          .memoryTypeIndex = 0,
  };

  VkMemoryRequirements mem_reqs;

  uint32_t queue_family_index = renderer_.GetDevice().GetQueueFamilyIndex();
  VkImageCreateInfo image_create_info = {
          .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
          .pNext = nullptr,
          .imageType = VK_IMAGE_TYPE_2D,
          .format = texture_format_,
          .extent = {static_cast<uint32_t>(tex_width_),
                     static_cast<uint32_t>(tex_height_), 1},
          .mipLevels = mip_levels_,
          .arrayLayers = 1,
          .samples = VK_SAMPLE_COUNT_1_BIT,
          .tiling = VK_IMAGE_TILING_LINEAR,
          .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
          .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
          .queueFamilyIndexCount = 1,
          .pQueueFamilyIndices = &queue_family_index,
          .initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
          .flags = 0,
  };

  CALL_VK(vkCreateImage(renderer_.GetVulkanDevice(), &image_create_info, nullptr, &image_));

  vkGetImageMemoryRequirements(renderer_.GetVulkanDevice(), image_, &mem_reqs);

  mem_alloc.allocationSize = mem_reqs.size;
  mem_alloc.memoryTypeIndex = benderhelpers::FindMemoryType(mem_reqs.memoryTypeBits,
                                                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                                            renderer_.GetDevice().GetPhysicalDevice());
  assert(mem_alloc.memoryTypeIndex != -1);

  CALL_VK(vkAllocateMemory(renderer_.GetVulkanDevice(), &mem_alloc, nullptr, &mem_));
  CALL_VK(vkBindImageMemory(renderer_.GetVulkanDevice(), image_, mem_, 0));

  VkSubresourceLayout layout;
  VkImageSubresource subres = {
          .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
          .mipLevel = 0,
          .arrayLayer = 0,
  };
  vkGetImageSubresourceLayout(renderer_.GetVulkanDevice(), image_, &subres, &layout);

  uint32_t width = header.x_size[0] | (header.x_size[1] << 8) | (header.x_size[2] << 16);
  uint32_t height = header.y_size[0] | (header.y_size[1] << 8) | (header.y_size[2] << 16);

  uint32_t block_rows = std::ceil((float) height / header.block_dim_y);
  uint32_t block_row_bytes = std::ceil((float) width / header.block_dim_x) * 16;

  void *data;
  CALL_VK(vkMapMemory(renderer_.GetVulkanDevice(), mem_, layout.offset, img_bytes, 0, &data));
  for (uint32_t y = 0; y < block_rows; y++) {
    unsigned char *block_row = (unsigned char *) ((char *) data + layout.rowPitch * y);
    memcpy(block_row, img_data + block_row_bytes * y, block_row_bytes);
  }
  vkUnmapMemory(renderer_.GetVulkanDevice(), mem_);
  stbi_image_free(img_data);

  VkCommandPoolCreateInfo cmd_pool_create_info{
          .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
          .pNext = nullptr,
          .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
          .queueFamilyIndex = renderer_.GetDevice().GetQueueFamilyIndex(),
  };

  VkCommandPool cmd_pool;
  CALL_VK(vkCreateCommandPool(renderer_.GetVulkanDevice(), &cmd_pool_create_info, nullptr,
                              &cmd_pool));

  const VkCommandBufferAllocateInfo cmd = {
          .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
          .pNext = nullptr,
          .commandPool = cmd_pool,
          .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
          .commandBufferCount = 1,
  };
  VkCommandBuffer mipmaps_cmd;
  CALL_VK(vkAllocateCommandBuffers(renderer_.GetVulkanDevice(), &cmd, &mipmaps_cmd));

  VkCommandBufferBeginInfo mipmaps_info = {
          .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
          .pNext = nullptr,
          .flags = 0,
          .pInheritanceInfo = nullptr
  };
  CALL_VK(vkBeginCommandBuffer(mipmaps_cmd, &mipmaps_info));

  LoadMipmapsFromFiles(app, texture_file_name);
  benderhelpers::SetImageLayout(mipmaps_cmd, image_, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, mip_levels_);

  CALL_VK(vkEndCommandBuffer(mipmaps_cmd));

  VkFenceCreateInfo fence_info = {
          .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
          .pNext = nullptr,
          .flags = 0,
  };
  VkFence fence;
  CALL_VK(vkCreateFence(renderer_.GetVulkanDevice(), &fence_info, nullptr, &fence));

  VkSubmitInfo submit_info = {
          .pNext = nullptr,
          .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
          .waitSemaphoreCount = 0,
          .pWaitSemaphores = nullptr,
          .pWaitDstStageMask = nullptr,
          .commandBufferCount = 1,
          .pCommandBuffers = &mipmaps_cmd,
          .signalSemaphoreCount = 0,
          .pSignalSemaphores = nullptr,
  };
  CALL_VK(vkQueueSubmit(renderer_.GetDevice().GetWorkerQueue(), 1, &submit_info, fence) != VK_SUCCESS);
  CALL_VK(vkWaitForFences(renderer_.GetVulkanDevice(), 1, &fence, VK_TRUE, 100000000) !=
          VK_SUCCESS);
  vkDestroyFence(renderer_.GetVulkanDevice(), fence, nullptr);

  vkFreeCommandBuffers(renderer_.GetVulkanDevice(), cmd_pool, 1, &mipmaps_cmd);
  vkDestroyCommandPool(renderer_.GetVulkanDevice(), cmd_pool, nullptr);
  return VK_SUCCESS;
}

void Texture::CreateImageView() {
  VkImageViewCreateInfo view_create_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .pNext = nullptr,
      .image = image_,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = texture_format_,
      .components =
          {
              .r = VK_COMPONENT_SWIZZLE_R,
              .g = VK_COMPONENT_SWIZZLE_G,
              .b = VK_COMPONENT_SWIZZLE_B,
              .a = VK_COMPONENT_SWIZZLE_A,
          },
      .subresourceRange = {
              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
              .baseMipLevel = 0,
              .baseArrayLayer = 0,
              .layerCount = 1,
              .levelCount = renderer_.MipmapsEnabled() ? mip_levels_ : 1,
      },
      .flags = 0,
  };
  CALL_VK(vkCreateImageView(renderer_.GetVulkanDevice(), &view_create_info, nullptr, &view_));
}

void Texture::ToggleMipmaps() {
  vkDestroyImageView(renderer_.GetVulkanDevice(), view_, nullptr);
  CreateImageView();
}

void Texture::GenerateMipmaps(VkCommandBuffer &command_buffer) {
  for (uint32_t i = 1; i < mip_levels_; i++) {
      VkImageBlit blit = {
              .srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
              .srcSubresource.layerCount = 1,
              .srcSubresource.mipLevel = i-1,
              .srcOffsets[1].x = int32_t (tex_width_ >> (i-1)),
              .srcOffsets[1].y = int32_t (tex_height_ >> (i-1)),
              .srcOffsets[1].z = 1,
              .dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
              .dstSubresource.layerCount = 1,
              .dstSubresource.mipLevel = i,
              .dstOffsets[1].x = int32_t (tex_width_ >> (i)),
              .dstOffsets[1].y = int32_t (tex_height_ >> (i)),
              .dstOffsets[1].z = 1,
      };

      benderhelpers::SetImageLayout(command_buffer, image_, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
              VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, i, 1);

      vkCmdBlitImage(command_buffer,
                     image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     1, &blit, VK_FILTER_LINEAR);

      benderhelpers::SetImageLayout(command_buffer, image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
              VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, i, 1);
  }

  benderhelpers::SetImageLayout(command_buffer, image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, mip_levels_);
}

void Texture::LoadMipmapsFromFiles(android_app &app, const std::string& base_file_name) {
  size_t file_basename = base_file_name.rfind(".");
  for (uint32_t i = 1; i < mip_levels_; i++) {
    std::string mip_file_name = base_file_name;
    mip_file_name.insert(file_basename, "-mip-" + std::to_string(i));
    AAsset *file = AAssetManager_open(app.activity->assetManager,
                                      mip_file_name.c_str(), AASSET_MODE_BUFFER);

    if (file == nullptr) {
      LOGE("Texture: missing expected mip file");
      break;
    }

    uint32_t img_bytes = 0;
    ASTCHeader header;
    unsigned char *img_data = LoadASTCFileData(file, header, img_bytes);

    VkSubresourceLayout layout;
    VkImageSubresource subres = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = i,
            .arrayLayer = 0,
    };
    vkGetImageSubresourceLayout(renderer_.GetVulkanDevice(), image_, &subres, &layout);

    uint32_t width = header.x_size[0] | (header.x_size[1] << 8) | (header.x_size[2] << 16);
    uint32_t height = header.y_size[0] | (header.y_size[1] << 8) | (header.y_size[2] << 16);

    uint32_t block_rows = std::ceil((float) height / header.block_dim_y);
    uint32_t block_row_bytes = std::ceil((float) width / header.block_dim_x) * 16;

    void *data;
    CALL_VK(vkMapMemory(renderer_.GetVulkanDevice(), mem_, layout.offset, img_bytes, 0, &data));

    for (uint32_t y = 0; y < block_rows; y++) {
      unsigned char *block_row = (unsigned char *) ((char *) data + layout.rowPitch * y);
      memcpy(block_row, img_data + block_row_bytes * y, block_row_bytes);
    }

    vkUnmapMemory(renderer_.GetVulkanDevice(), mem_);
  }
}
