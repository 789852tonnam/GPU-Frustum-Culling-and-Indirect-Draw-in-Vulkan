#pragma once

#include <vulkan/vulkan.h>

#include <vector>

struct GLFWwindow;

/*
 * Presentation state: retain the current swapchain and the images available for display.
 * Views expose those images to rendering; format and extent describe their shared configuration.
 * Recreate this state when the window changes, after waiting for all users of the old images.
 */
struct SwapchainState {
    VkSwapchainKHR handle = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    std::vector<VkImage> images;
    std::vector<VkImageView> views;
};

void destroySwapchain(VkDevice device, SwapchainState& swapchain);
bool createSwapchain(VkPhysicalDevice physicalDevice, VkDevice device,
                     VkSurfaceKHR surface, GLFWwindow* window, SwapchainState& swapchain);
