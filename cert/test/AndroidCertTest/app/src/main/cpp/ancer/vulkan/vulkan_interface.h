#ifndef AGDC_ANCER_VULKANBASE_H_
#define AGDC_ANCER_VULKANBASE_H_

#include <cstdarg>
#include <cstdint>
#include <memory>
#include <cassert>
#include <vector>
#include <string>
#include <unordered_set>

#define VK_USE_PLATFORM_ANDROID_KHR 1

#include <vulkan/vulkan.h>

namespace ancer {
namespace vulkan {

#define VK_RETURN_FAIL(x) do { \
  Result _err{x}; \
  if(!_err.is_success()) return _err; \
} while(0)

#define VK_GOTO_FAIL(x) do { \
  Result _err{x}; \
  if(!_err.is_success()) goto fail; \
} while(0)

/**
 * A X macro for iterating through Vulkan types with a matching enum
 */
#define VK_OBJECTS_X(X) \
  X(VK_DEBUG_REPORT_OBJECT_TYPE_UNKNOWN_EXT, uint64_t) \
  X(VK_DEBUG_REPORT_OBJECT_TYPE_INSTANCE_EXT, VkInstance) \
  X(VK_DEBUG_REPORT_OBJECT_TYPE_PHYSICAL_DEVICE_EXT, VkPhysicalDevice) \
  X(VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_EXT, VkDevice) \
  X(VK_DEBUG_REPORT_OBJECT_TYPE_QUEUE_EXT, VkQueue) \
  X(VK_DEBUG_REPORT_OBJECT_TYPE_SEMAPHORE_EXT, VkSemaphore) \
  X(VK_DEBUG_REPORT_OBJECT_TYPE_COMMAND_BUFFER_EXT, VkCommandBuffer) \
  X(VK_DEBUG_REPORT_OBJECT_TYPE_FENCE_EXT, VkFence) \
  X(VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_MEMORY_EXT, VkDeviceMemory) \
  X(VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT, VkBuffer) \
  X(VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT, VkImage) \
  X(VK_DEBUG_REPORT_OBJECT_TYPE_EVENT_EXT, VkEvent) \
  X(VK_DEBUG_REPORT_OBJECT_TYPE_QUERY_POOL_EXT, VkQueryPool) \
  X(VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_VIEW_EXT, VkBufferView) \
  X(VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_VIEW_EXT, VkImageView) \
  X(VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT, VkShaderModule) \
  X(VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_CACHE_EXT, VkPipelineCache) \
  X(VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_LAYOUT_EXT, VkPipelineLayout) \
  X(VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT, VkRenderPass) \
  X(VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, VkPipeline) \
  X(VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT_EXT, VkDescriptorSetLayout) \
  X(VK_DEBUG_REPORT_OBJECT_TYPE_SAMPLER_EXT, VkSampler) \
  X(VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_POOL_EXT, VkDescriptorPool) \
  X(VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_EXT, VkDescriptorSet) \
  X(VK_DEBUG_REPORT_OBJECT_TYPE_FRAMEBUFFER_EXT, VkFramebuffer) \
  X(VK_DEBUG_REPORT_OBJECT_TYPE_COMMAND_POOL_EXT, VkCommandPool) \
  X(VK_DEBUG_REPORT_OBJECT_TYPE_SURFACE_KHR_EXT, VkSurfaceKHR) \
  X(VK_DEBUG_REPORT_OBJECT_TYPE_SWAPCHAIN_KHR_EXT, VkSwapchainKHR) \
  X(VK_DEBUG_REPORT_OBJECT_TYPE_DEBUG_REPORT_CALLBACK_EXT_EXT, VkDebugReportCallbackEXT) \
  X(VK_DEBUG_REPORT_OBJECT_TYPE_DISPLAY_KHR_EXT, VkDisplayKHR) \
  X(VK_DEBUG_REPORT_OBJECT_TYPE_DISPLAY_MODE_KHR_EXT, VkDisplayModeKHR) \
  X(VK_DEBUG_REPORT_OBJECT_TYPE_OBJECT_TABLE_NVX_EXT, VkObjectTableNVX) \
  X(VK_DEBUG_REPORT_OBJECT_TYPE_INDIRECT_COMMANDS_LAYOUT_NVX_EXT, VkIndirectCommandsLayoutNVX) \
  X(VK_DEBUG_REPORT_OBJECT_TYPE_VALIDATION_CACHE_EXT_EXT, VkValidationCacheEXT) \
  X(VK_DEBUG_REPORT_OBJECT_TYPE_SAMPLER_YCBCR_CONVERSION_EXT, VkSamplerYcbcrConversionKHR) \
  X(VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_EXT, VkDescriptorUpdateTemplate) \
  X(VK_DEBUG_REPORT_OBJECT_TYPE_ACCELERATION_STRUCTURE_NV_EXT, VkAccelerationStructureNV) \
  X(VK_DEBUG_REPORT_OBJECT_TYPE_DEBUG_REPORT_EXT, VkDebugReportEXT) \
  X(VK_DEBUG_REPORT_OBJECT_TYPE_VALIDATION_CACHE_EXT, VkValidationCache) \
  X(VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_KHR_EXT, VkDescriptorUpdateTemplateKHR) \
  X(VK_DEBUG_REPORT_OBJECT_TYPE_SAMPLER_YCBCR_CONVERSION_KHR_EXT, VkSamplerYcbcrConversionKHR) \

