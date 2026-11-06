#pragma once

#include <vulkan/vulkan.h>

/*
 * Depth attachment state: group the image, its allocation and the view used for drawing.
 * One attachment is shared safely because this renderer allows only one frame in flight.
 * The caller owns this state and recreates it when the swapchain dimensions change.
 */
struct DepthState {
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
};

void destroyDepthState(VkDevice device, DepthState& depth);
bool createDepthState(VkPhysicalDevice physicalDevice, VkDevice device,
                      VkExtent2D extent, DepthState& depth);
