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
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

/*
 * Scene configuration: define the floor and sphere grid in metres.
 * These shared dimensions keep mesh generation and object placement on the same scale.
 * The data starts on the CPU; main creates and uploads the GPU resources later.
 */
// World-space floor corners: X/Z span 20 metres; Y is height.
const std::array<glm::vec3, 4> floorVertices{{
    {-10.0f, 0.0f, -10.0f},
    { 10.0f, 0.0f, -10.0f},
    { 10.0f, 0.0f,  10.0f},
    {-10.0f, 0.0f,  10.0f},
}};
// Each group of three indices selects one triangle from those corners.
constexpr std::array<std::uint16_t, 6> floorIndices{0, 2, 1, 0, 3, 2};

constexpr std::size_t sphereGridSide = 20;
constexpr std::size_t sphereCount = sphereGridSide * sphereGridSide;
constexpr uint32_t sphereGroupSide = 4;
constexpr float sphereRadius = 0.25f;
constexpr float sphereSpacing = 1.0f;

/*
 * Object placement: generate an evenly spaced grid around the scene centre.
 * Raise each centre by the sphere radius so its bottom touches the floor.
 * These positions reuse one shared mesh rather than storing a mesh copy per sphere.
 */
std::array<glm::vec3, sphereCount> createSpherePositions() {
    std::array<glm::vec3, sphereCount> positions{};
    const float halfSpan = static_cast<float>(sphereGridSide - 1) * sphereSpacing * 0.5f;
    for (std::size_t row = 0; row < sphereGridSide; ++row) {
        for (std::size_t column = 0; column < sphereGridSide; ++column) {
            // Center the grid on X/Z; placing centers at radius makes spheres touch the floor.
            positions[row * sphereGridSide + column] = {
                static_cast<float>(column) * sphereSpacing - halfSpan,
                sphereRadius,
                static_cast<float>(row) * sphereSpacing - halfSpan,
            };
        }
    }
    return positions;
}

/*
 * Shared sphere mesh: describe the surface with vertices and triangle indices.
 * Generate horizontal rings, join adjacent rings, and close the top and bottom poles.
 * Build this mesh once on the CPU; rendering places it at each instance position.
 */
struct SphereGeometry {
    // CPU mesh around local (0, 0, 0); instance positions place it in the world later.
    std::vector<glm::vec3> vertices;
    std::vector<std::uint16_t> indices;
};

SphereGeometry createSphereGeometry() {
    constexpr std::uint16_t slices = 16; // Segments around each horizontal ring.
    constexpr std::uint16_t stacks = 8;  // Segments from the top pole to the bottom.
    const float pi = glm::radians(180.0f);
    SphereGeometry geometry;
    geometry.vertices.push_back({0.0f, sphereRadius, 0.0f});
    for (std::uint16_t stack = 1; stack < stacks; ++stack) {
        const float latitude = pi * static_cast<float>(stack) / stacks;
        const float ringRadius = sphereRadius * glm::sin(latitude);
        const float height = sphereRadius * glm::cos(latitude);
        for (std::uint16_t slice = 0; slice < slices; ++slice) {
            const float longitude = 2.0f * pi * static_cast<float>(slice) / slices;
            geometry.vertices.push_back({
                ringRadius * glm::cos(longitude), height, ringRadius * glm::sin(longitude),
            });
        }
    }
    const auto bottom = static_cast<std::uint16_t>(geometry.vertices.size());
    geometry.vertices.push_back({0.0f, -sphereRadius, 0.0f});

    // Join neighboring rings; each four-corner patch becomes two triangles.
    for (std::uint16_t ring = 0; ring < stacks - 2; ++ring) {
        for (std::uint16_t slice = 0; slice < slices; ++slice) {
            const auto current = static_cast<std::uint16_t>(1 + ring * slices + slice);
            const auto next = static_cast<std::uint16_t>(1 + ring * slices + (slice + 1) % slices);
            const auto below = static_cast<std::uint16_t>(current + slices);
            const auto belowNext = static_cast<std::uint16_t>(next + slices);
            geometry.indices.insert(geometry.indices.end(), {
                current, next, below, next, belowNext, below,
            });
        }
    }
    // Close both poles with triangle fans. Modulo joins the last slice to the first.
    for (std::uint16_t slice = 0; slice < slices; ++slice) {
        const auto first = static_cast<std::uint16_t>(1 + slice);
        const auto firstNext = static_cast<std::uint16_t>(1 + (slice + 1) % slices);
        const auto last = static_cast<std::uint16_t>(1 + (stacks - 2) * slices + slice);
        const auto lastNext = static_cast<std::uint16_t>(1 + (stacks - 2) * slices + (slice + 1) % slices);
        geometry.indices.insert(geometry.indices.end(), {
            0, firstNext, first, bottom, last, lastNext,
        });
    }
    return geometry;
}

