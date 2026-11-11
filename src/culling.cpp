#include "culling.hpp"
#include "vulkan_utils.hpp"

#include <glm/geometric.hpp>
#include <glm/matrix.hpp>

#include <iostream>

/*
 * Frustum preparation: derive world-space planes from Vulkan's clip inequalities.
 * X/Y lie between -W and W; depth lies between zero and W, so the near plane uses row 2.
 * The CPU reference keeps spheres touching a plane, with a small tolerance for floating-point error.
 */
std::array<glm::vec4, 6> extractFrustumPlanes(const glm::mat4& viewProjection) {
    const glm::mat4 rows = glm::transpose(viewProjection);
    std::array<glm::vec4, 6> planes{
        rows[3] + rows[0], rows[3] - rows[0],
        rows[3] + rows[1], rows[3] - rows[1],
        rows[2], rows[3] - rows[2],
    };
    for (auto& plane : planes) {
        plane /= glm::length(glm::vec3(plane));
    }
    return planes;
}

// Test-only CPU reference for smoke/unit checks; normal rendering uses cull.comp instead.
bool sphereInFrustum(const std::array<glm::vec4, 6>& planes, glm::vec3 centre, float radius) {
    for (const auto& plane : planes) {
        if (glm::dot(plane, glm::vec4(centre, 1.0f)) < -radius - 1e-5f) {
            return false;
        }
    }
    return true;
}

/*
 * Compute lifecycle: bind the existing position buffer and the writable indirect-command buffer.
 * Descriptors describe those buffers; main still owns their memory and releases it after this state.
 * Keep partially created handles available for the same shutdown path when initialization fails.
 */
void destroyCullingState(VkDevice device, CullingState& culling) {
    vkDestroyPipeline(device, culling.pipeline, nullptr);
    vkDestroyPipelineLayout(device, culling.layout, nullptr);
    vkDestroyDescriptorPool(device, culling.descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(device, culling.descriptorLayout, nullptr);
    culling = {};
}

bool createCullingState(VkDevice device, VkShaderModule shader, VkBuffer positions, VkBuffer commands,
                        float radius, uint32_t objectCount, uint32_t indexCount, CullingState& culling) {
    culling.commands = commands;
    culling.parameters.radius = radius;
    culling.parameters.objectCount = objectCount;
    culling.parameters.indexCount = indexCount;
    VkDescriptorSetLayoutBinding bindings[2]{};
    for (uint32_t index = 0; index < 2; ++index) {
        bindings[index].binding = index;
        bindings[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[index].descriptorCount = 1;
        bindings[index].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo descriptorInfo{};
    descriptorInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorInfo.bindingCount = 2;
    descriptorInfo.pBindings = bindings;
    if (!checkVulkan(vkCreateDescriptorSetLayout(device, &descriptorInfo, nullptr, &culling.descriptorLayout),
                     "Create culling descriptor layout")) {
        return false;
    }
    const VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (!checkVulkan(vkCreateDescriptorPool(device, &poolInfo, nullptr, &culling.descriptorPool),
                     "Create culling descriptor pool")) {
        return false;
    }
    VkDescriptorSetAllocateInfo setInfo{};
    setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setInfo.descriptorPool = culling.descriptorPool;
    setInfo.descriptorSetCount = 1;
    setInfo.pSetLayouts = &culling.descriptorLayout;
    if (!checkVulkan(vkAllocateDescriptorSets(device, &setInfo, &culling.descriptorSet),
                     "Allocate culling descriptor set")) {
        return false;
    }
    const VkDescriptorBufferInfo buffers[]{
        {positions, 0, objectCount * sizeof(glm::vec3)},
        {commands, 0, objectCount * sizeof(VkDrawIndexedIndirectCommand)},
    };
    VkWriteDescriptorSet writes[2]{};
    for (uint32_t index = 0; index < 2; ++index) {
        writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[index].dstSet = culling.descriptorSet;
        writes[index].dstBinding = index;
        writes[index].descriptorCount = 1;
        writes[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[index].pBufferInfo = &buffers[index];
    }
    vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);

    const VkPushConstantRange pushRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(CullingParameters)};
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &culling.descriptorLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    if (!checkVulkan(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &culling.layout),
                     "Create culling pipeline layout")) {
        return false;
    }
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = shader;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = culling.layout;
    return checkVulkan(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                               &culling.pipeline), "Create culling pipeline");
}

/*
 * GPU culling: update every object's command before the graphics pass consumes the buffer.
 * The frame fence has completed earlier reads; the first barrier also orders prior indirect accesses explicitly.
 * The final barrier exposes compute writes to indirect drawing and to the smoke test's post-idle host read.
 */
void recordCulling(VkCommandBuffer commandBuffer, CullingState& culling, const glm::mat4& viewProjection) {
    culling.parameters.planes = extractFrustumPlanes(viewProjection);
    VkBufferMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = culling.commands;
    barrier.size = VK_WHOLE_SIZE;
    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.bufferMemoryBarrierCount = 1;
    dependency.pBufferMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commandBuffer, &dependency);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, culling.pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, culling.layout,
                            0, 1, &culling.descriptorSet, 0, nullptr);
    vkCmdPushConstants(commandBuffer, culling.layout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(culling.parameters), &culling.parameters);
    vkCmdDispatch(commandBuffer, (culling.parameters.objectCount + 63) / 64, 1, 1);

    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_HOST_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_HOST_READ_BIT;
    vkCmdPipelineBarrier2(commandBuffer, &dependency);
}

/*
 * Smoke-test-only readback: after device idle, compare every GPU command with a CPU sphere test.
 * Also check draw metadata so a wrong stride or firstInstance cannot silently render the wrong object.
 * Normal rendering never reads visibility back to the CPU.
 */
bool verifyCulling(VkDevice device, VkDeviceMemory commandMemory, const CullingState& culling,
                   std::span<const glm::vec3> positions) {
    void* mapped = nullptr;
    const auto& parameters = culling.parameters;
    const VkDeviceSize size = positions.size() * sizeof(VkDrawIndexedIndirectCommand);
    if (!checkVulkan(vkMapMemory(device, commandMemory, 0, size, 0, &mapped), "Read culling commands")) {
        return false;
    }
    const auto* commands = static_cast<const VkDrawIndexedIndirectCommand*>(mapped);
    uint32_t visible = 0;
    bool valid = true;
    for (std::size_t index = 0; index < positions.size(); ++index) {
        const uint32_t expected = sphereInFrustum(parameters.planes, positions[index], parameters.radius) ? 1 : 0;
        const auto& command = commands[index];
        visible += command.instanceCount;
        if (command.indexCount != parameters.indexCount || command.instanceCount != expected ||
            command.firstIndex != 0 || command.vertexOffset != 0 || command.firstInstance != index) {
            std::cerr << "Culling command mismatch at object " << index << '\n';
            valid = false;
            break;
        }
    }
    vkUnmapMemory(device, commandMemory);
    if (valid) {
        std::cout << "GPU culling verified: " << visible << '/' << positions.size()
                  << " visible; all commands match CPU reference.\n";
    }
    return valid;
}
