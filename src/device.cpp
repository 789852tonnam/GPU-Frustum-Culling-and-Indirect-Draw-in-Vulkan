#include "device.hpp"
#include "vulkan_utils.hpp"

#include <iostream>
#include <optional>
#include <string_view>
#include <vector>

namespace {

const char* deviceTypeName(VkPhysicalDeviceType type) {
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "Integrated GPU";
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "Discrete GPU";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "Virtual GPU";
    case VK_PHYSICAL_DEVICE_TYPE_CPU: return "CPU (software implementation)";
    default: return "Other";
    }
}

bool printQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface,
                        std::optional<uint32_t>& unifiedFamily) {
    unifiedFamily.reset();
    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, families.data());
    families.resize(familyCount);

    std::cout << "  Queue families: " << familyCount << '\n';
    for (uint32_t index = 0; index < familyCount; ++index) {
        const VkQueueFamilyProperties& family = families[index];
        const bool graphics = (family.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
        const bool compute = (family.queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
        // Graphics and compute queues also support transfer operations.
        const bool transfer = (family.queueFlags &
            (VK_QUEUE_TRANSFER_BIT | VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) != 0;
        const bool sparse = (family.queueFlags & VK_QUEUE_SPARSE_BINDING_BIT) != 0;
        const bool videoDecode = (family.queueFlags & VK_QUEUE_VIDEO_DECODE_BIT_KHR) != 0;
        const bool videoEncode = (family.queueFlags & VK_QUEUE_VIDEO_ENCODE_BIT_KHR) != 0;

        // Presentation support is specific to this family and this surface.
        VkBool32 present = VK_FALSE;
        const VkResult result = vkGetPhysicalDeviceSurfaceSupportKHR(device, index, surface, &present);
        if (!checkVulkan(result, "Query presentation support")) {
            std::cerr << "Queue family index: " << index << '\n';
            return false;
        }

        if (!unifiedFamily && family.queueCount > 0 && graphics && compute && present == VK_TRUE) {
            unifiedFamily = index;
        }

        std::cout << "    Family " << index
                  << " | Queues: " << family.queueCount
                  << " | Graphics: " << (graphics ? "Yes" : "No")
                  << " | Compute: " << (compute ? "Yes" : "No")
                  << " | Present: " << (present == VK_TRUE ? "Yes" : "No") << '\n'
                  << "      Transfer: " << (transfer ? "Yes" : "No")
                  << " | Sparse binding: " << (sparse ? "Yes" : "No")
                  << " | Video decode: " << (videoDecode ? "Yes" : "No")
                  << " | Video encode: " << (videoEncode ? "Yes" : "No") << '\n';
    }
    return true;
}

// Return false for query errors. Unsupported capabilities use supported = false.
bool checkSwapchainSupport(VkPhysicalDevice device, VkSurfaceKHR surface, bool& supported) {
    supported = false;
    uint32_t extensionCount = 0;
    std::vector<VkExtensionProperties> extensions;
    VkResult result;
    do {
        result = vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
        if (!checkVulkan(result, "Count device extensions")) {
            return false;
        }
        extensions.resize(extensionCount);
        result = vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, extensions.data());
    } while (result == VK_INCOMPLETE);
    if (!checkVulkan(result, "Read device extensions")) {
        return false;
    }
    extensions.resize(extensionCount);

    bool hasSwapchain = false;
    for (const VkExtensionProperties& extension : extensions) {
        if (std::string_view(extension.extensionName) == VK_KHR_SWAPCHAIN_EXTENSION_NAME) {
            hasSwapchain = true;
            break;
        }
    }
    if (!hasSwapchain) {
        std::cout << "  Skip: VK_KHR_swapchain is unavailable.\n\n";
        return true;
    }

    VkSurfaceCapabilitiesKHR capabilities{};
    result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &capabilities);
    if (!checkVulkan(result, "Read surface capabilities")) {
        return false;
    }
    uint32_t formatCount = 0;
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
    if (!checkVulkan(result, "Count surface formats")) {
        return false;
    }
    uint32_t presentModeCount = 0;
    result = vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
    if (!checkVulkan(result, "Count present modes")) {
        return false;
    }
    std::cout << "  Swapchain: Yes | Surface formats: " << formatCount
              << " | Present modes: " << presentModeCount << '\n';
    if (formatCount == 0 || presentModeCount == 0) {
        std::cout << "  Skip: surface formats or present modes are unavailable.\n\n";
        return true;
    }
    if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0) {
        std::cout << "  Skip: surface images cannot be used as color attachments.\n\n";
        return true;
    }
    supported = true;
    return true;
}

int devicePriority(VkPhysicalDeviceType type) {
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return 3;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return 2;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return 1;
    default: return 0;
    }
}

} // namespace

