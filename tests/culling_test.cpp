#ifdef NDEBUG
#undef NDEBUG
#endif
#include "culling.hpp"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <cassert>
#include <iostream>
#include <vector>

int main() {
    const auto box = extractFrustumPlanes(glm::mat4(1.0f));
    assert(sphereInFrustum(box, {0, 0, 0.5f}, 0.1f));
    const std::array<glm::vec3, 6> outside{
        glm::vec3{-1.2f, 0, 0.5f}, {1.2f, 0, 0.5f},
        {0, -1.2f, 0.5f}, {0, 1.2f, 0.5f}, {0, 0, -0.2f}, {0, 0, 1.2f},
    };
    for (auto centre : outside) {
        assert(!sphereInFrustum(box, centre, 0.1f));
    }
    const std::array<glm::vec3, 6> touching{
        glm::vec3{-1.1f, 0, 0.5f}, {1.1f, 0, 0.5f},
        {0, -1.1f, 0.5f}, {0, 1.1f, 0.5f}, {0, 0, -0.1f}, {0, 0, 1.1f},
    };
    for (auto centre : touching) {
        assert(sphereInFrustum(box, centre, 0.1f));
    }

    auto projection = glm::perspectiveRH_ZO(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 10.0f);
    projection[1][1] *= -1.0f;
    const auto forward = extractFrustumPlanes(projection);
    assert(sphereInFrustum(forward, {0, 0, -2}, 0.25f));
    assert(!sphereInFrustum(forward, {0, 0, 2}, 0.25f));
    assert(!sphereInFrustum(forward, {0, 0, -11}, 0.25f));
    assert(!sphereInFrustum(forward, {0, 0, -0.02f}, 0.01f));
    assert(sphereInFrustum(forward, {0, 0, -0.1f}, 0.05f));
    assert(sphereInFrustum(forward, {0, 0, -10.05f}, 0.1f));
    const auto turned = extractFrustumPlanes(projection * glm::lookAtRH(
        glm::vec3{0, 2, 0}, glm::vec3{1, 2, 0}, glm::vec3{0, 1, 0}));
    assert(sphereInFrustum(turned, {2, 2, 0}, 0.25f));
    assert(!sphereInFrustum(turned, {-2, 2, 0}, 0.25f));

    const CullingGroup inside{glm::vec4(-0.5f, -0.5f, 0.2f, 0), glm::vec4(0.5f, 0.5f, 0.8f, 0)};
    assert(aabbInFrustum(box, inside));
    for (auto centre : outside) {
        const CullingGroup group{glm::vec4(centre - glm::vec3(0.1f), 0),
                                 glm::vec4(centre + glm::vec3(0.1f), 0)};
        assert(!aabbInFrustum(box, group));
    }
    for (auto centre : touching) {
        const CullingGroup group{glm::vec4(centre - glm::vec3(0.1f), 0),
                                 glm::vec4(centre + glm::vec3(0.1f), 0)};
        assert(aabbInFrustum(box, group));
    }

    // Ragged blocks and blocks larger than the grid must still cover each original object exactly once.
    for (uint32_t gridSide : {1u, 5u, 20u}) {
        std::vector<glm::vec3> positions;
        for (uint32_t row = 0; row < gridSide; ++row) {
            for (uint32_t column = 0; column < gridSide; ++column) {
                positions.emplace_back(static_cast<float>(column) - 2, 0.25f, static_cast<float>(row) - 2);
            }
        }
        for (uint32_t groupSide : {1u, 2u, 4u, 8u, 32u}) {
            CullingGroups result;
            assert(createCullingGroups(positions, gridSide, groupSide, 0.25f, result));
            const uint32_t blocks = (gridSide + groupSide - 1) / groupSide;
            assert(result.groups.size() == blocks * blocks && result.members.size() == positions.size());
            std::vector<uint32_t> visits(positions.size());
            for (const auto& group : result.groups) {
                assert(group.memberCount > 0 && group.firstMember + group.memberCount <= result.members.size());
                for (uint32_t offset = 0; offset < group.memberCount; ++offset) {
                    const uint32_t object = result.members[group.firstMember + offset];
                    assert(object < positions.size());
                    ++visits[object];
                    for (int axis = 0; axis < 3; ++axis) {
                        assert(group.minimum[axis] <= positions[object][axis] - 0.25f);
                        assert(group.maximum[axis] >= positions[object][axis] + 0.25f);
                    }
                    // A coarse rejection must never remove an object the fine reference would retain.
                    assert(aabbInFrustum(forward, group) || !sphereInFrustum(forward, positions[object], 0.25f));
                    assert(aabbInFrustum(turned, group) || !sphereInFrustum(turned, positions[object], 0.25f));
                }
            }
            for (uint32_t visitsToObject : visits) assert(visitsToObject == 1);
        }
        CullingGroups invalid;
        assert(!createCullingGroups(positions, gridSide, 0, 0.25f, invalid));
        assert(!createCullingGroups(positions, gridSide + 1, 2, 0.25f, invalid));
        assert(!createCullingGroups(positions, gridSide, 2, -0.25f, invalid));
    }
    std::cout << "Frustum tests passed: all six planes, tangent spheres, perspective and camera rotation.\n";
    std::cout << "Grouping tests passed: full bounds, unique coverage, partial blocks and conservative rejection.\n";
}