class GraphicsContext;
class Vulkan;

/**
 * A custom VkResult that ensures that every error is bubbled up to the a way
 * that can be handled correctly for the task at hand.
 */
struct [[nodiscard]] Result {
  Result() : vk_result(VK_SUCCESS) { }

  explicit Result(VkResult vk) : vk_result(vk) { }

  Result & operator = (VkResult vk) {
    vk_result = vk;
    return *this;
  }

  VkResult vk_result;

  inline bool is_success() { return vk_result == VK_SUCCESS; }

  static Result kSuccess;
};

/**
 * Requirements for initializing Vulkan. This influences picking the physical
 * device, loading layers and extensions.
 */
class VulkanRequirements {
 public:
  VulkanRequirements();

  inline void Debug(bool value = true) {
    debug = value;
  }

  inline void InstanceLayer(std::string layer) {
    instance_layers.insert(std::move(layer));
  }

  inline void InstanceExtension(std::string extension) {
    instance_extensions.insert(std::move(extension));
  }

#define FEATURE(X, Y) \
  inline void Y(bool value = true) { \
    features.X = value ? VK_TRUE : VK_FALSE; \
  }

  FEATURE(robustBufferAccess, RobustBufferAccess)
  FEATURE(fullDrawIndexUint32, FullDrawIndexUint32)
  FEATURE(imageCubeArray, ImageCubeArray)
  FEATURE(independentBlend, IndependentBlend)
  FEATURE(geometryShader, GeometryShader)
  FEATURE(tessellationShader, TessellationShader)
  FEATURE(sampleRateShading, SampleRateShading)
  FEATURE(dualSrcBlend, DualSrcBlend)
  FEATURE(logicOp, LogicOp)
  FEATURE(multiDrawIndirect, MultiDrawIndirect)
  FEATURE(drawIndirectFirstInstance, DrawIndirectFirstInstance)
  FEATURE(depthClamp, DepthClamp)
  FEATURE(depthBiasClamp, DepthBiasClamp)
  FEATURE(fillModeNonSolid, FillModeNonSolid)
  FEATURE(depthBounds, DepthBounds)
  FEATURE(wideLines, WideLines)
  FEATURE(largePoints, LargePoints)
  FEATURE(alphaToOne, AlphaToOne)
  FEATURE(multiViewport, MultiViewport)
  FEATURE(samplerAnisotropy, SamplerAnisotropy)
  FEATURE(textureCompressionETC2, TextureCompressionETC2)
  FEATURE(textureCompressionASTC_LDR, TextureCompressionASTC_LDR)
  FEATURE(textureCompressionBC, TextureCompressionBC)
  FEATURE(occlusionQueryPrecise, OcclusionQueryPrecise)
  FEATURE(pipelineStatisticsQuery, PipelineStatisticsQuery)
  FEATURE(vertexPipelineStoresAndAtomics, VertexPipelineStoresAndAtomics)
  FEATURE(fragmentStoresAndAtomics, FragmentStoresAndAtomics)
  FEATURE(shaderTessellationAndGeometryPointSize, ShaderTessellationAndGeometryPointSize)
  FEATURE(shaderImageGatherExtended, ShaderImageGatherExtended)
  FEATURE(shaderStorageImageExtendedFormats, ShaderStorageImageExtendedFormats)
  FEATURE(shaderStorageImageMultisample, ShaderStorageImageMultisample)
  FEATURE(shaderStorageImageReadWithoutFormat, ShaderStorageImageReadWithoutFormat)
  FEATURE(shaderStorageImageWriteWithoutFormat, ShaderStorageImageWriteWithoutFormat)
  FEATURE(shaderUniformBufferArrayDynamicIndexing, ShaderUniformBufferArrayDynamicIndexing)
  FEATURE(shaderSampledImageArrayDynamicIndexing, ShaderSampledImageArrayDynamicIndexing)
  FEATURE(shaderStorageBufferArrayDynamicIndexing, ShaderStorageBufferArrayDynamicIndexing)
  FEATURE(shaderStorageImageArrayDynamicIndexing, ShaderStorageImageArrayDynamicIndexing)
  FEATURE(shaderClipDistance, ShaderClipDistance)
  FEATURE(shaderCullDistance, ShaderCullDistance)
  FEATURE(shaderFloat64, ShaderFloat64)
  FEATURE(shaderInt64, ShaderInt64)
  FEATURE(shaderInt16, ShaderInt16)
  FEATURE(shaderResourceResidency, ShaderResourceResidency)
  FEATURE(shaderResourceMinLod, ShaderResourceMinLod)
  FEATURE(sparseBinding, SparseBinding)
  FEATURE(sparseResidencyBuffer, SparseResidencyBuffer)
  FEATURE(sparseResidencyImage2D, SparseResidencyImage2D)
  FEATURE(sparseResidencyImage3D, SparseResidencyImage3D)
  FEATURE(sparseResidency2Samples, SparseResidency2Samples)
  FEATURE(sparseResidency4Samples, SparseResidency4Samples)
  FEATURE(sparseResidency8Samples, SparseResidency8Samples)
  FEATURE(sparseResidency16Samples, SparseResidency16Samples)
  FEATURE(sparseResidencyAliased, SparseResidencyAliased)
  FEATURE(variableMultisampleRate, VariableMultisampleRate)
  FEATURE(inheritedQueries, InheritedQueries)

#undef FEATURE

