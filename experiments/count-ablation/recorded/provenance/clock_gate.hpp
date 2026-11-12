#pragma once
#include <algorithm>
#include <span>

struct ClockSample { double seconds; double mhz; };

inline bool clockInRange(double mhz) { return mhz >= 1990.0 && mhz <= 2010.0; }

inline bool stableClockWindow(std::span<const ClockSample> samples, double warmupSeconds) {
    if (warmupSeconds < 5.0 || samples.empty() || samples.back().seconds - samples.front().seconds < 1.0) {
        return false;
    }
    const auto [low, high] = std::minmax_element(samples.begin(), samples.end(),
        [](const ClockSample& a, const ClockSample& b) { return a.mhz < b.mhz; });
    return clockInRange(low->mhz) && clockInRange(high->mhz) && high->mhz - low->mhz <= 10.0;
}