/*
 * Persistent camera and pipeline state.
 * The camera describes the view; pipeline state records GPU handles and attachment formats.
 * These structures store state, while the functions below create, use and release resources.
 */
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
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
};

/*
 * Camera transform: map world-space positions into the camera's clip space.
 * Combine the view and perspective matrices using the current drawable aspect ratio.
 * The result includes Vulkan's image-axis adjustment and is sent to vertex shaders each frame.
 */
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

/*
 * Geometry resources: separate buffer creation, memory allocation and data upload.
 * A buffer describes its size and usage; an allocation provides the storage bound to it.
 * Upload static scene data through CPU-writable memory before submitting any draw work.
 */
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

/*
 * Shared upload entry point: compose the three resource steps above.
 * Read CPU data and fill the caller's buffer and memory handles through output references.
 * The caller retains ownership, including any partial resources left after a failure.
 */
// Input: CPU data, byte count and Vulkan usage. Outputs: buffer/memory handles, by reference.
// On success the data is uploaded. On failure partial outputs may still exist;
// the caller must destroy the buffer before freeing its memory at shutdown.
bool createUploadedGeometryBuffer(VkPhysicalDevice physicalDevice, VkDevice device,
                                  const void* data, VkDeviceSize size, VkBufferUsageFlags usage,
                                  VkBuffer& outBuffer, VkDeviceMemory& outMemory) {
    // && stops at the first failure; later steps require earlier resources to exist.
    return createGeometryBuffer(device, size, usage, outBuffer) &&
           allocateGeometryMemory(physicalDevice, device, outBuffer, outMemory) &&
           uploadGeometryData(device, outMemory, data, size);
}

/*
 * Shader loading: read SPIR-V files compiled by CMake and create shader modules.
 * Check the file size and header before passing the bytecode to Vulkan.
 * Modules are inputs to pipeline creation; loading them does not issue any drawing commands.
 */
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

/*
 * Graphics pipeline lifecycle: combine shaders with the GPU's drawing rules.
 * Floor and sphere pipelines share this setup; spheres add per-instance position input.
 * The caller owns the pipeline and layout and waits for GPU work before destroying them.
 */
void destroyGraphicsPipeline(VkDevice device, GraphicsPipelineState& pipeline) {
    vkDestroyPipeline(device, pipeline.handle, nullptr);
    vkDestroyPipelineLayout(device, pipeline.layout, nullptr);
    pipeline = {};
}

