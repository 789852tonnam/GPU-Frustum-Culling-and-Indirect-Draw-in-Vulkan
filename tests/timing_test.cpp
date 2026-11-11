#ifdef NDEBUG
#undef NDEBUG
#endif
#include "timing.hpp"

#include <cassert>
#include <cmath>
#include <limits>

int main() {
    assert(std::abs(timestampMilliseconds(10, 20, 64, 2.0f) - 0.00002) < 1e-12);
    assert(timestampMilliseconds(42, 42, 64, 1.0f) == 0.0);
    assert(std::abs(timestampMilliseconds(254, 3, 8, 1.0f) - 0.000005) < 1e-12);
    assert(std::abs(timestampMilliseconds(0xffff'fffe, 3, 32, 2.0f) - 0.00001) < 1e-12);
    assert(std::abs(timestampMilliseconds(std::numeric_limits<uint64_t>::max() - 2, 3, 64, 1.0f)
                    - 0.000006) < 1e-12);
    assert(std::abs(timestampMilliseconds(0xabc0'0000'00fe, 0xdef0'0000'0103, 8, 0.5f)
                    - 0.0000025) < 1e-12);
    const double cull = timestampMilliseconds(10, 50, 64, 1.0f);
    const double draw = timestampMilliseconds(20, 80, 64, 1.0f);
    const double total = timestampMilliseconds(10, 80, 64, 1.0f);
    assert(cull + draw > total); // Overlapping intervals are valid; summing pass times is not.
}
