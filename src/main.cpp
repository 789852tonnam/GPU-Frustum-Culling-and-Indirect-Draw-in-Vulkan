#define GLFW_INCLUDE_NONE
#include "device.hpp"
#include "frame.hpp"
#include "swapchain.hpp"
#include "vulkan_utils.hpp"

#include <GLFW/glfw3.h>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

// World-space floor corners: X/Z span 20 metres; Y is height.
const std::array<glm::vec3, 4> floorVertices{{
    {-10.0f, 0.0f, -10.0f},
    { 10.0f, 0.0f, -10.0f},
    { 10.0f, 0.0f,  10.0f},
    {-10.0f, 0.0f,  10.0f},
}};
// Each group of three indices selects one triangle from those corners.
constexpr std::array<std::uint16_t, 6> floorIndices{0, 2, 1, 0, 3, 2};

struct CameraState {
    glm::vec3 position{0.0f, 2.0f, 0.0f};
    float yawDegrees = 0.0f; // Zero yaw will face -Z.
    float pitchDegrees = -20.0f;
    float verticalFovDegrees = 60.0f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
};

struct GraphicsPipelineState {
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline handle = VK_NULL_HANDLE;
    VkFormat colorFormat = VK_FORMAT_UNDEFINED;
};

glm::mat4 createCameraViewProjection(const CameraState& camera, VkExtent2D extent) {
    const float yaw = glm::radians(camera.yawDegrees);
    const float pitch = glm::radians(camera.pitchDegrees);
    const glm::vec3 forward{
        glm::sin(yaw) * glm::cos(pitch),
        glm::sin(pitch),
        -glm::cos(yaw) * glm::cos(pitch),
    };
    const glm::mat4 view = glm::lookAtRH(camera.position, camera.position + forward,
                                        glm::vec3{0.0f, 1.0f, 0.0f});
    const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    glm::mat4 projection = glm::perspectiveRH_ZO(glm::radians(camera.verticalFovDegrees),
                                               aspect, camera.nearPlane, camera.farPlane);
    // Vulkan's positive-height viewport maps increasing Y down the image.
    projection[1][1] *= -1.0f;
    return projection * view;
}

// Create the buffer object first; its backing memory is a separate step.
bool createGeometryBuffer(VkDevice device, VkDeviceSize size,
                          VkBufferUsageFlags usage, VkBuffer& buffer) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer createdBuffer = VK_NULL_HANDLE;
    if (!checkVulkan(vkCreateBuffer(device, &bufferInfo, nullptr, &createdBuffer),
                     "Create geometry buffer")) {
        return false;
    }
    buffer = createdBuffer;
    return true;
}

bool allocateGeometryMemory(VkPhysicalDevice physicalDevice, VkDevice device,
                            VkBuffer buffer, VkDeviceMemory& memory) {
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device, buffer, &requirements);
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &properties);
    const VkMemoryPropertyFlags required = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    for (uint32_t index = 0; index < properties.memoryTypeCount; ++index) {
        const bool compatible = (requirements.memoryTypeBits & (1u << index)) != 0;
        const bool hostWritable = (properties.memoryTypes[index].propertyFlags & required) == required;
        if (!compatible || !hostWritable) {
            continue;
        }

        VkMemoryAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocateInfo.allocationSize = requirements.size;
        allocateInfo.memoryTypeIndex = index;
        VkDeviceMemory allocatedMemory = VK_NULL_HANDLE;
        if (!checkVulkan(vkAllocateMemory(device, &allocateInfo, nullptr, &allocatedMemory),
                         "Allocate geometry memory")) {
            return false;
        }
        // The caller owns this allocation even if binding fails, so cleanup can free it.
        memory = allocatedMemory;
        if (!checkVulkan(vkBindBufferMemory(device, buffer, memory, 0), "Bind geometry memory")) {
            return false;
        }
        std::cout << "Geometry memory bound: " << requirements.size
                  << " bytes, memory type " << index << ".\n";
        return true;
    }
    std::cerr << "No compatible host-visible coherent memory type for geometry buffer.\n";
    return false;
}

// These allocations are host-coherent; upload once before submitting any GPU work.
bool uploadGeometryData(VkDevice device, VkDeviceMemory memory,
                        const void* source, VkDeviceSize size) {
    void* mapped = nullptr;
    if (!checkVulkan(vkMapMemory(device, memory, 0, size, 0, &mapped), "Map geometry memory")) {
        return false;
    }
    std::memcpy(mapped, source, static_cast<std::size_t>(size));
    vkUnmapMemory(device, memory);
    return true;
}

