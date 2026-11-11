#pragma once

#include <cstdint>

struct GpuTimings {
    double cullingMs = 0.0;
    double drawingMs = 0.0;
    double totalMs = 0.0;
};

// shortcut: intervals must be shorter than one timestamp-counter wrap; split longer GPU workloads.
inline double timestampMilliseconds(uint64_t start, uint64_t end, uint32_t validBits, float periodNs) {
    uint64_t ticks = end - start;
    if (validBits < 64) {
        ticks &= (uint64_t{1} << validBits) - 1;
    }
    return static_cast<double>(ticks) * static_cast<double>(periodNs) / 1'000'000.0;
}
