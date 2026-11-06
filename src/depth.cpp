#include "depth.hpp"
#include "vulkan_utils.hpp"

#include <iostream>

/*
 * Depth cleanup: release the view, then the image, then its backing allocation.
 * The caller waits for GPU work before invoking this during resize or shutdown.
 * Empty handles are allowed, including resources left by a failed initialization.
 */
void destroyDepthState(VkDevice device, DepthState& depth) {
    vkDestroyImageView(device, depth.view, nullptr);
    vkDestroyImage(device, depth.image, nullptr);
    vkFreeMemory(device, depth.memory, nullptr);
    depth = {};
}

bool createDepthState(VkPhysicalDevice physicalDevice, VkDevice device,
                      VkExtent2D extent, DepthState& depth) {
    /*
     * Depth format and image: create a depth-only attachment matching the colour image size.
     * Choose a supported optimal-tiling format instead of assuming every GPU accepts the same one.
     * This image stores nearest-surface depths for opaque visibility, not colour or presentation.
     */
    // Prefer floating-point depth; both candidates contain depth without stencil.
    for (const VkFormat format : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM}) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &properties);
        if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            depth.format = format;
            break;
        }
    }
    if (depth.format == VK_FORMAT_UNDEFINED) {
        std::cerr << "No supported depth attachment format.\n";
        return false;
    }
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = depth.format;
    imageInfo.extent = {extent.width, extent.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (!checkVulkan(vkCreateImage(device, &imageInfo, nullptr, &depth.image), "Create depth image")) {
        return false;
    }
    /*
     * Backing storage: allocate compatible GPU-local memory and bind it to the image.
     * The image's requirements determine which memory types and allocation size are valid.
     * Keep any partial image/allocation in DepthState so the caller can clean up after failure.
     */
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device, depth.image, &requirements);
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
    for (uint32_t index = 0; index < memoryProperties.memoryTypeCount; ++index) {
        if (!(requirements.memoryTypeBits & (1u << index)) ||
            !(memoryProperties.memoryTypes[index].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            continue;
        }
        VkMemoryAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = index;
        if (!checkVulkan(vkAllocateMemory(device, &allocation, nullptr, &depth.memory),
                         "Allocate depth memory")) {
            return false;
        }
        break;
    }
    if (depth.memory == VK_NULL_HANDLE) {
        std::cerr << "No compatible device-local memory for depth image.\n";
        return false;
    }
    if (!checkVulkan(vkBindImageMemory(device, depth.image, depth.memory, 0), "Bind depth memory")) {
        return false;
    }
    /*
     * Attachment view: expose the image's depth aspect to dynamic rendering.
     * Frame recording handles layout transitions and clears before using this view.
     * Resize replaces the entire depth state so its dimensions continue to match the swapchain.
     */
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = depth.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = depth.format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    if (!checkVulkan(vkCreateImageView(device, &viewInfo, nullptr, &depth.view), "Create depth view")) {
        return false;
    }
    std::cout << "Depth attachment created: " << extent.width << 'x' << extent.height << ".\n";
    return true;
}
