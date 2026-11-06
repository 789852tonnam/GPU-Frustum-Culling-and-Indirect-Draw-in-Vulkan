#pragma once

#include <vulkan/vulkan.h>

#include <iostream>

/*
 * Vulkan error reporting: turn an expected-success API result into a readable failure message.
 * Return a boolean so callers can stop setup or rendering and follow their existing cleanup path.
 * Callers handle recoverable statuses such as timeout or out-of-date before using this helper.
 */
// Handle special statuses (INCOMPLETE, SUBOPTIMAL, TIMEOUT, ...) before calling.
inline bool checkVulkan(VkResult result, const char* operation) {
    if (result == VK_SUCCESS) {
        return true;
    }
    std::cerr << operation << " failed. VkResult: " << result << '\n';
    return false;
}
