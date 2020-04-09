//
// Created by mattkw on 10/25/2019.
//

#ifndef BENDER_BASE_GEOMETRY_H_
#define BENDER_BASE_GEOMETRY_H_

#include "vulkan_wrapper.h"
#include "bender_kit.h"
#include <functional>
#include <glm/glm.hpp>
#include <packed_types.h>
#include <mesh_helpers.h>

struct BoundingBox {
    BoundingBox(glm::vec3 min, glm::vec3 max) :
            min(min), max(max) {
        center = (max + min) * .5f;
    }

    BoundingBox() :
            min({MAXFLOAT, MAXFLOAT, MAXFLOAT}), max({-MAXFLOAT, -MAXFLOAT, -MAXFLOAT}),
            center({0, 0, 0}) {}

    glm::vec3 min, max, center;
};

class Geometry {
 public:
  Geometry(benderkit::Device &device,
           const std::vector<MeshVertex> &vertex_data,
           const std::vector<uint16_t> &index_data);

  ~Geometry();

  int GetVertexCount() const { return vertex_count_; }
  int GetIndexCount() const { return index_count_; }
  glm::vec3 GetScaleFactor() const { return scale_factor_; }
  BoundingBox GetBoundingBox() const { return bounding_box_; }

  void Bind(VkCommandBuffer cmd_buffer) const;

 private:
  benderkit::Device &device_;

  int vertex_count_;
  VkBuffer vertex_buf_;
  VkDeviceMemory vertex_buffer_device_memory_;

  int index_count_;
  VkBuffer index_buf_;
  VkDeviceMemory index_buffer_device_memory_;

  BoundingBox bounding_box_;
  glm::vec3 scale_factor_;

  void CreateVertexBuffer(const std::vector<packed_vertex> &vertex_data,
                          const std::vector<uint16_t> &index_data);

  BoundingBox GenerateBoundingBox( const std::vector<MeshVertex>&vertices ) const;
};

#endif //BENDER_BASE_GEOMETRY_H