bool loadShaderModule(VkDevice device, const std::filesystem::path& path,
                      VkShaderModule& shader) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        std::cerr << "Unable to open shader file: " << path << '\n';
        return false;
    }
    const std::streamoff size = file.tellg();
    // SPIR-V has a five-word header and consists of 32-bit words.
    if (size < 20 || size % 4 != 0) {
        std::cerr << "Invalid SPIR-V file size: " << path << '\n';
        return false;
    }
    std::vector<std::uint32_t> code(static_cast<std::size_t>(size) / sizeof(std::uint32_t));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(code.data()), size) || code[0] != 0x07230203u) {
        std::cerr << "Unable to read SPIR-V shader or invalid header: " << path << '\n';
        return false;
    }

    VkShaderModuleCreateInfo shaderInfo{};
    shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderInfo.codeSize = static_cast<std::size_t>(size);
    shaderInfo.pCode = code.data();
    VkShaderModule createdShader = VK_NULL_HANDLE;
    if (!checkVulkan(vkCreateShaderModule(device, &shaderInfo, nullptr, &createdShader),
                     "Create shader module")) {
        return false;
    }
    shader = createdShader;
    std::cout << "Shader module created: " << path << '\n';
    return true;
}

void destroyGraphicsPipeline(VkDevice device, GraphicsPipelineState& pipeline) {
    vkDestroyPipeline(device, pipeline.handle, nullptr);
    vkDestroyPipelineLayout(device, pipeline.layout, nullptr);
    pipeline = {};
}

bool createGraphicsPipeline(VkDevice device, VkFormat colorFormat,
                         VkShaderModule vertexShader, VkShaderModule fragmentShader,
                         GraphicsPipelineState& pipeline) {
    // The vertex shader receives one camera matrix through push constants.
    VkPushConstantRange cameraRange{};
    cameraRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    cameraRange.size = sizeof(glm::mat4);
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &cameraRange;
    VkPipelineLayout createdLayout = VK_NULL_HANDLE;
    if (!checkVulkan(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &createdLayout),
                     "Create graphics pipeline layout")) {
        return false;
    }
    pipeline.layout = createdLayout; // Caller cleanup owns it even if pipeline creation fails.

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertexShader;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragmentShader;
    stages[1].pName = "main";

    // Binding 0 supplies the vec3 position declared at shader location 0.
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(glm::vec3);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription position{};
    position.location = 0;
    position.binding = 0;
    position.format = VK_FORMAT_R32G32B32_SFLOAT;
    position.offset = 0;
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 1;
    vertexInput.pVertexAttributeDescriptions = &position;

    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterization{};
    rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization.rasterizerDiscardEnable = VK_FALSE;
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.cullMode = VK_CULL_MODE_NONE;
    rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorAttachment{};
    colorAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &colorAttachment;
    const VkDynamicState dynamicStates[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;

    // Dynamic rendering supplies attachment formats instead of a VkRenderPass.
    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &colorFormat;
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &rendering;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &assembly;
    pipelineInfo.pViewportState = &viewport;
    pipelineInfo.pRasterizationState = &rasterization;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamic;
    pipelineInfo.layout = pipeline.layout;
    pipelineInfo.basePipelineIndex = -1;
    VkPipeline createdPipeline = VK_NULL_HANDLE;
    if (!checkVulkan(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo,
                                              nullptr, &createdPipeline), "Create graphics pipeline")) {
        // Pipeline creation can return valid handles even when the overall call fails.
        vkDestroyPipeline(device, createdPipeline, nullptr);
        return false;
    }
    pipeline.handle = createdPipeline;
    pipeline.colorFormat = colorFormat;
    std::cout << "Graphics pipeline created for format " << colorFormat << ".\n";
    return true;
}

// Keep receiving window events while rendering is paused. A minimized window
// may keep a nonzero framebuffer size on X11, so check ICONIFIED as well.
bool waitForDrawableWindow(GLFWwindow* window, double closeAt = 0.0,
                           double* pausedSeconds = nullptr) {
    bool paused = false;
    double pauseStarted = 0.0;
    if (pausedSeconds) {
        *pausedSeconds = 0.0;
    }
    while (!glfwWindowShouldClose(window)) {
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS ||
            (closeAt > 0.0 && glfwGetTime() >= closeAt)) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            return false;
        }
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        if (width > 0 && height > 0 && !glfwGetWindowAttrib(window, GLFW_ICONIFIED)) {
            if (paused) {
                if (pausedSeconds) {
                    *pausedSeconds = glfwGetTime() - pauseStarted;
                }
                std::cout << "Window drawable again; resuming.\n";
            }
            return true;
        }
        if (!paused) {
            pauseStarted = glfwGetTime();
            std::cout << "Rendering paused: window minimized or framebuffer has zero size.\n";
            paused = true;
        }
        glfwWaitEventsTimeout(0.05);
    }
    return false;
}

