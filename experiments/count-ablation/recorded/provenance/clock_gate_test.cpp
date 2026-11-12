#include "clock_gate.hpp"
#include <cassert>
#include <array>
#include <limits>

int main() {
    const std::array<ClockSample, 3> stable{{{0.0, 1995.0}, {0.5, 2000.0}, {1.0, 2005.0}}};
    assert(stableClockWindow(stable, 5.0));
    assert(!stableClockWindow(stable, 4.99));
    assert(!stableClockWindow(std::span(stable).first(2), 5.0));
    assert(!stableClockWindow({}, 5.0));
    auto noisy = stable;
    noisy[1].mhz = 1989.0;
    assert(!stableClockWindow(noisy, 5.0));
    noisy[1].mhz = 2011.0;
    assert(!stableClockWindow(noisy, 5.0));
    noisy[1].mhz = 1990.0;
    assert(!stableClockWindow(noisy, 5.0));
    assert(!clockInRange(std::numeric_limits<double>::quiet_NaN()));
    assert(clockInRange(1990.0) && clockInRange(2010.0));
}
