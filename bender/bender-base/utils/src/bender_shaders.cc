// Copyright 2016 Google Inc. All Rights Reserved.
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
#include "bender_shaders.h"

extern VkDevice benderDevice;
extern AAssetManager *benderAssetManager;

VkResult loadShaderFromFile(const char *filePath, VkShaderModule *shaderOut,
                            ShaderType type) {
  // Read the file:
  AAsset *file =
      AAssetManager_open(benderAssetManager, filePath, AASSET_MODE_BUFFER);
  size_t fileLength = AAsset_getLength(file);

  char *fileContent = new char[fileLength];

  AAsset_read(file, fileContent, fileLength);

  VkShaderModuleCreateInfo shaderModuleCreateInfo{
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .pNext = nullptr,
      .codeSize = fileLength,
      .pCode = (const uint32_t *) fileContent,
      .flags = 0,
  };
  VkResult result = vkCreateShaderModule(
      benderDevice, &shaderModuleCreateInfo, nullptr, shaderOut);

  delete[] fileContent;

  return result;
}
