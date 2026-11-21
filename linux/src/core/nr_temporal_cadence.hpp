#pragma once
#include <algorithm>
#include <cstdint>

namespace nr {
// Driven by real NR evaluations only, never by Present or FG callbacks. Reset,
// motion and rejected history deliberately are not inputs to the cadence.
struct TemporalCadence {
    uint64_t frames{}, full{}, reconstructed{}, unexpected{};
    uint32_t phase{};
    bool next(int interval) {
        const uint32_t n = uint32_t(std::clamp(interval, 2, 4));
        const bool run = phase == 0;
        phase = (phase + 1) % n;
        ++frames;
        run ? ++full : ++reconstructed;
        return run;
    }
};
} // namespace nr
