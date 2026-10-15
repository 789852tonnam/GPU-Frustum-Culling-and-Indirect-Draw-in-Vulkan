#define GLFW_INCLUDE_NONE
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include <cstdlib>
#include <iostream>
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

bool printPhysicalDevices(VkInstance instance) {
    uint32_t deviceCount = 0;
    std::vector<VkPhysicalDevice> devices;
    VkResult result;

    // Query the count, then retrieve the handles. Retry if the list grows.
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
    for (const VkPhysicalDevice device : devices) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device, &properties);
        std::cout << "  Name: " << properties.deviceName
                  << "\n  Type: " << deviceTypeName(properties.deviceType)
                  << "\n  Vulkan API: " << VK_API_VERSION_MAJOR(properties.apiVersion)
                  << '.' << VK_API_VERSION_MINOR(properties.apiVersion)
                  << '.' << VK_API_VERSION_PATCH(properties.apiVersion) << "\n\n";
    }
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
    GLFWwindow* window = glfwCreateWindow(960, 540, "GPU Culling - S2.2 Device Enumeration", nullptr, nullptr);
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
    if (!printPhysicalDevices(instance)) {
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

    vkDestroyInstance(instance, nullptr);
    std::cout << "Vulkan instance destroyed.\n";
    glfwDestroyWindow(window);
    glfwTerminate();
    std::cout << "Window closed; GLFW shut down.\n";
    return EXIT_SUCCESS;
}
