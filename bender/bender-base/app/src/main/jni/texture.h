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

#ifndef BENDER_BASE_TEXTURE_H
#define BENDER_BASE_TEXTURE_H

#include "astc.h"
#include "renderer.h"

#include <android_native_app_glue.h>
#include "vulkan_wrapper.h"
#include "bender_kit.h"

#include <functional>

using namespace astc;

class Texture {
 public:

  Texture(Renderer &renderer,
          uint8_t *img_data,
          uint32_t img_width,
          uint32_t img_height,
          VkFormat texture_format);

  Texture(Renderer &renderer,
          android_app &android_app_ctx,
          const std::string &texture_file_name,
          VkFormat texture_format);

  ~Texture();

  void ToggleMipmaps();

  VkImageView GetImageView() const { return view_; }

  int32_t GetWidth() const { return tex_width_; };

  int32_t GetHeight() const { return tex_height_; };

  uint32_t GetMipLevel() { return mip_levels_; }

 private:
  Renderer &renderer_;

  VkImage image_;
  VkDeviceMemory mem_;
  VkImageView view_;
  int32_t tex_width_;
  int32_t tex_height_;
  VkFormat texture_format_;

  uint32_t mip_levels_;

  unsigned char *LoadFallbackData();
  unsigned char *LoadImageFileData(AAsset *file);
  unsigned char *LoadASTCFileData(AAsset *file, ASTCHeader& header, uint32_t& img_bytes);

  VkResult CreateTextureFromFile(android_app &app, const std::string& texture_file_name);
  VkResult CreateUncompressedTexture(uint8_t *img_data, uint32_t img_bytes);
  VkResult CreateASTCTexture(android_app& app, const std::string& texture_file_name, AAsset *file);
  void CreateImageView();

  void GenerateMipmaps(VkCommandBuffer& command_buffer);
  void LoadMipmapsFromFiles(android_app& app, const std::string& base_file_name);
};

#endif //BENDER_BASE_TEXTURE_H
