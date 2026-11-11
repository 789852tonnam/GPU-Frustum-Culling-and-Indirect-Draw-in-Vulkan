#pragma once

#include <vulkan/vulkan.h>

/*
 * Device selection result: identify suitable hardware and a shared queue family.
 * A physical-device handle describes hardware; it does not own a logical Vulkan device.
 * The creation function below opens that hardware and returns the queue used for submissions.
 */
struct DeviceSelection {
    VkPhysicalDevice device = VK_NULL_HANDLE;
    uint32_t queueFamilyIndex = 0;
};

bool selectPhysicalDevice(VkInstance instance, VkSurfaceKHR surface,
                           uint32_t indirectDrawCount, DeviceSelection& selection);
bool createLogicalDevice(const DeviceSelection& selection, VkDevice& device, VkQueue& queue);