  inline void DeviceLayer(std::string layer) {
    device_layers.insert(std::move(layer));
  }

  inline void DeviceExtension(std::string extension) {
    device_extensions.insert(std::move(extension));
  }

  /**
   * Sets common layers and extensions used to create a Swapchain
   */
  void Swapchain();

 private:
  friend class Vulkan;

  bool debug;

  std::unordered_set<std::string> instance_layers;
  std::unordered_set<std::string> instance_extensions;

  VkPhysicalDeviceFeatures features;

  std::unordered_set<std::string> device_layers;
  std::unordered_set<std::string> device_extensions;
};

/**
 * A wrapper over the VkFence object. It is reference counted where a command
 * buffer is the holder of these references. When a command buffer is submitted,
 * it is done with a Fence. Several command buffers can use the same fence. When
 * a command buffer would be reused, first it waits for it's associated fence.
 * thus letting all command buffers using the same fence that it has already
 * been waited for on the CPU side.
 *
 * Fences also control lazy resource deallocation/destruction. The root Vulkan
 * object knows what 'generation' a resource should be destroyed on and when a
 * Fence is successfully waited on it notifies the Vulkan object to advance the
 * 'generation'.
 */
struct Fence {
 private:
  friend class Vulkan;

  struct Data {
    bool waited;
    uint32_t frame;
    uint32_t references;
    VkFence fence;
  };

