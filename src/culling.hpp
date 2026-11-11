#pragma once

#include <vulkan/vulkan.h>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <array>
#include <cstddef>
#include <span>
#include <vector>

/*
 * Culling inputs: six inward-facing world-space planes and the shared sphere dimensions.
 * Plane distances use normalized normals so radius is measured in world-space metres.
 * This layout matches the compute shader's push constants, including its scalar offsets.
 */
struct CullingParameters {
    std::array<glm::vec4, 6> planes{};
    float radius = 0.0f;
    uint32_t objectCount = 0;
    uint32_t indexCount = 0;
    uint32_t groupCount = 0;
};
static_assert(offsetof(CullingParameters, radius) == 96 && sizeof(CullingParameters) == 112);
static_assert(sizeof(glm::vec3) == 12 && sizeof(VkDrawIndexedIndirectCommand) == 20);

// The trailing struct padding matches the std430 array stride used by the grouped shader.
struct alignas(16) CullingGroup {
    glm::vec4 minimum{};
    glm::vec4 maximum{};
    uint32_t firstMember = 0;
    uint32_t memberCount = 0;
};
static_assert(offsetof(CullingGroup, firstMember) == 32 && sizeof(CullingGroup) == 48);

struct CullingGroups {
    std::vector<CullingGroup> groups;
    std::vector<uint32_t> members;
};

bool createCullingGroups(std::span<const glm::vec3> positions, uint32_t gridSide,
                          uint32_t groupSide, float radius, CullingGroups& result);
std::array<glm::vec4, 6> extractFrustumPlanes(const glm::mat4& viewProjection);
// Test-only CPU reference for smoke/unit checks; normal rendering uses cull.comp instead.
bool sphereInFrustum(const std::array<glm::vec4, 6>& planes, glm::vec3 centre, float radius);
// Test-only coarse reference; production group rejection runs on the GPU.
bool aabbInFrustum(const std::array<glm::vec4, 6>& planes, const CullingGroup& group);

/*
 * Compute resources: own the pipeline and descriptors, and borrow the scene's buffers.
 * Every object keeps one indirect slot; compute rewrites all slots each frame to avoid stale visibility.
 * The last parameters are retained only for the smoke test's GPU-versus-CPU result check.
 */
struct CullingState {
    VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkBuffer commands = VK_NULL_HANDLE;
    CullingParameters parameters{};
    bool grouped = false;
};

void destroyCullingState(VkDevice device, CullingState& culling);
bool createCullingState(VkDevice device, VkShaderModule shader, VkBuffer positions, VkBuffer commands,
                        VkBuffer groups, VkBuffer members, float radius, uint32_t objectCount,
                        uint32_t indexCount, uint32_t groupCount, bool grouped, CullingState& culling);
void recordCulling(VkCommandBuffer commandBuffer, CullingState& culling, const glm::mat4& viewProjection);
// Smoke-test only, after device idle; normal rendering does not read commands back to the CPU.
bool verifyCulling(VkDevice device, VkDeviceMemory commandMemory, const CullingState& culling,
                   std::span<const glm::vec3> positions);
