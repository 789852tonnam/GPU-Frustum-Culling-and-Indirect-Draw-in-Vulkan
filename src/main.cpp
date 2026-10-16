#define GLFW_INCLUDE_NONE
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string_view>
#include <vector>

namespace {

struct DeviceSelection {
    VkPhysicalDevice device = VK_NULL_HANDLE;
    uint32_t queueFamilyIndex = 0;
};

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
        if (result != VK_SUCCESS) {
            std::cerr << "Failed to query presentation support for queue family "
                      << index << ". VkResult: " << result << '\n';
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
        if (result != VK_SUCCESS) {
            std::cerr << "Failed to count device extensions. VkResult: " << result << '\n';
            return false;
        }
        extensions.resize(extensionCount);
        result = vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, extensions.data());
    } while (result == VK_INCOMPLETE);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to read device extensions. VkResult: " << result << '\n';
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
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to read surface capabilities. VkResult: " << result << '\n';
        return false;
    }
    uint32_t formatCount = 0;
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to count surface formats. VkResult: " << result << '\n';
        return false;
    }
    uint32_t presentModeCount = 0;
    result = vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to count present modes. VkResult: " << result << '\n';
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

bool selectPhysicalDevice(VkInstance instance, VkSurfaceKHR surface, DeviceSelection& selection) {
    selection = {};
    uint32_t deviceCount = 0;
    std::vector<VkPhysicalDevice> devices;
    VkResult result;

    // Query the count, then retrieve the handles. Retry if Vulkan reports an incomplete list.
    do {
        result = vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
        if (result != VK_SUCCESS) {
            std::cerr << "Failed to count Vulkan devices. VkResult: " << result << '\n';
            return false;
        }
        if (deviceCount == 0) {
            std::cerr << "No Vulkan physical devices found.\n";
            return false;
        }

        devices.resize(deviceCount);
        result = vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
    } while (result == VK_INCOMPLETE);

    if (result != VK_SUCCESS) {
        std::cerr << "Failed to enumerate Vulkan devices. VkResult: " << result << '\n';
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
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to create logical device. VkResult: " << result << '\n';
        return false;
    }

    // Queue index 0 is the first (and only) queue requested from this family.
    vkGetDeviceQueue(device, selection.queueFamilyIndex, 0, &queue);
    std::cout << "Logical device created; dynamicRendering, synchronization2, and VK_KHR_swapchain enabled.\n"
              << "Queue acquired: family " << selection.queueFamilyIndex << ", queue index 0.\n";
    return true;
}

} // namespace

int main(int argc, char** argv) {
    const bool smokeTest = argc == 2 && std::string_view(argv[1]) == "--smoke-test";
    if (argc > 1 && !smokeTest) {
        std::cerr << "Usage: gpu_culling [--smoke-test]\n";
        return EXIT_FAILURE;
    }

    glfwSetErrorCallback([](int code, const char* description) {
        std::cerr << "GLFW error " << code << ": " << description << '\n';
    });

    if (!glfwInit()) {
        return EXIT_FAILURE;
    }

    // Vulkan will handle rendering later; no OpenGL context is needed.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(960, 540, "GPU Culling - S2.6 Logical Device", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return EXIT_FAILURE;
    }

    std::cout << "GLFW window created (960x540). Press Escape or close the window to exit.\n";

    // GLFW chooses the surface extensions for the active window system.
    uint32_t extensionCount = 0;
    const char** extensions = glfwGetRequiredInstanceExtensions(&extensionCount);
    if (!extensions) {
        std::cerr << "Vulkan loader or required window-system extensions are unavailable.\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    VkApplicationInfo applicationInfo{};
    applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    applicationInfo.pApplicationName = "GPU Culling";
    applicationInfo.applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    applicationInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &applicationInfo;
    instanceInfo.enabledExtensionCount = extensionCount;
    instanceInfo.ppEnabledExtensionNames = extensions;

    VkInstance instance = VK_NULL_HANDLE;
    const VkResult result = vkCreateInstance(&instanceInfo, nullptr, &instance);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to create Vulkan 1.3 instance. VkResult: " << result << '\n';
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    std::cout << "Vulkan instance created (requested API 1.3).\n";
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    const VkResult surfaceResult = glfwCreateWindowSurface(instance, window, nullptr, &surface);
    if (surfaceResult != VK_SUCCESS) {
        std::cerr << "Failed to create Vulkan window surface. VkResult: " << surfaceResult << '\n';
        vkDestroyInstance(instance, nullptr);
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }
    std::cout << "Vulkan window surface created.\n";

    DeviceSelection selection{};
    if (!selectPhysicalDevice(instance, surface, selection)) {
        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyInstance(instance, nullptr);
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }
    VkPhysicalDeviceProperties selectedProperties{};
    vkGetPhysicalDeviceProperties(selection.device, &selectedProperties);
    std::cout << "Selected GPU: " << selectedProperties.deviceName
              << "\nSelected queue family: " << selection.queueFamilyIndex
              << " (graphics + compute + present)\n"
              << "Preference: discrete > integrated > virtual > other; ties keep the first eligible device.\n";

    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    if (!createLogicalDevice(selection, device, queue)) {
        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyInstance(instance, nullptr);
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }
    const double startTime = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        glfwWaitEventsTimeout(0.05);
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS ||
            (smokeTest && glfwGetTime() - startTime >= 0.5)) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
    }

    // No GPU work has been submitted in this milestone, so no idle wait is needed.
    vkDestroyDevice(device, nullptr);
    std::cout << "Logical device destroyed; its queue is released with it.\n";
    // The surface must be released before its instance and native window.
    vkDestroySurfaceKHR(instance, surface, nullptr);
    std::cout << "Vulkan window surface destroyed.\n";
    vkDestroyInstance(instance, nullptr);
    std::cout << "Vulkan instance destroyed.\n";
    glfwDestroyWindow(window);
    glfwTerminate();
    std::cout << "Window closed; GLFW shut down.\n";
    return EXIT_SUCCESS;
}