  std::shared_ptr<Data> data;
};

/**
 * Simplify required memory properties to these
 */
enum VkResourceUse {
  VK_RESOURCE_GPU,
  VK_RESOURCE_CPU_TO_GPU,
  VK_RESOURCE_GPU_TO_CPU
};

/**
 * A memory allocation from our allocator
 *
 * each allocation is a block of memory from a larger vulkan memory object. Due
 * to alignments, we store the start of a block and the offset in that block to
 * the usable memory.
 */
class MemoryAllocation {
 public:
  inline const VkDeviceMemory &Memory() const {
    return _memory;
  }

  inline VkDeviceSize Offset() const {
    return _offset;
  }

  inline VkDeviceSize TotalSize() const {
    return _end - _start;
  }

  inline VkDeviceSize Size() const {
    return _end - _offset;
  }

  inline void *Map() {
    return _map;
  }

 private:
  friend class Vulkan;

  VkDeviceMemory _memory;
  void * _map;
  VkDeviceSize _start;
  VkDeviceSize _offset;
  VkDeviceSize _end;
};

/**
 *
 */
struct UploadBuffer {
  VkDeviceSize offset;
  std::vector<VkBufferCopy> regions;
};

/**
 * The Vulkan object is primarily just a shared_ptr to Vulkan::Data. The Data
 * is all the data that is shared. Functions in Vulkan are for managing the
 * Data. The Vulkan::Data also keeps the function pointers into Vulkan and it's
 * extensions.
 *
 * Every object is tracked for destruction, Fences control the timing of
 * deallocating/destroying objects.
 *
 * Below are special considerations currently in place or planned for in the
 * future:
 *
 * Fence. These are allocated and a free list is used to minimize allocations.
 *
 * Queue. Created and managed by the Vulkan::Data. Nothing should touch a Queue
 * besides the logic in Vulkan.
 *
 * RenderPass. Some information about a VkRenderPass and it's sub passes are
 * stored here for the creation of `GraphicsPipeline`s
 *
 * PipelineCache. A cache for pipeline creation.
 */
class Vulkan {
 public:
  struct Data;

  Result Initialize(const VulkanRequirements & requirements);
  void Shutdown();

  inline VkInstance Intance() const {
    return vk->instance;
  }

  inline VkPhysicalDevice PhysicalDevice() const {
    return vk->physical_device;
  }

  /**
   * Return the Data, prefer using `operator ->`
   */
  inline Data & get() {
    return *vk.get();
  }

  /**
   * Since Vulkan is mostly a shared_ptr<Data>, act like one a little bit.
   */
  inline Data * operator -> () {
    return vk.get();
  }
  DEBUG_NAME_OBJECT(VkCommandPool, COMMAND_POOL_EXT)
  DEBUG_NAME_OBJECT(VkCommandBuffer, COMMAND_BUFFER_EXT)

#define DEBUG_NAME_OBJECT_X(ENUM, TYPE) \
  /** \
   * Add a name to this object for debugging \
   */ \
  inline Result DebugName(TYPE object, const char * format, ...) { \
    va_list ap; \
    va_start(ap, format); \
    Result res; \
    res = DebugName(ENUM, reinterpret_cast<uint64_t>(object), format, ap); \
    va_end(ap); \
    return res; \
  }

  VK_OBJECTS_X(DEBUG_NAME_OBJECT_X)

#undef DEBUG_NAME_OBJECT_X

