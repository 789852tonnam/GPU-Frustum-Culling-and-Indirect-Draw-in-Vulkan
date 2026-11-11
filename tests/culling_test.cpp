#ifdef NDEBUG
#undef NDEBUG
#endif
#include "culling.hpp"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <cassert>
#include <iostream>

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
    std::cout << "Frustum tests passed: all six planes, tangent spheres, perspective and camera rotation.\n";
}
