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

#ifndef BENDER_BASE_SHADER_STATE_H_
#define BENDER_BASE_SHADER_STATE_H_

#include "vulkan_wrapper.h"
#include "bender_kit.h"

#include <android_native_app_glue.h>
#include <string>
#include <vector>
#include <array>

class ShaderState {
 public:
  enum class Type { Vertex, Fragment };

  static constexpr int kShaderTypesCount = static_cast<int>(Type::Fragment) + 1;

  // TODO: passing app and app_device to each shader state object to just set a static member variable
  // TODO: is bad design. Consider a different approach.
  ShaderState(std::string shader_name, const benderkit::VertexFormat& vertex_format,
              android_app &app, VkDevice app_device);

  ~ShaderState();

  void FillPipelineInfo(VkGraphicsPipelineCreateInfo *pipeline_info);


 private:
  android_app &android_app_ctx_;
  VkDevice device_;
  std::string file_name_;

  benderkit::VertexFormat vertex_format_;        // TODO: Consider sharing the vertex format across shader states

  VkPipelineInputAssemblyStateCreateInfo pipeline_input_assembly_;

  std::array<VkShaderModule, kShaderTypesCount> shader_modules;
  std::array<VkPipelineShaderStageCreateInfo, kShaderTypesCount> shader_stages;

  void SetVertexShader(const std::string &shader_file);
  void SetFragmentShader(const std::string &shader_file);

  VkResult LoadShaderFromFile(const char *file_path, VkShaderModule *shader_out);
};

#endif //BENDER_BASE_SHADER_STATE_HPP