  /**
   * Did we initialize with this instance extension, extension names can be
   * found in vulkan.h
   */
  inline bool HaveInstanceExtension(const char * name) {
    return vk->enabled_instance_extensions.count(name) > 0;
  }

  /**
   * Did we initialize with this device extension, extension names can be found
   * in vulkan.h
   */
  inline bool HaveDeviceExtension(const char * name) {
    return vk->enabled_instance_extensions.count(name) > 0;
  }

  /**
   * Allocate a Fence to be used with SubmitToQueue. The same fence can be used
   * multiple times in a frame.
   */
  Result AllocateFence(Fence &fence);

  /**
   * Wait for a Fence to complete. if force is false this becomes a query for
   * the Fence state and does cleanup after if it has been set.
   */
  Result WaitForFence(Fence &fence, bool &completed, bool force = true);

  /**
   * Submit a GraphicsContext to the graphics VkQueue. This is the best wayt to
   * interact with the graphics VkQueue.
   */
  inline Result SubmitToQueue(GraphicsContext &context, Fence &fence) {
    return SubmitToQueue(vk->graphics_queue, context, fence);
  }

  /**
   * Submit a Context to a VkQueue. Generic form of SubmitToQueue
   */
  Result SubmitToQueue(VkQueue queue, Context &context, Fence &fence);

  // ==========================================================================
  Result AllocateMemory(VkMemoryRequirements &requirements,
                        VkMemoryPropertyFlags flags,
                        MemoryAllocation &allocation);

  void Free(const MemoryAllocation &allocation);

  // ==========================================================================
  Result AcquireTemporaryCommandBuffer(uint32_t qfi,
                                       VkCommandBuffer &cmd_buffer);

  Result ReleaseTemporaryCommandBuffer(VkCommandBuffer cmd_buffer);

  // ==========================================================================
  Result UploadStart(VkBuffer buffer, VkDeviceSize size,
                     UploadBuffer &context);

  void UploadCopy(UploadBuffer &context, VkDeviceSize dst_offset,
                  VkDeviceSize size, const void * data);

  Result UploadFinish(UploadBuffer &context);

 public:
  struct Destroy {
    VkDebugReportObjectTypeEXT object_type;
    uint64_t object;
    uint32_t frame;
  };

  struct Data {
    std::unordered_set<std::string> available_instance_layers;
    std::unordered_set<std::string> available_instance_extensions;
    std::unordered_set<std::string> enabled_instance_layers;
    std::unordered_set<std::string> enabled_instance_extensions;

    VkInstance instance;

    bool debug_enabled;

    VkPhysicalDevice physical_device;
    VkPhysicalDeviceProperties physical_device_properties;
    VkPhysicalDeviceMemoryProperties physical_device_memory_properties;
    VkPhysicalDeviceFeatures physical_device_features;

    uint32_t window_width;
    uint32_t window_height;

    VkSurfaceKHR surface;
    VkSurfaceCapabilitiesKHR surface_capabilities;
    std::vector<VkPresentModeKHR> surface_present_modes;
    std::vector<VkSurfaceFormatKHR> surface_formats;

    uint32_t graphics_queue_family_index;
    VkQueue graphics_queue;

    uint32_t compute_queue_family_index;
    VkQueue compute_queue;

    uint32_t transfer_queue_family_index;
    VkQueue transfer_queue;

    uint32_t present_queue_family_index;
    VkQueue present_queue;

    std::unordered_set<std::string> available_device_layers;
    std::unordered_set<std::string> available_device_extensions;
    std::unordered_set<std::string> enabled_device_layers;
    std::unordered_set<std::string> enabled_device_extensions;

    VkDevice device;

    uint32_t frame;

    std::vector<Fence> free_fences;

    // TODO(sarahburns@gmail.com):
    std::vector<Destroy> destroy;