bool recreateSwapchain(const DeviceSelection& selection, VkDevice device,
                       VkSurfaceKHR surface, GLFWwindow* window,
                       SwapchainState& swapchain, FrameState& frame,
                       VkShaderModule vertexShader, VkShaderModule fragmentShader,
                       GraphicsPipelineState& pipeline) {
    // Resize is infrequent: finish old work, then rebuild dependent objects.
    // Rebuilding FrameState also updates its per-image semaphore count and
    // starts with fresh synchronization objects after an out-of-date result.
    if (!checkVulkan(vkDeviceWaitIdle(device), "Wait for device before swapchain recreation")) {
        return false;
    }
    destroyFrameState(device, frame);
    destroySwapchain(device, swapchain);
    if (!createSwapchain(selection.device, device, surface, window, swapchain)) {
        return false;
    }
    if (pipeline.colorFormat != swapchain.format) {
        destroyGraphicsPipeline(device, pipeline);
        if (!createGraphicsPipeline(device, swapchain.format, vertexShader, fragmentShader, pipeline)) {
            return false;
        }
    }
    return createFrameState(device, selection.queueFamilyIndex, swapchain.images.size(), frame);
}

} // namespace

int main(int argc, char** argv) {
    const bool smokeTest = argc == 2 && std::string_view(argv[1]) == "--smoke-test";
    if (argc > 1 && !smokeTest) {
        std::cerr << "Usage: gpu_culling [--smoke-test]\n";
        return EXIT_FAILURE;
    }
    CameraState camera{};
    constexpr double cameraRotationPeriod = 10.0;

    glfwSetErrorCallback([](int code, const char* description) {
        std::cerr << "GLFW error " << code << ": " << description << '\n';
    });

    if (!glfwInit()) {
        return EXIT_FAILURE;
    }

    // Vulkan handles rendering; no OpenGL context is needed.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(960, 540, "GPU Culling - Checkerboard Preview", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return EXIT_FAILURE;
    }

    // Callbacks only request a rebuild; Vulkan work stays in the main loop.
    bool framebufferResized = false;
    glfwSetWindowUserPointer(window, &framebufferResized);
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow* resizedWindow, int, int) {
        auto* resized = static_cast<bool*>(glfwGetWindowUserPointer(resizedWindow));
        *resized = true;
    });
    glfwSetWindowIconifyCallback(window, [](GLFWwindow* changedWindow, int) {
        auto* resized = static_cast<bool*>(glfwGetWindowUserPointer(changedWindow));
        *resized = true;
    });

    std::cout << "GLFW window created (960x540). Press Escape or close the window to exit.\n";
    std::cout << "Scene data prepared on CPU: floor 20 x 20 m, " << floorVertices.size()
              << " vertices, " << floorIndices.size() << " indices.\n"
              << "Camera position: (" << camera.position.x << ", " << camera.position.y
              << ", " << camera.position.z << "), yaw " << camera.yawDegrees
              << ", pitch " << camera.pitchDegrees << " degrees.\n";

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
    if (!checkVulkan(result, "Create Vulkan 1.3 instance")) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    std::cout << "Vulkan instance created (requested API 1.3).\n";
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    const VkResult surfaceResult = glfwCreateWindowSurface(instance, window, nullptr, &surface);
    if (!checkVulkan(surfaceResult, "Create Vulkan window surface")) {
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

    // The window may have been minimized while device setup was running.
    glfwPollEvents();
    if (!waitForDrawableWindow(window)) {
        vkDestroyDevice(device, nullptr);
        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyInstance(instance, nullptr);
        glfwDestroyWindow(window);
        glfwTerminate();
        return smokeTest ? EXIT_FAILURE : EXIT_SUCCESS;
    }

    SwapchainState swapchain{};
    if (!createSwapchain(selection.device, device, surface, window, swapchain)) {
        vkDestroyDevice(device, nullptr);
        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyInstance(instance, nullptr);
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }
    const VkDeviceSize vertexBytes = floorVertices.size() * sizeof(floorVertices[0]);
    const VkDeviceSize indexBytes = floorIndices.size() * sizeof(floorIndices[0]);
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory = VK_NULL_HANDLE;
    const std::filesystem::path shaderDirectory{GPU_CULLING_SHADER_DIR};
    VkShaderModule vertexShader = VK_NULL_HANDLE;
    VkShaderModule fragmentShader = VK_NULL_HANDLE;
    GraphicsPipelineState graphicsPipeline{};
    FrameState frame{};
    bool success = createGeometryBuffer(device, vertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                        vertexBuffer) &&
                   createGeometryBuffer(device, indexBytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                        indexBuffer) &&
                   allocateGeometryMemory(selection.device, device, vertexBuffer, vertexMemory) &&
                   allocateGeometryMemory(selection.device, device, indexBuffer, indexMemory) &&
                   uploadGeometryData(device, vertexMemory, floorVertices.data(), vertexBytes) &&
                   uploadGeometryData(device, indexMemory, floorIndices.data(), indexBytes) &&
                   loadShaderModule(device, shaderDirectory / "floor.vert.spv", vertexShader) &&
                   loadShaderModule(device, shaderDirectory / "floor.frag.spv", fragmentShader) &&
                   createGraphicsPipeline(device, swapchain.format, vertexShader, fragmentShader,
                                       graphicsPipeline) &&
                   createFrameState(device, selection.queueFamilyIndex, swapchain.images.size(), frame);
    if (success) {
        std::cout << "Geometry buffers ready: vertex " << vertexBytes
                  << " bytes, index " << indexBytes << " bytes; ready to draw.\n";
    }
    framebufferResized = false;
    uint64_t presentedFrames = 0;
    uint32_t swapchainRecreations = 0;
    const double startTime = glfwGetTime();
    double previousFrameTime = startTime;
    double activePreviewSeconds = 0.0;

    while (success && !glfwWindowShouldClose(window)) {
        glfwPollEvents();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS ||
            (smokeTest && glfwGetTime() - startTime >= 0.5)) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
        if (glfwWindowShouldClose(window)) {
            break;
        }
        double pausedSeconds = 0.0;
        if (!waitForDrawableWindow(window, smokeTest ? startTime + 0.5 : 0.0, &pausedSeconds)) {
            break;
        }
        // Subtract only the actual pause, not the normal window-check overhead.
        const double currentTime = glfwGetTime();
        activePreviewSeconds += currentTime - previousFrameTime - pausedSeconds;
        previousFrameTime = currentTime;
        camera.yawDegrees = static_cast<float>(
            std::fmod(activePreviewSeconds, cameraRotationPeriod) * 360.0 / cameraRotationPeriod);
        if (framebufferResized) {
            success = recreateSwapchain(selection, device, surface, window, swapchain, frame,
                                         vertexShader, fragmentShader, graphicsPipeline);
            if (success) {
                framebufferResized = false;
                ++swapchainRecreations;
                std::cout << "Swapchain recreation completed.\n";
            }
            continue;
        }
        const glm::mat4 viewProjection =
            createCameraViewProjection(camera, swapchain.extent);
        const FrameResult frameResult = drawFrame(
            device, queue, swapchain, frame, graphicsPipeline.handle, graphicsPipeline.layout,
            vertexBuffer, indexBuffer, static_cast<uint32_t>(floorIndices.size()), viewProjection);
        if (frameResult == FrameResult::OutOfDate) {
            std::cout << "Swapchain is out of date; requesting recreation.\n";
            framebufferResized = true;
        } else if (frameResult == FrameResult::Failed) {
            success = false;
        } else if (frameResult == FrameResult::Presented) {
            ++presentedFrames;
            if (presentedFrames == 1) {
                std::cout << "First checkerboard frame submitted for presentation.\n";
            }
        }
    }
    if (smokeTest && presentedFrames == 0) {
        std::cerr << "Smoke test did not present any frames.\n";
        success = false;
    }

    // Wait at shutdown (and during recreation), not after every submitted frame.
    if (!checkVulkan(vkDeviceWaitIdle(device), "Wait for device at shutdown")) {
        success = false;
    }
    std::cout << "Frames submitted for presentation: " << presentedFrames << '\n';
    std::cout << "Swapchain recreations: " << swapchainRecreations << '\n';
    destroyFrameState(device, frame);
    std::cout << "Frame resources destroyed.\n";
    destroyGraphicsPipeline(device, graphicsPipeline);
    std::cout << "Graphics pipeline and layout destroyed.\n";
    vkDestroyShaderModule(device, fragmentShader, nullptr);
    vkDestroyShaderModule(device, vertexShader, nullptr);
    std::cout << "Shader modules destroyed.\n";
    vkDestroyBuffer(device, indexBuffer, nullptr);
    vkDestroyBuffer(device, vertexBuffer, nullptr);
    vkFreeMemory(device, indexMemory, nullptr);
    vkFreeMemory(device, vertexMemory, nullptr);
    std::cout << "Geometry buffers destroyed; memory freed.\n";
    destroySwapchain(device, swapchain);
    std::cout << "Swapchain image views and swapchain destroyed.\n";
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
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
