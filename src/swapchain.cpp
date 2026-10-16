#define GLFW_INCLUDE_NONE
#include "swapchain.hpp"
#include "vulkan_utils.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <iostream>
#include <limits>
#include <optional>

void destroySwapchain(VkDevice device, SwapchainState& swapchain) {
    for (const VkImageView view : swapchain.views) {
        vkDestroyImageView(device, view, nullptr);
    }
    if (swapchain.handle != VK_NULL_HANDLE) {
        // The swapchain owns its images; do not call vkDestroyImage on them.
        vkDestroySwapchainKHR(device, swapchain.handle, nullptr);
    }
    swapchain = {};
}

bool createSwapchain(VkPhysicalDevice physicalDevice, VkDevice device,
                     VkSurfaceKHR surface, GLFWwindow* window, SwapchainState& swapchain) {
    VkSurfaceCapabilitiesKHR capabilities{};
    VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);
    if (!checkVulkan(result, "Read swapchain surface capabilities")) {
        return false;
    }

    uint32_t formatCount = 0;
    std::vector<VkSurfaceFormatKHR> formats;
    do {
        result = vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
        if (!checkVulkan(result, "Count swapchain surface formats")) {
            return false;
        }
        if (formatCount == 0) {
            std::cerr << "Surface formats unavailable.\n";
            return false;
        }
        formats.resize(formatCount);
        result = vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());
    } while (result == VK_INCOMPLETE);
    if (!checkVulkan(result, "Read swapchain surface formats")) {
        return false;
    }
    if (formatCount == 0) {
        std::cerr << "Surface formats unavailable.\n";
        return false;
    }
    formats.resize(formatCount);

    // Prefer BGRA8 sRGB; otherwise use the first color-renderable surface format.
    std::optional<VkSurfaceFormatKHR> surfaceFormat;
    for (const VkSurfaceFormatKHR candidate : formats) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice, candidate.format, &properties);
        if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) == 0) {
            continue;
        }
        if (!surfaceFormat) {
            surfaceFormat = candidate;
        }
        if (candidate.format == VK_FORMAT_B8G8R8A8_SRGB &&
            candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            surfaceFormat = candidate;
            break;
        }
    }
    if (!surfaceFormat) {
        std::cerr << "No color-renderable surface format found.\n";
        return false;
    }

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    if (width <= 0 || height <= 0) {
        std::cerr << "Cannot create swapchain for a zero-size framebuffer.\n";
        return false;
    }
    VkExtent2D extent = capabilities.currentExtent;
    if (extent.width == std::numeric_limits<uint32_t>::max()) {
        extent.width = std::clamp(static_cast<uint32_t>(width),
            capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        extent.height = std::clamp(static_cast<uint32_t>(height),
            capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    }
    if (extent.width == 0 || extent.height == 0) {
        std::cerr << "Surface currently has a zero-size extent.\n";
        return false;
    }

    uint32_t imageCount = capabilities.minImageCount;
    if (imageCount < std::numeric_limits<uint32_t>::max() &&
        (capabilities.maxImageCount == 0 || imageCount < capabilities.maxImageCount)) {
        ++imageCount;
    }
    // maxImageCount == 0 means there is no surface-imposed maximum.
    const VkCompositeAlphaFlagBitsKHR alphaModes[] = {
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR
    };
    std::optional<VkCompositeAlphaFlagBitsKHR> compositeAlpha;
    for (const VkCompositeAlphaFlagBitsKHR mode : alphaModes) {
        if ((capabilities.supportedCompositeAlpha & mode) != 0) {
            compositeAlpha = mode;
            break;
        }
    }
    if (!compositeAlpha ||
        (capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0) {
        std::cerr << "Surface does not support the requested swapchain usage or alpha mode.\n";
        return false;
    }

    VkSwapchainCreateInfoKHR swapchainInfo{};
    swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainInfo.surface = surface;
    swapchainInfo.minImageCount = imageCount;
    swapchainInfo.imageFormat = surfaceFormat->format;
    swapchainInfo.imageColorSpace = surfaceFormat->colorSpace;
    swapchainInfo.imageExtent = extent;
    swapchainInfo.imageArrayLayers = 1;
    swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainInfo.preTransform = capabilities.currentTransform;
    swapchainInfo.compositeAlpha = *compositeAlpha;
    swapchainInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR; // Guaranteed for ordinary surfaces.
    swapchainInfo.clipped = VK_TRUE;

    result = vkCreateSwapchainKHR(device, &swapchainInfo, nullptr, &swapchain.handle);
    if (!checkVulkan(result, "Create swapchain")) {
        return false;
    }
    swapchain.format = surfaceFormat->format;
    swapchain.extent = extent;

    // The implementation may provide more images than the requested minimum.
    do {
        result = vkGetSwapchainImagesKHR(device, swapchain.handle, &imageCount, nullptr);
        if (!checkVulkan(result, "Count swapchain images")) {
            destroySwapchain(device, swapchain);
            return false;
        }
        if (imageCount == 0) {
            std::cerr << "No swapchain images available.\n";
            destroySwapchain(device, swapchain);
            return false;
        }
        swapchain.images.resize(imageCount);
        result = vkGetSwapchainImagesKHR(device, swapchain.handle, &imageCount, swapchain.images.data());
    } while (result == VK_INCOMPLETE);
    if (!checkVulkan(result, "Read swapchain images")) {
        destroySwapchain(device, swapchain);
        return false;
    }
    if (imageCount == 0) {
        std::cerr << "No swapchain images available.\n";
        destroySwapchain(device, swapchain);
        return false;
    }
    swapchain.images.resize(imageCount);
    swapchain.views.reserve(imageCount);
    for (const VkImage image : swapchain.images) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = swapchain.format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        VkImageView view = VK_NULL_HANDLE;
        result = vkCreateImageView(device, &viewInfo, nullptr, &view);
        if (!checkVulkan(result, "Create swapchain image view")) {
            destroySwapchain(device, swapchain);
            return false;
        }
        swapchain.views.push_back(view);
    }
    std::cout << "Swapchain created: " << extent.width << 'x' << extent.height
              << " | Images: " << swapchain.images.size()
              << " | Format enum: " << swapchain.format
              << " | Color-space enum: " << surfaceFormat->colorSpace
              << " | Present mode: FIFO\n"
              << "Swapchain image views created: " << swapchain.views.size() << '\n';
    return true;
}