bool createGraphicsPipeline(VkDevice device, VkFormat colorFormat, VkFormat depthFormat,
                         VkShaderModule vertexShader, VkShaderModule fragmentShader,
                         bool instanced, GraphicsPipelineState& pipeline) {
    /*
     * Shader stages and the camera data interface.
     * Declare the camera push-constant range and choose the vertex and fragment modules.
     * Matching ranges let both pipelines use the same pushed camera matrix during drawing.
     */
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

    /*
     * Vertex input: describe how the GPU advances through each buffer.
     * Mesh positions advance per vertex; sphere translations advance per instance.
     * Attributes connect those streams to the input locations declared by the vertex shader.
     */
    // Binding 0 advances per vertex; optional binding 1 advances per sphere instance.
    VkVertexInputBindingDescription bindings[2]{};
    bindings[0].binding = 0;
    bindings[0].stride = sizeof(glm::vec3);
    bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    bindings[1].binding = 1;
    bindings[1].stride = sizeof(glm::vec3);
    bindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
    VkVertexInputAttributeDescription attributes[2]{};
    attributes[0].location = 0;
    attributes[0].binding = 0;
    attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[1].location = 1;
    attributes[1].binding = 1;
    attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = instanced ? 2 : 1;
    vertexInput.pVertexBindingDescriptions = bindings;
    vertexInput.vertexAttributeDescriptionCount = instanced ? 2 : 1;
    vertexInput.pVertexAttributeDescriptions = attributes;

    /*
     * Drawing rules: assemble triangles, fill surfaces and test their depth.
     * Depth tests and writes keep nearer opaque surfaces in front without sorting objects.
     * Viewport and scissor are set during recording, so resizing can reuse the pipeline.
     */
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
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

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

    /*
     * Create the pipeline from the assembled configuration.
     * Match its colour and depth formats to the attachments used by dynamic rendering.
     * A swapchain recreation that changes these formats also needs matching new pipelines.
     */
    // Dynamic rendering supplies attachment formats instead of a VkRenderPass.
    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &colorFormat;
    rendering.depthAttachmentFormat = depthFormat;
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
    pipeline.depthFormat = depthFormat;
    std::cout << "Graphics pipeline created for format " << colorFormat << ".\n";
    return true;
}

/*
 * Pause rendering when the window is minimized or has no drawable area.
 * Continue processing window events so the application can still close while paused.
 * Report the paused duration so camera rotation resumes without counting that time.
 */
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

/*
 * Resize-dependent resources: rebuild images and frame state for the current window.
 * Wait for GPU work before replacing the swapchain, depth attachment and synchronization.
 * Keep geometry uploads; rebuild pipelines only if their attachment formats change.
 */
bool recreateSwapchain(const DeviceSelection& selection, VkDevice device,
                       VkSurfaceKHR surface, GLFWwindow* window,
                       SwapchainState& swapchain, FrameState& frame,
                       DepthState& depth,
                       VkShaderModule vertexShader, VkShaderModule fragmentShader,
                       GraphicsPipelineState& pipeline,
                       VkShaderModule sphereVertexShader, VkShaderModule sphereFragmentShader,
                       GraphicsPipelineState& spherePipeline) {
    // Resize is infrequent: finish old work, then rebuild dependent objects.
    // Rebuilding FrameState also updates its per-image semaphore count and
    // starts with fresh synchronization objects after an out-of-date result.
    if (!checkVulkan(vkDeviceWaitIdle(device), "Wait for device before swapchain recreation")) {
        return false;
    }
    destroyFrameState(device, frame);
    destroyDepthState(device, depth);
    destroySwapchain(device, swapchain);
    if (!createSwapchain(selection.device, device, surface, window, swapchain) ||
        !createDepthState(selection.device, device, swapchain.extent, depth)) {
        return false;
    }
    if (pipeline.colorFormat != swapchain.format || pipeline.depthFormat != depth.format) {
        destroyGraphicsPipeline(device, pipeline);
        destroyGraphicsPipeline(device, spherePipeline);
        if (!createGraphicsPipeline(device, swapchain.format, depth.format,
                                    vertexShader, fragmentShader, false, pipeline) ||
            !createGraphicsPipeline(device, swapchain.format, depth.format,
                                    sphereVertexShader, sphereFragmentShader, true, spherePipeline)) {
            return false;
        }
    }
    return createFrameState(device, selection.queueFamilyIndex, swapchain.images.size(), frame);
}

} // namespace