bool selectPhysicalDevice(VkInstance instance, VkSurfaceKHR surface, DeviceSelection& selection) {
    selection = {};
    uint32_t deviceCount = 0;
    std::vector<VkPhysicalDevice> devices;
    VkResult result;

    // Query the count, then retrieve the handles. Retry if Vulkan reports an incomplete list.
    do {
        result = vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
        if (!checkVulkan(result, "Count Vulkan devices")) {
            return false;
        }
        if (deviceCount == 0) {
            std::cerr << "No Vulkan physical devices found.\n";
            return false;
        }

        devices.resize(deviceCount);
        result = vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
    } while (result == VK_INCOMPLETE);

    if (!checkVulkan(result, "Enumerate Vulkan devices")) {
        return false;
    }
    devices.resize(deviceCount);
    if (devices.empty()) {
        std::cerr << "No Vulkan physical devices found.\n";
        return false;
    }

    std::cout << "Vulkan physical devices: " << deviceCount << '\n';
    int bestPriority = -1;
    for (const VkPhysicalDevice device : devices) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device, &properties);
        std::cout << "  Name: " << properties.deviceName
                  << "\n  Type: " << deviceTypeName(properties.deviceType)
                  << "\n  Vulkan API: " << VK_API_VERSION_MAJOR(properties.apiVersion)
                  << '.' << VK_API_VERSION_MINOR(properties.apiVersion)
                  << '.' << VK_API_VERSION_PATCH(properties.apiVersion) << '\n';
        std::optional<uint32_t> unifiedFamily;
        if (!printQueueFamilies(device, surface, unifiedFamily)) {
            return false;
        }
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) {
            std::cout << "  Skip: CPU software rendering is excluded from GPU selection.\n\n";
            continue;
        }
        if (VK_API_VERSION_VARIANT(properties.apiVersion) != 0 ||
            properties.apiVersion < VK_API_VERSION_1_3) {
            std::cout << "  Skip: Vulkan 1.3 or newer is required.\n\n";
            continue;
        }
        if (!unifiedFamily) {
            std::cout << "  Skip: no single family supports graphics + compute + present.\n\n";
            continue;
        }

        VkPhysicalDeviceVulkan13Features features13{};
        features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        VkPhysicalDeviceFeatures2 features{};
        features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features.pNext = &features13;
        vkGetPhysicalDeviceFeatures2(device, &features);
        std::cout << "  Dynamic rendering: " << (features13.dynamicRendering ? "Yes" : "No")
                  << " | Synchronization2: " << (features13.synchronization2 ? "Yes" : "No") << '\n';
        if (!features13.dynamicRendering || !features13.synchronization2) {
            std::cout << "  Skip: dynamicRendering and synchronization2 are required.\n\n";
            continue;
        }

        bool swapchainSupported = false;
        if (!checkSwapchainSupport(device, surface, swapchainSupported)) {
            return false;
        }
        if (!swapchainSupported) {
            continue;
        }

        const int priority = devicePriority(properties.deviceType);
        std::cout << "  Eligible | Unified family: " << *unifiedFamily
                  << " | Device-type priority: " << priority << "\n\n";
        if (priority > bestPriority) {
            selection.device = device;
            selection.queueFamilyIndex = *unifiedFamily;
            bestPriority = priority;
        }
    }
    if (selection.device == VK_NULL_HANDLE) {
        std::cerr << "No suitable GPU found for the Vulkan 1.3 skeleton.\n";
        return false;
    }
    return true;
}

bool createLogicalDevice(const DeviceSelection& selection, VkDevice& device, VkQueue& queue) {
    device = VK_NULL_HANDLE;
    queue = VK_NULL_HANDLE;

    const float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = selection.queueFamilyIndex;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &queuePriority;

    // Enable only the features already checked during GPU selection.
    VkPhysicalDeviceVulkan13Features enabledFeatures{};
    enabledFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    enabledFeatures.dynamicRendering = VK_TRUE;
    enabledFeatures.synchronization2 = VK_TRUE;

    const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.pNext = &enabledFeatures;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = 1;
    deviceInfo.ppEnabledExtensionNames = extensions;

    const VkResult result = vkCreateDevice(selection.device, &deviceInfo, nullptr, &device);
    if (!checkVulkan(result, "Create logical device")) {
        return false;
    }

    // Queue index 0 is the first (and only) queue requested from this family.
    vkGetDeviceQueue(device, selection.queueFamilyIndex, 0, &queue);
    std::cout << "Logical device created; dynamicRendering, synchronization2, and VK_KHR_swapchain enabled.\n"
              << "Queue acquired: family " << selection.queueFamilyIndex << ", queue index 0.\n";
    return true;
}
