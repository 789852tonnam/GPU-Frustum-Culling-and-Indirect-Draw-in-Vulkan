#include "culling.hpp"
#include <cassert>
#include <iostream>

int main() {
    for (uint32_t side : {320u, 640u, 1280u, 2560u}) {
        std::vector<glm::vec3> positions;
        positions.reserve(size_t(side) * side);
        const float half = float(side - 1) * 0.5f;
        for (uint32_t row = 0; row < side; ++row)
            for (uint32_t column = 0; column < side; ++column)
                positions.emplace_back(float(column) - half, 0.25f, float(row) - half);
        uint32_t first = 1;
        while ((4u << (first - 1)) < side) ++first;
        CullingGroups reference;
        assert(createCullingGroups(positions, side, 4u << (first - 1), 0.25f, reference));
        assert(reference.groups.size() == 1 && reference.members.size() == positions.size());
        for (uint32_t level = first + 1; level <= 15; ++level) {
            CullingGroups actual;
            assert(createCullingGroups(positions, side, 4u << (level - 1), 0.25f, actual));
            assert(actual.groups.size() == 1 && actual.members == reference.members);
            const auto& a = actual.groups[0];
            const auto& b = reference.groups[0];
            assert(a.firstMember == b.firstMember && a.memberCount == b.memberCount);
            for (int axis = 0; axis < 4; ++axis)
                assert(a.minimum[axis] == b.minimum[axis] && a.maximum[axis] == b.maximum[axis]);
        }
        std::cout << side << "x" << side << ": levels " << first
                  << " through 15 have identical shader-read group fields and member IDs.\n";
    }
}
