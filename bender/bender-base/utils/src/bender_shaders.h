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

#ifndef BENDER_BASE_UTILS_SRC_BENDER_SHADERS_H_
#define BENDER_BASE_UTILS_SRC_BENDER_SHADERS_H_

#include <android/asset_manager.h>
#include <vulkan_wrapper.h>
enum ShaderType { VERTEX_SHADER, FRAGMENT_SHADER };

VkResult loadShaderFromFile(const char *filePath, VkShaderModule *shaderOut,
                            ShaderType type);

#endif  // BENDER_SHADERS_HPP