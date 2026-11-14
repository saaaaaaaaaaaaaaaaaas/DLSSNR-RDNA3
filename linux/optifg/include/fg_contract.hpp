// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cmath>

namespace opti_bridge {
struct MotionScale { float x, y; };
inline MotionScale fg_motion_scale(bool pixel_units, unsigned width, unsigned height) {
    return pixel_units ? MotionScale{1.f, 1.f} : MotionScale{1.f / width, 1.f / height};
}

// Explicit manual camera approximation. Optical flow already contains camera
// motion; no engine view transform is available through the ReShade ABI.
inline std::array<float, 16> manual_projection(float near_plane, float far_plane,
        float vertical_fov, float aspect, bool reversed, bool infinite) {
    std::array<float, 16> p {};
    const float y = 1.f / std::tan(vertical_fov * 0.5f);
    p[0] = y / aspect; p[5] = y; p[11] = 1.f;
    if (infinite) {
        // Avoid infinity/infinity in a generic perspective-matrix helper.
        p[10] = reversed ? 0.f : 1.f;
        p[14] = reversed ? near_plane : -near_plane;
    } else {
        const float n = reversed ? far_plane : near_plane;
        const float f = reversed ? near_plane : far_plane;
        p[10] = f / (f - n); p[14] = -n * p[10];
    }
    return p;
}
}
