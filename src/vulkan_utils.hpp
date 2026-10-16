#pragma once

#include <vulkan/vulkan.h>

#include <iostream>

// Handle special statuses (INCOMPLETE, SUBOPTIMAL, TIMEOUT, ...) before calling.
inline bool checkVulkan(VkResult result, const char* operation) {
    if (result == VK_SUCCESS) {
        return true;
    }
    std::cerr << operation << " failed. VkResult: " << result << '\n';
    return false;
}
