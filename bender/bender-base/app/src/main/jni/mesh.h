//
// Created by mattkw on 10/31/2019.
//

#ifndef BENDER_BASE_MESH_H
#define BENDER_BASE_MESH_H

#include <vector>
#include <memory>

#include "vulkan_wrapper.h"
#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"
#include "bender_kit.h"
#include "geometry.h"
#include "shader_state.h"
#include "texture.h"
#include "renderer.h"
#include "material.h"

#include "uniform_buffer.h"
#include "camera.h"

struct ModelViewProjection {
  alignas(16) glm::mat4 mvp;
  alignas(16) glm::mat4 model;
  alignas(16) glm::mat4 inv_transpose;
};

class Mesh {
public:
  Mesh(Renderer &renderer, std::shared_ptr<Material> material, std::shared_ptr<Geometry> geometry_data);

  Mesh(const Mesh &other, std::shared_ptr<Geometry> geometry);
  Mesh(const Mesh &other, std::shared_ptr<Material> material);

  ~Mesh();

  void UpdatePipeline(VkRenderPass render_pass);

  void Update(uint_t frame_index, Camera &camera);
  void SubmitDraw(VkCommandBuffer cmd_buffer, uint_t frame_index) const;

  void Translate(glm::vec3 offset);
  void Rotate(glm::vec3 axis, float angle);
  void Scale(glm::vec3 scaling);

  void SetPosition(glm::vec3 position);
  void SetRotation(glm::vec3 axis, float angle);
  void SetScale(glm::vec3 scale);

  glm::vec3 GetPosition() const;
  glm::quat GetRotation() const;
  glm::vec3 GetScale() const;

  glm::mat4 GetTransform(bool for_mvp = false) const;

  BoundingBox GetBoundingBoxWorldSpace();

  int GetTrianglesCount() const;

private:
  std::unique_ptr<UniformBufferObject<ModelViewProjection>> mesh_buffer_;

  Renderer &renderer_;

  const std::shared_ptr<Material> material_;
  const std::shared_ptr<Geometry> geometry_;

  glm::vec3 position_;
  glm::quat rotation_;
  glm::vec3 scale_;

  BoundingBox world_space_box_;
  bool bounding_box_dirty_;

  VkDescriptorSetLayout mesh_descriptors_layout_;

  VkPipelineLayout layout_ = VK_NULL_HANDLE;
  VkPipeline pipeline_ = VK_NULL_HANDLE;

  std::vector<VkDescriptorSet> mesh_descriptor_sets_;

  void CreateMeshPipeline(VkRenderPass render_pass);

  void CreateMeshDescriptorSetLayout();
  void CreateMeshDescriptors();
  void ComputeBoundingBoxWorldSpace();
};

#endif //BENDER_BASE_MESH_H
