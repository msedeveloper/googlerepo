//
// Created by mattkw on 10/31/2019.
//

#include <glm/gtc/matrix_transform.hpp>

#include "mesh.h"
#include "material.h"
#include "shader_bindings.h"

Mesh::Mesh(Renderer *renderer,
           std::shared_ptr<Material> material,
           std::shared_ptr<Geometry> geometry) :
    renderer_(renderer), material_(material), geometry_(geometry) {
  position_ = glm::vec3(0.0f, 0.0f, 0.0f);
  rotation_ = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
  scale_ = glm::vec3(1.0f, 1.0f, 1.0f);

  mesh_buffer_ = std::make_unique<UniformBufferObject<ModelViewProjection>>(renderer_->GetDevice());
  createMeshDescriptorSetLayout();
  createMeshDescriptors();
}

Mesh::Mesh(Renderer *renderer,
           std::shared_ptr<Material> material,
           const std::vector<float> &vertexData,
           const std::vector<uint16_t> &indexData) :
    Mesh(renderer,
         material,
         std::make_shared<Geometry>(renderer->GetDevice(), vertexData, indexData)) {}

Mesh::Mesh(const Mesh &other, std::shared_ptr<Material> material) :
    renderer_(other.renderer_),
    material_(material),
    geometry_(other.geometry_),
    position_(other.position_),
    rotation_(other.rotation_),
    scale_(other.scale_){
  mesh_buffer_ = std::make_unique<UniformBufferObject<ModelViewProjection>>(renderer_->GetDevice());
  createMeshDescriptorSetLayout();
  createMeshDescriptors();
}

Mesh::Mesh(const Mesh &other, std::shared_ptr<Geometry> geometry) :
    renderer_(other.renderer_),
    material_(other.material_),
    geometry_(geometry),
    position_(other.position_),
    rotation_(other.rotation_),
    scale_(other.scale_) {
  mesh_buffer_ = std::make_unique<UniformBufferObject<ModelViewProjection>>(renderer_->GetDevice());
  createMeshDescriptorSetLayout();
  createMeshDescriptors();
}

Mesh::~Mesh() {
  cleanup();
}

void Mesh::cleanup() {
  vkDeviceWaitIdle(renderer_->GetVulkanDevice());
  vkDestroyPipeline(renderer_->GetVulkanDevice(), pipeline_, nullptr);
  vkDestroyPipelineLayout(renderer_->GetVulkanDevice(), layout_, nullptr);
  vkDestroyDescriptorSetLayout(renderer_->GetVulkanDevice(), mesh_descriptors_layout_, nullptr);
  mesh_buffer_.reset();
  pipeline_ = VK_NULL_HANDLE;
}

void Mesh::onResume(Renderer *renderer) {
  renderer_ = renderer;
  mesh_buffer_ = std::make_unique<UniformBufferObject<ModelViewProjection>>(renderer_->GetDevice());
  createMeshDescriptorSetLayout();
  createMeshDescriptors();
}

void Mesh::createMeshDescriptors() {
  std::vector<VkDescriptorSetLayout> layouts(renderer_->GetDevice().GetDisplayImages().size(),
                                             mesh_descriptors_layout_);

  VkDescriptorSetAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = renderer_->GetDescriptorPool();
  allocInfo.descriptorSetCount = renderer_->GetDevice().GetDisplayImages().size();
  allocInfo.pSetLayouts = layouts.data();

  mesh_descriptor_sets_.resize(renderer_->GetDevice().GetDisplayImages().size());
  CALL_VK(vkAllocateDescriptorSets(renderer_->GetVulkanDevice(),
                                   &allocInfo,
                                   mesh_descriptor_sets_.data()));

  for (size_t i = 0; i < renderer_->GetDevice().GetDisplayImages().size(); i++) {
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

    vkUpdateDescriptorSets(renderer_->GetVulkanDevice(),
                           descriptorWrites.size(),
                           descriptorWrites.data(),
                           0,
                           nullptr);

  }
}

void Mesh::createMeshPipeline(VkRenderPass renderPass) {
  std::array<VkDescriptorSetLayout, 3> layouts;

  layouts[BINDING_SET_MESH] = mesh_descriptors_layout_;
  layouts[BINDING_SET_MATERIAL] = material_->getMaterialDescriptorSetLayout();
  layouts[BINDING_SET_LIGHTS] = renderer_->GetLightsDescriptorSetLayout();

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .pNext = nullptr,
      .setLayoutCount = layouts.size(),
      .pSetLayouts = layouts.data(),
      .pushConstantRangeCount = 0,
      .pPushConstantRanges = nullptr,
  };

  CALL_VK(vkCreatePipelineLayout(renderer_->GetVulkanDevice(), &pipelineLayoutInfo, nullptr,
                                 &layout_))

  VkGraphicsPipelineCreateInfo pipelineInfo = renderer_->GetDefaultPipelineInfo(
      layout_,
      renderPass);

  material_->fillPipelineInfo(&pipelineInfo);

  CALL_VK(vkCreateGraphicsPipelines(
      renderer_->GetVulkanDevice(),
      renderer_->GetPipelineCache(), 1,
      &pipelineInfo,
      nullptr, &pipeline_));
}

void Mesh::createMeshDescriptorSetLayout() {
  VkDescriptorSetLayoutBinding uboLayoutBinding = {};
  uboLayoutBinding.binding = VERTEX_BINDING_MODEL_VIEW_PROJECTION;
  uboLayoutBinding.descriptorCount = 1;
  uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  uboLayoutBinding.pImmutableSamplers = nullptr;
  uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

  std::array<VkDescriptorSetLayoutBinding, 1> bindings = {uboLayoutBinding};
  VkDescriptorSetLayoutCreateInfo layoutInfo = {};
  layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfo.bindingCount = bindings.size();
  layoutInfo.pBindings = bindings.data();

  CALL_VK(vkCreateDescriptorSetLayout(renderer_->GetVulkanDevice(), &layoutInfo, nullptr,
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

  mesh_buffer_->update(frame_index, [&mvp, &model](auto &ubo) {
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

  vkCmdBindDescriptorSets(commandBuffer,
                          VK_PIPELINE_BIND_POINT_GRAPHICS,
                          layout_,
                          BINDING_SET_MESH,
                          1,
                          &mesh_descriptor_sets_[frame_index],
                          0,
                          nullptr);

  VkDescriptorSet lightsDescriptorSet = renderer_->GetLightsDescriptorSet(frame_index);

  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          layout_, BINDING_SET_LIGHTS, 1, &lightsDescriptorSet, 0, nullptr);

  VkDescriptorSet materialDescriptorSet = material_->getMaterialDescriptorSet(frame_index);

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

int Mesh::getTrianglesCount() const {
  return geometry_->getIndexCount() / 3;
}