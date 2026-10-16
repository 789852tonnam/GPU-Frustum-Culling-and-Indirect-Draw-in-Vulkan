#pragma once

#include <vulkan/vulkan.h>

struct DeviceSelection {
    VkPhysicalDevice device = VK_NULL_HANDLE;
    uint32_t queueFamilyIndex = 0;
};

bool selectPhysicalDevice(VkInstance instance, VkSurfaceKHR surface, DeviceSelection& selection);
bool createLogicalDevice(const DeviceSelection& selection, VkDevice& device, VkQueue& queue);
