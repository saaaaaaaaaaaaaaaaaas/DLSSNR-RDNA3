#pragma once
#include "dittosr/constants.hpp"
#include <array>

namespace nr::ditto {
struct NeuralFrame {
    uint32_t rw, rh, ow, oh;
    float jitX, jitY, mvToUv[2], mvRes[2];
    bool jitteredMv, depthInverted, hdr;
    float preExposure, exposureMul, reactiveMul;
};
inline NeuralFrame neural_frame(uint32_t nr_w, uint32_t nr_h, uint32_t out_w, uint32_t out_h,
                                uint32_t render_w, uint32_t render_h, float jitter_x, float jitter_y,
                                bool jittered_motion, bool inverted) {
    // Neural RGB is already in NR's encoded proxy domain. Game HDR exposure must not be applied again.
    return {nr_w, nr_h, out_w, out_h, jitter_x * float(nr_w) / render_w, jitter_y * float(nr_h) / render_h,
            {1.f, 1.f}, {float(nr_w), float(nr_h)}, jittered_motion, inverted, false, 1.f, 1.f, 1.f};
}
inline std::array<float, 2> output_phase(float x, float y, uint32_t render_w, uint32_t render_h,
                                        uint32_t output_w, uint32_t output_h) {
    // Ditto reconstructs onto a stable grid. Rephase that result to match up_proxy and the next SR's input.
    return {x * float(output_w) / render_w, y * float(output_h) / render_h};
}
} // namespace nr::ditto