    // 1.0 core functions
    PFN_vkCreateInstance createInstance;
    PFN_vkDestroyInstance destroyInstance;
    PFN_vkEnumeratePhysicalDevices enumeratePhysicalDevices;
    PFN_vkGetPhysicalDeviceFeatures getPhysicalDeviceFeatures;
    PFN_vkGetPhysicalDeviceFormatProperties getPhysicalDeviceFormatProperties;
    PFN_vkGetPhysicalDeviceImageFormatProperties getPhysicalDeviceImageFormatProperties;
    PFN_vkGetPhysicalDeviceProperties getPhysicalDeviceProperties;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties getPhysicalDeviceQueueFamilyProperties;
    PFN_vkGetPhysicalDeviceMemoryProperties getPhysicalDeviceMemoryProperties;
    PFN_vkGetInstanceProcAddr getInstanceProcAddr;
    PFN_vkGetDeviceProcAddr getDeviceProcAddr;
    PFN_vkCreateDevice createDevice;
    PFN_vkDestroyDevice destroyDevice;
    PFN_vkEnumerateInstanceExtensionProperties enumerateInstanceExtensionProperties;
    PFN_vkEnumerateDeviceExtensionProperties enumerateDeviceExtensionProperties;
    PFN_vkEnumerateInstanceLayerProperties enumerateInstanceLayerProperties;
    PFN_vkEnumerateDeviceLayerProperties enumerateDeviceLayerProperties;
    PFN_vkGetDeviceQueue getDeviceQueue;
    PFN_vkQueueSubmit queueSubmit;
    PFN_vkQueueWaitIdle queueWaitIdle;
    PFN_vkDeviceWaitIdle deviceWaitIdle;
    PFN_vkAllocateMemory allocateMemory;
    PFN_vkFreeMemory freeMemory;
    PFN_vkMapMemory mapMemory;
    PFN_vkUnmapMemory unmapMemory;
    PFN_vkFlushMappedMemoryRanges flushMappedMemoryRanges;
    PFN_vkInvalidateMappedMemoryRanges invalidateMappedMemoryRanges;
    PFN_vkGetDeviceMemoryCommitment getDeviceMemoryCommitment;
    PFN_vkBindBufferMemory bindBufferMemory;
    PFN_vkBindImageMemory bindImageMemory;
    PFN_vkGetBufferMemoryRequirements getBufferMemoryRequirements;
    PFN_vkGetImageMemoryRequirements getImageMemoryRequirements;
    PFN_vkGetImageSparseMemoryRequirements getImageSparseMemoryRequirements;
    PFN_vkGetPhysicalDeviceSparseImageFormatProperties getPhysicalDeviceSparseImageFormatProperties;
    PFN_vkQueueBindSparse queueBindSparse;
    PFN_vkCreateFence createFence;
    PFN_vkDestroyFence destroyFence;
    PFN_vkResetFences resetFences;
    PFN_vkGetFenceStatus getFenceStatus;
    PFN_vkWaitForFences waitForFences;
    PFN_vkCreateSemaphore createSemaphore;
    PFN_vkDestroySemaphore destroySemaphore;
    PFN_vkCreateEvent createEvent;
    PFN_vkDestroyEvent destroyEvent;
    PFN_vkGetEventStatus getEventStatus;
    PFN_vkSetEvent setEvent;
    PFN_vkResetEvent resetEvent;
    PFN_vkCreateQueryPool createQueryPool;
    PFN_vkDestroyQueryPool destroyQueryPool;
    PFN_vkGetQueryPoolResults getQueryPoolResults;
    PFN_vkCreateBuffer createBuffer;
    PFN_vkDestroyBuffer destroyBuffer;
    PFN_vkCreateBufferView createBufferView;
    PFN_vkDestroyBufferView destroyBufferView;
    PFN_vkCreateImage createImage;
    PFN_vkDestroyImage destroyImage;
    PFN_vkGetImageSubresourceLayout getImageSubresourceLayout;
    PFN_vkCreateImageView createImageView;
    PFN_vkDestroyImageView destroyImageView;
    PFN_vkCreateShaderModule createShaderModule;
    PFN_vkDestroyShaderModule destroyShaderModule;
    PFN_vkCreatePipelineCache createPipelineCache;
    PFN_vkDestroyPipelineCache destroyPipelineCache;
    PFN_vkGetPipelineCacheData getPipelineCacheData;
    PFN_vkMergePipelineCaches mergePipelineCaches;
    PFN_vkCreateGraphicsPipelines createGraphicsPipelines;
    PFN_vkCreateComputePipelines createComputePipelines;
    PFN_vkDestroyPipeline destroyPipeline;
    PFN_vkCreatePipelineLayout createPipelineLayout;
    PFN_vkDestroyPipelineLayout destroyPipelineLayout;
    PFN_vkCreateSampler createSampler;
    PFN_vkDestroySampler destroySampler;
    PFN_vkCreateDescriptorSetLayout createDescriptorSetLayout;
    PFN_vkDestroyDescriptorSetLayout destroyDescriptorSetLayout;
    PFN_vkCreateDescriptorPool createDescriptorPool;
    PFN_vkDestroyDescriptorPool destroyDescriptorPool;
    PFN_vkResetDescriptorPool resetDescriptorPool;
    PFN_vkAllocateDescriptorSets allocateDescriptorSets;
    PFN_vkFreeDescriptorSets freeDescriptorSets;
    PFN_vkUpdateDescriptorSets updateDescriptorSets;
    PFN_vkCreateFramebuffer createFramebuffer;
    PFN_vkDestroyFramebuffer destroyFramebuffer;
    PFN_vkCreateRenderPass createRenderPass;
    PFN_vkDestroyRenderPass destroyRenderPass;
    PFN_vkGetRenderAreaGranularity getRenderAreaGranularity;
    PFN_vkCreateCommandPool createCommandPool;
    PFN_vkDestroyCommandPool destroyCommandPool;
    PFN_vkResetCommandPool resetCommandPool;
    PFN_vkAllocateCommandBuffers allocateCommandBuffers;
    PFN_vkFreeCommandBuffers freeCommandBuffers;
    PFN_vkBeginCommandBuffer beginCommandBuffer;
    PFN_vkEndCommandBuffer endCommandBuffer;
    PFN_vkResetCommandBuffer resetCommandBuffer;
    PFN_vkCmdBindPipeline cmdBindPipeline;
    PFN_vkCmdSetViewport cmdSetViewport;
    PFN_vkCmdSetScissor cmdSetScissor;
    PFN_vkCmdSetLineWidth cmdSetLineWidth;
    PFN_vkCmdSetDepthBias cmdSetDepthBias;
    PFN_vkCmdSetBlendConstants cmdSetBlendConstants;
    PFN_vkCmdSetDepthBounds cmdSetDepthBounds;
    PFN_vkCmdSetStencilCompareMask cmdSetStencilCompareMask;
    PFN_vkCmdSetStencilWriteMask cmdSetStencilWriteMask;
    PFN_vkCmdSetStencilReference cmdSetStencilReference;
    PFN_vkCmdBindDescriptorSets cmdBindDescriptorSets;
    PFN_vkCmdBindIndexBuffer cmdBindIndexBuffer;
    PFN_vkCmdBindVertexBuffers cmdBindVertexBuffers;
    PFN_vkCmdDraw cmdDraw;
    PFN_vkCmdDrawIndexed cmdDrawIndexed;
    PFN_vkCmdDrawIndirect cmdDrawIndirect;
    PFN_vkCmdDrawIndexedIndirect cmdDrawIndexedIndirect;
    PFN_vkCmdDispatch cmdDispatch;
    PFN_vkCmdDispatchIndirect cmdDispatchIndirect;
    PFN_vkCmdCopyBuffer cmdCopyBuffer;
    PFN_vkCmdCopyImage cmdCopyImage;
    PFN_vkCmdBlitImage cmdBlitImage;
    PFN_vkCmdCopyBufferToImage cmdCopyBufferToImage;
    PFN_vkCmdCopyImageToBuffer cmdCopyImageToBuffer;
    PFN_vkCmdUpdateBuffer cmdUpdateBuffer;
    PFN_vkCmdFillBuffer cmdFillBuffer;
    PFN_vkCmdClearColorImage cmdClearColorImage;
    PFN_vkCmdClearDepthStencilImage cmdClearDepthStencilImage;
    PFN_vkCmdClearAttachments cmdClearAttachments;
    PFN_vkCmdResolveImage cmdResolveImage;
    PFN_vkCmdSetEvent cmdSetEvent;
    PFN_vkCmdResetEvent cmdResetEvent;
    PFN_vkCmdWaitEvents cmdWaitEvents;
    PFN_vkCmdPipelineBarrier cmdPipelineBarrier;
    PFN_vkCmdBeginQuery cmdBeginQuery;
    PFN_vkCmdEndQuery cmdEndQuery;
    PFN_vkCmdResetQueryPool cmdResetQueryPool;
    PFN_vkCmdWriteTimestamp cmdWriteTimestamp;
    PFN_vkCmdCopyQueryPoolResults cmdCopyQueryPoolResults;
    PFN_vkCmdPushConstants cmdPushConstants;
    PFN_vkCmdBeginRenderPass cmdBeginRenderPass;
    PFN_vkCmdNextSubpass cmdNextSubpass;
    PFN_vkCmdEndRenderPass cmdEndRenderPass;
    PFN_vkCmdExecuteCommands cmdExecuteCommands;

