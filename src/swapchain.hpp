#pragma once

#include <vulkan/vulkan.h>

#include <vector>

struct GLFWwindow;

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