int main(int argc, char** argv) {
    /*
     * Startup and CPU scene preparation.
     * Read the run mode, generate positions and the shared mesh, and initialize the camera.
     * Smoke tests use the normal rendering path but close automatically after a short run.
     */
    bool smokeTest = false;
    bool grouped = false;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--smoke-test") {
            smokeTest = true;
        } else if (argument == "--grouped") {
            grouped = true;
        } else {
            std::cerr << "Usage: gpu_culling [--smoke-test] [--grouped]\n";
            return EXIT_FAILURE;
        }
    }
    const auto spherePositions = createSpherePositions();
    const auto sphereGeometry = createSphereGeometry();
    CullingGroups sphereGroups{};
    if (!createCullingGroups(spherePositions, static_cast<uint32_t>(sphereGridSide),
                             sphereGroupSide, sphereRadius, sphereGroups)) {
        return EXIT_FAILURE;
    }
    CameraState camera{};
    constexpr double cameraRotationPeriod = 10.0;

    /*
     * Window and event setup without an OpenGL context.
     * Callbacks only report size or visibility changes; the main loop rebuilds GPU resources.
     * This keeps resource changes ordered with the rest of the rendering work.
     */
    glfwSetErrorCallback([](int code, const char* description) {
        std::cerr << "GLFW error " << code << ": " << description << '\n';
    });

    if (!glfwInit()) {
        return EXIT_FAILURE;
    }

    // Vulkan handles rendering; no OpenGL context is needed.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(960, 540, "GPU Culling - Sphere Grid Preview", nullptr, nullptr);
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
              << "Sphere positions prepared on CPU: " << spherePositions.size()
              << " spheres, radius " << sphereRadius << " m, center spacing "
              << sphereSpacing << " m.\n"
              << "Sphere geometry prepared on CPU: " << sphereGeometry.vertices.size()
              << " vertices, " << sphereGeometry.indices.size() << " indices ("
              << sphereGeometry.indices.size() / 3 << " triangles).\n"
              << "Culling mode: " << (grouped ? "grouped" : "per-object") << "; "
              << sphereGroups.groups.size() << " groups of up to " << sphereGroupSide << 'x'
              << sphereGroupSide << " spheres, with full-sphere AABBs.\n"
              << "Camera position: (" << camera.position.x << ", " << camera.position.y
              << ", " << camera.position.z << "), yaw " << camera.yawDegrees
              << ", pitch " << camera.pitchDegrees << " degrees.\n";

    /*
     * Connect Vulkan to the window and select a usable GPU.
     * Create the instance and surface before checking graphics and presentation support.
     * Then create the logical device and queue, releasing earlier resources if setup fails.
     */
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
    if (!selectPhysicalDevice(instance, surface, static_cast<uint32_t>(sphereCount), selection)) {
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

    /*
     * Prepare image targets and the scene's resource records.
     * The swapchain supplies colour images; draw metadata identifies mesh and instance buffers.
     * Start handles empty so the same cleanup path also covers partially completed setup.
     */
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
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory = VK_NULL_HANDLE;
    // Set draw counts now; buffer handles start null and are filled by the upload calls below.
    MeshDraw floorDraw{};
    floorDraw.indexCount = static_cast<uint32_t>(floorIndices.size());
    MeshDraw sphereDraw{};
    sphereDraw.indexCount = static_cast<uint32_t>(sphereGeometry.indices.size());
    sphereDraw.instanceCount = static_cast<uint32_t>(spherePositions.size());
    const VkDeviceSize sphereVertexBytes = sphereGeometry.vertices.size() * sizeof(glm::vec3);
    const VkDeviceSize sphereIndexBytes = sphereGeometry.indices.size() * sizeof(std::uint16_t);
    const VkDeviceSize spherePositionBytes = spherePositions.size() * sizeof(glm::vec3);
    VkDeviceMemory sphereVertexMemory = VK_NULL_HANDLE;
    VkDeviceMemory sphereIndexMemory = VK_NULL_HANDLE;
    VkDeviceMemory spherePositionMemory = VK_NULL_HANDLE;
    std::array<VkDrawIndexedIndirectCommand, sphereCount> indirectCommands{};
    VkBuffer indirectBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indirectMemory = VK_NULL_HANDLE;
    VkBuffer groupBuffer = VK_NULL_HANDLE;
    VkBuffer memberBuffer = VK_NULL_HANDLE;
    VkDeviceMemory groupMemory = VK_NULL_HANDLE;
    VkDeviceMemory memberMemory = VK_NULL_HANDLE;
    const std::filesystem::path shaderDirectory{GPU_CULLING_SHADER_DIR};
    VkShaderModule vertexShader = VK_NULL_HANDLE;
    VkShaderModule fragmentShader = VK_NULL_HANDLE;
    VkShaderModule sphereVertexShader = VK_NULL_HANDLE;
    VkShaderModule sphereFragmentShader = VK_NULL_HANDLE;
    VkShaderModule cullingShader = VK_NULL_HANDLE;
    GraphicsPipelineState graphicsPipeline{};
    GraphicsPipelineState spherePipeline{};
    DepthState depth{};
    FrameState frame{};
    CullingState culling{};
    /*
     * Create scene resources once before rendering starts.
     * Upload floor, sphere and instance data, then load shaders and create pipelines and frame state.
     * Upload helpers fill MeshDraw handles by reference; main keeps the memory handles for cleanup.
     */
    // Output references fill each MeshDraw member directly; no separate assignment is needed.
    // Keep memory handles here too: MeshDraw describes drawing, while main owns cleanup.
    // Initialization runs once. The frame loop below only records/submits draw commands.
    bool success = createDepthState(selection.device, device, swapchain.extent, depth) &&
                   createUploadedGeometryBuffer(selection.device, device, floorVertices.data(), vertexBytes,
                                                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, floorDraw.vertexBuffer, vertexMemory) &&
                   createUploadedGeometryBuffer(selection.device, device, floorIndices.data(), indexBytes,
                                                VK_BUFFER_USAGE_INDEX_BUFFER_BIT, floorDraw.indexBuffer, indexMemory) &&
                   createUploadedGeometryBuffer(selection.device, device, sphereGeometry.vertices.data(), sphereVertexBytes,
                                                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, sphereDraw.vertexBuffer, sphereVertexMemory) &&
                   createUploadedGeometryBuffer(selection.device, device, sphereGeometry.indices.data(), sphereIndexBytes,
                                                VK_BUFFER_USAGE_INDEX_BUFFER_BIT, sphereDraw.indexBuffer, sphereIndexMemory) &&
                   createUploadedGeometryBuffer(selection.device, device, spherePositions.data(), spherePositionBytes,
                                                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                sphereDraw.instanceBuffer, spherePositionMemory) &&
                   createUploadedGeometryBuffer(selection.device, device, indirectCommands.data(), sizeof(indirectCommands),
                                                VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                indirectBuffer, indirectMemory) &&
                   createUploadedGeometryBuffer(selection.device, device, sphereGroups.groups.data(),
                                                sphereGroups.groups.size() * sizeof(CullingGroup),
                                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, groupBuffer, groupMemory) &&
                   createUploadedGeometryBuffer(selection.device, device, sphereGroups.members.data(),
                                                sphereGroups.members.size() * sizeof(uint32_t),
                                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, memberBuffer, memberMemory) &&
                   loadShaderModule(device, shaderDirectory / "floor.vert.spv", vertexShader) &&
                   loadShaderModule(device, shaderDirectory / "floor.frag.spv", fragmentShader) &&
                   loadShaderModule(device, shaderDirectory / "sphere.vert.spv", sphereVertexShader) &&
                   loadShaderModule(device, shaderDirectory / "sphere.frag.spv", sphereFragmentShader) &&
                   loadShaderModule(device, shaderDirectory / (grouped ? "cull_grouped.comp.spv" : "cull.comp.spv"),
                                    cullingShader) &&
                   createCullingState(device, cullingShader, sphereDraw.instanceBuffer, indirectBuffer,
                                      groupBuffer, memberBuffer, sphereRadius, sphereDraw.instanceCount,
                                      sphereDraw.indexCount, static_cast<uint32_t>(sphereGroups.groups.size()),
                                      grouped, culling) &&
                   createGraphicsPipeline(device, swapchain.format, depth.format,
                                          vertexShader, fragmentShader, false, graphicsPipeline) &&
                   createGraphicsPipeline(device, swapchain.format, depth.format,
                                          sphereVertexShader, sphereFragmentShader, true, spherePipeline) &&
                   createFrameState(device, selection.queueFamilyIndex, swapchain.images.size(), frame);
    if (success) {
        std::cout << "Geometry buffers ready: vertex " << vertexBytes
                  << " bytes, index " << indexBytes << " bytes; ready to draw.\n"
                  << "Sphere buffers uploaded: one shared mesh, " << sphereDraw.instanceCount
                  << " instances; GPU frustum culling with fixed-slot indirect drawing.\n";
    }
    /*
     * Preview loop: process events, update the camera and draw the existing scene each frame.
     * Derive rotation from elapsed active time and rebuild window-dependent resources when needed.
     * GPU compute rewrites visibility and draw parameters before indirect drawing each frame.
     */
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
                                         depth, vertexShader, fragmentShader, graphicsPipeline,
                                         sphereVertexShader, sphereFragmentShader, spherePipeline);
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
            spherePipeline.handle, floorDraw, sphereDraw, depth, culling, viewProjection);
        if (frameResult == FrameResult::OutOfDate) {
            std::cout << "Swapchain is out of date; requesting recreation.\n";
            framebufferResized = true;
        } else if (frameResult == FrameResult::Failed) {
            success = false;
        } else if (frameResult == FrameResult::Presented) {
            ++presentedFrames;
            if (presentedFrames == 1) {
                std::cout << "First sphere-grid frame submitted for presentation.\n";
            }
        }
    }
    if (smokeTest && presentedFrames == 0) {
        std::cerr << "Smoke test did not present any frames.\n";
        success = false;
    }

    /*
     * Shutdown: release resources in dependency order.
     * Wait for GPU work before destroying frame state, pipelines, buffers and their allocations.
     * Release attachments and the device before the surface, instance and window, including on setup failure.
     */
    // Wait at shutdown (and during recreation), not after every submitted frame.
    if (!checkVulkan(vkDeviceWaitIdle(device), "Wait for device at shutdown")) {
        success = false;
    }
    if (smokeTest && success) {
        success = verifyCulling(device, indirectMemory, culling, spherePositions);
    }
    std::cout << "Frames submitted for presentation: " << presentedFrames << '\n';
    std::cout << "Swapchain recreations: " << swapchainRecreations << '\n';
    destroyFrameState(device, frame);
    destroyCullingState(device, culling);
    std::cout << "Frame resources destroyed.\n";
    destroyGraphicsPipeline(device, graphicsPipeline);
    destroyGraphicsPipeline(device, spherePipeline);
    std::cout << "Graphics pipeline and layout destroyed.\n";
    vkDestroyShaderModule(device, fragmentShader, nullptr);
    vkDestroyShaderModule(device, vertexShader, nullptr);
    vkDestroyShaderModule(device, sphereFragmentShader, nullptr);
    vkDestroyShaderModule(device, sphereVertexShader, nullptr);
    vkDestroyShaderModule(device, cullingShader, nullptr);
    std::cout << "Shader modules destroyed.\n";
    // All buffer handles belong to main, including partial setup after an initialization failure.
    vkDestroyBuffer(device, floorDraw.indexBuffer, nullptr);
    vkDestroyBuffer(device, floorDraw.vertexBuffer, nullptr);
    vkDestroyBuffer(device, sphereDraw.instanceBuffer, nullptr);
    vkDestroyBuffer(device, sphereDraw.indexBuffer, nullptr);
    vkDestroyBuffer(device, sphereDraw.vertexBuffer, nullptr);
    vkDestroyBuffer(device, indirectBuffer, nullptr);
    vkDestroyBuffer(device, groupBuffer, nullptr);
    vkDestroyBuffer(device, memberBuffer, nullptr);
    vkFreeMemory(device, indexMemory, nullptr);
    vkFreeMemory(device, vertexMemory, nullptr);
    vkFreeMemory(device, spherePositionMemory, nullptr);
    vkFreeMemory(device, sphereIndexMemory, nullptr);
    vkFreeMemory(device, sphereVertexMemory, nullptr);
    vkFreeMemory(device, indirectMemory, nullptr);
    vkFreeMemory(device, groupMemory, nullptr);
    vkFreeMemory(device, memberMemory, nullptr);
    std::cout << "Geometry buffers destroyed; memory freed.\n";
    destroyDepthState(device, depth);
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