    PFN_vkDestroySurfaceKHR destroySurfaceKHR;
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR getPhysicalDeviceSurfaceSupportKHR;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR getPhysicalDeviceSurfaceCapabilitiesKHR;
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR getPhysicalDeviceSurfaceFormatsKHR;
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR getPhysicalDeviceSurfacePresentModesKHR;

#ifdef VK_USE_PLATFORM_ANDROID_KHR
    PFN_vkCreateAndroidSurfaceKHR createAndroidSurfaceKHR;
#endif

    PFN_vkCreateSwapchainKHR createSwapchainKHR;
    PFN_vkDestroySwapchainKHR destroySwapchainKHR;
    PFN_vkGetSwapchainImagesKHR getSwapchainImagesKHR;
    PFN_vkAcquireNextImageKHR acquireNextImageKHR;
    PFN_vkQueuePresentKHR queuePresentKHR;
    PFN_vkGetDeviceGroupPresentCapabilitiesKHR getDeviceGroupPresentCapabilitiesKHR;
    PFN_vkGetDeviceGroupSurfacePresentModesKHR getDeviceGroupSurfacePresentModesKHR;
    PFN_vkGetPhysicalDevicePresentRectanglesKHR getPhysicalDevicePresentRectanglesKHR;
    PFN_vkAcquireNextImage2KHR acquireNextImage2KHR;
  };

  std::shared_ptr<Data> vk{nullptr};

 private:
  Result InitEntry(const VulkanRequirements & requirements);
  Result InitInstanceExtensions(const VulkanRequirements & requirements);
  Result InitInstance(const VulkanRequirements & requirements);
  Result InitDebugReporting(const VulkanRequirements & requirements);
  Result InitPhysicalDevice(const VulkanRequirements & requirements);
  Result InitSurface(const VulkanRequirements & requirements);
  Result InitDeviceExtensions(const VulkanRequirements & requirements);
  Result InitDevice(const VulkanRequirements & requirements);

  Result DebugName(VkDebugReportObjectTypeEXT object_type, uint64_t object,
                   const char * format, va_list args);

  void DoDestroy(Destroy & destroy);
};

};
};

#endif // AGDC_ANCER_VULKANBASE_H_

