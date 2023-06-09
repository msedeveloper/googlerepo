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

#include <vector>
#include "bender_window_manager.h"
#include "bender_helpers.h"

// --------------------------------------------------------------
// Global variables
VkInstance benderInstance;
VkPhysicalDevice benderGpu;
VkDevice benderDevice;
VkQueue benderGraphicsQueue;
VkPhysicalDeviceMemoryProperties benderMemoryProperties;

VkSurfaceKHR benderSurface;
VkSwapchainKHR benderSwapchain;
VkExtent2D benderDisplaySize;
VkFormat benderDisplayFormat;
uint32_t benderSwapchainLength;

VkFramebuffer *benderFramebuffer;

VkImageView *displayViews;

void benderInitWindow(ANativeWindow *platformWindow,
                      VkApplicationInfo *appInfo) {
  LOGI("->BenderInitWindow()");

  std::vector<const char *> instance_extensions;
  std::vector<const char *> device_extensions;

  instance_extensions.push_back("VK_KHR_surface");
  instance_extensions.push_back("VK_KHR_android_surface");

  device_extensions.push_back("VK_KHR_swapchain");

  // **********************************************************
  // Create the Vulkan instance
  VkInstanceCreateInfo instanceCreateInfo{
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pNext = nullptr,
      .pApplicationInfo = appInfo,
      .enabledExtensionCount = static_cast<uint32_t>(instance_extensions.size()),
      .ppEnabledExtensionNames = instance_extensions.data(),
      .enabledLayerCount = 0,
      .ppEnabledLayerNames = nullptr,
  };
  CALL_VK(vkCreateInstance(&instanceCreateInfo, nullptr, &benderInstance));
  VkAndroidSurfaceCreateInfoKHR createInfo{
      .sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
      .pNext = nullptr,
      .flags = 0,
      .window = platformWindow};

  CALL_VK(vkCreateAndroidSurfaceKHR(benderInstance, &createInfo, nullptr,
                                    &benderSurface));
  LOGI("->BenderInitWindow() CreateAndroidSurfaceKHR");

  // **********************************************************
  // We will choose the right physical device to run our app
  // To do that we will:
  //   - Get the list of physical devices
  //   - Iterate on them and look for a device with:
  //     - Swap-chain available
  //     - A queue having a flag with GRAPHIC_BIT
  uint32_t gpuCount = 0;

  // First get the amount of GPU (nullptr as last argument and gpuCount == 0)
  CALL_VK(vkEnumeratePhysicalDevices(benderInstance, &gpuCount, nullptr));

  // Then get the list of physical devices
  VkPhysicalDevice tmpGpus[gpuCount];
  CALL_VK(vkEnumeratePhysicalDevices(benderInstance, &gpuCount, tmpGpus));

  // On Android, every device supports present; every queue supports graphics
  // operations. we loop to confirm it does support both graphics and compute
  bool gpuFound = false;
  for (uint32_t idx = 0; idx < gpuCount && !gpuFound; idx++) {
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(tmpGpus[idx], &count, nullptr);
    VkQueueFamilyProperties queueProps[count];
    vkGetPhysicalDeviceQueueFamilyProperties(tmpGpus[idx], &count, queueProps);
    for (uint32_t family = 0; family < count; family++) {
      if ((queueProps[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
          (queueProps[family].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
        // Got a queue supports both graphics and compute
        VkBool32 supPresent;
        CALL_VK(vkGetPhysicalDeviceSurfaceSupportKHR(
            tmpGpus[idx],
            family,
            benderSurface,
            &supPresent));
        // on Android queue need to support present
        assert(supPresent);
        benderGpu = tmpGpus[idx];
        // It should be the 1st queue family. the rest of the tutorial will
        // use 0 as family throughout [Android Only feature]
        assert(0 == family);
        gpuFound = true;
      }
    }
  }

  // confirm that it is the first gpu and the first queue family
  assert(gpuFound);
  assert(benderGpu == tmpGpus[0]);

  // **********************************************************
  // Create a logical device (
  float priorities[] = {1.0f,};
  VkDeviceQueueCreateInfo queueCreateInfo{
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .queueCount = 1,
      .queueFamilyIndex = 0,
      .pQueuePriorities = priorities,
  };

  VkDeviceCreateInfo deviceCreateInfo{
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = nullptr,
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &queueCreateInfo,
      .enabledLayerCount = 0,
      .ppEnabledLayerNames = nullptr,
      .enabledExtensionCount = static_cast<uint32_t>(device_extensions.size()),
      .ppEnabledExtensionNames = device_extensions.data(),
      .pEnabledFeatures = nullptr,
  };

  CALL_VK(vkCreateDevice(benderGpu, &deviceCreateInfo, nullptr,
                         &benderDevice));
  // **********************************************************
  // Get the graphic queue (used later to submit command buffer)
  vkGetDeviceQueue(benderDevice, 0, 0, &benderGraphicsQueue);

  LOGI("<-BenderInitWindow");
}

void benderCreateSwapChain() {
  LOGI("->benderCreateSwapChain");

  // **********************************************************
  // Get the surface capabilities because:
  //   - It contains the minimal and max length of the chain, we will need it
  //   - It's necessary to query the supported surface format (R8G8B8A8 for
  //   instance ...)
  VkSurfaceCapabilitiesKHR surfaceCapabilities;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(benderGpu, benderSurface,
                                            &surfaceCapabilities);

  LOGI("Capabilities:\n");
  LOGI("          image count: %u - %u\n", surfaceCapabilities.minImageCount,
       surfaceCapabilities.maxImageCount);
  LOGI("         array layers: %u\n", surfaceCapabilities.maxImageArrayLayers);
  LOGI("     image size (now): %dx%d\n",
       surfaceCapabilities.currentExtent.width,
       surfaceCapabilities.currentExtent.height);
  LOGI("  image size (extent): %dx%d - %dx%d\n",
       surfaceCapabilities.minImageExtent.width,
       surfaceCapabilities.minImageExtent.height,
       surfaceCapabilities.maxImageExtent.width,
       surfaceCapabilities.maxImageExtent.height);
  LOGI("                usage: %x\n", surfaceCapabilities.supportedUsageFlags);
  LOGI("    current transform: %u\n", surfaceCapabilities.currentTransform);
  LOGI("   allowed transforms: %x\n", surfaceCapabilities.supportedTransforms);
  LOGI("composite alpha flags: %u\n", surfaceCapabilities.currentTransform);
  // **********************************************************
  // Query the list of supported surface format and choose one we like
  uint32_t formatCount = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(benderGpu, benderSurface, &formatCount,
                                       nullptr);
  VkSurfaceFormatKHR *formats = new VkSurfaceFormatKHR[formatCount];
  vkGetPhysicalDeviceSurfaceFormatsKHR(benderGpu, benderSurface, &formatCount,
                                       formats);
  LOGI("Got %d formats", formatCount);

  uint32_t chosenFormat;
  for (chosenFormat = 0; chosenFormat < formatCount; chosenFormat++) {
    // This will go away once proper support for BGRA/RGBA swapping is
    // implemented in the driver.
    if (formats[chosenFormat].format == VK_FORMAT_R8G8B8A8_UNORM) break;
  }
  assert(chosenFormat < formatCount);

  // **********************************************************
  // Create a swap chain (here we choose the minimum available number of surface
  // in the chain)
  uint32_t queueFamily = 0;
  VkSwapchainCreateInfoKHR swapchainCreate{
      .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .pNext = nullptr,
      .surface = benderSurface,
      .minImageCount = surfaceCapabilities.minImageCount,
      .imageFormat = formats[chosenFormat].format,
      .imageColorSpace = formats[chosenFormat].colorSpace,
      .imageExtent = surfaceCapabilities.currentExtent,
      .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
      .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
      .imageArrayLayers = 1,
      .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .queueFamilyIndexCount = 1,
      .pQueueFamilyIndices = &queueFamily,
      .presentMode = VK_PRESENT_MODE_FIFO_KHR,
      .oldSwapchain = VK_NULL_HANDLE,
      .clipped = VK_FALSE,
  };

  benderDisplaySize = surfaceCapabilities.currentExtent;
  benderDisplayFormat = formats[chosenFormat].format;

  CALL_VK(vkCreateSwapchainKHR(benderDevice, &swapchainCreate, nullptr,
                               &benderSwapchain));
  // **********************************************************
  // Get the length of the created swap chain
  uint32_t displaySwapchainLength;
  CALL_VK(vkGetSwapchainImagesKHR(benderDevice, benderSwapchain,
                                  &displaySwapchainLength,
                                  nullptr));

  LOGI("Swapchain length: %u\n", displaySwapchainLength);
  benderSwapchainLength = displaySwapchainLength;
  // **********************************************************

  // Get Memory information and properties
  vkGetPhysicalDeviceMemoryProperties(benderGpu, &benderMemoryProperties);
  delete[] formats;
}

void benderCreateFrameBuffers(VkRenderPass &renderPass,
                              VkImageView depthView) {

  uint32_t SwapchainImagesCount = 0;
  CALL_VK(vkGetSwapchainImagesKHR(benderDevice, benderSwapchain,
                                  &SwapchainImagesCount,
                                  nullptr));

  VkImage *displayImages = new VkImage[SwapchainImagesCount];
  CALL_VK(vkGetSwapchainImagesKHR(benderDevice, benderSwapchain,
                                  &SwapchainImagesCount,
                                  displayImages));

  displayViews = new VkImageView[SwapchainImagesCount];
  for (uint32_t i = 0; i < SwapchainImagesCount; i++) {
    VkImageViewCreateInfo viewCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = nullptr,
        .image = displayImages[i],
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = benderDisplayFormat,
        .components =
            {
                .r = VK_COMPONENT_SWIZZLE_R,
                .g = VK_COMPONENT_SWIZZLE_G,
                .b = VK_COMPONENT_SWIZZLE_B,
                .a = VK_COMPONENT_SWIZZLE_A,
            },
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        .flags = 0,

    };

    CALL_VK(vkCreateImageView(benderDevice, &viewCreateInfo, nullptr,
                              &displayViews[i]));
  }

  delete[] displayImages;

  benderFramebuffer = new VkFramebuffer[benderSwapchainLength];

  for (uint32_t i = 0; i < benderSwapchainLength; i++) {
    VkImageView attachments[2] = {
        displayViews[i], depthView,
    };
    VkFramebufferCreateInfo fbCreateInfo{
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .pNext = nullptr,
        .renderPass = renderPass,
        .layers = 1,
        .attachmentCount = 1,  // 2 if using depth
        .pAttachments = attachments,
        .width = static_cast<uint32_t>(benderDisplaySize.width),
        .height = static_cast<uint32_t>(benderDisplaySize.height),
    };
    fbCreateInfo.attachmentCount = (depthView == VK_NULL_HANDLE ? 1 : 2);

    CALL_VK(vkCreateFramebuffer(benderDevice, &fbCreateInfo, nullptr,
                                &benderFramebuffer[i]));
  }
}

void benderCleanup() {
  for (int i = 0; i < benderSwapchainLength; i++) {
    vkDestroyFramebuffer(benderDevice, benderFramebuffer[i], nullptr);
    vkDestroyImageView(benderDevice, displayViews[i], nullptr);
  }
  delete[] displayViews;
  delete[] benderFramebuffer;

  vkDestroySwapchainKHR(benderDevice, benderSwapchain, nullptr);
}
