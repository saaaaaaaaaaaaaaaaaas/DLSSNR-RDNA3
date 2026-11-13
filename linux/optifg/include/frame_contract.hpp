// SPDX-License-Identifier: MIT
#pragma once
#include "opti_reshade_bridge.h"
#include <cmath>

namespace opti_bridge {
inline uint32_t validate(const OptiBridgeFrame* f) {
    if (!f || f->size < sizeof(OptiBridgeFrame) || f->version != OPTI_BRIDGE_VERSION)
        return OPTI_BRIDGE_BAD_VERSION;
    constexpr uint32_t known = OPTI_BRIDGE_RESET | OPTI_BRIDGE_REVERSED_DEPTH |
        OPTI_BRIDGE_INFINITE_DEPTH | OPTI_BRIDGE_ESTIMATED_MOTION | OPTI_BRIDGE_MANUAL_CAMERA;
    if (!f->frame_id || !f->color || !f->motion || !f->depth || f->reserved || (f->flags & ~known) ||
        !f->width || !f->height || f->width > 16384 || f->height > 16384 ||
        !std::isfinite(f->delta_ms) || f->delta_ms <= 0 || f->delta_ms > 1000 ||
        !std::isfinite(f->camera_near) || f->camera_near <= 0 ||
        !std::isfinite(f->camera_far) || f->camera_far <= f->camera_near ||
        !std::isfinite(f->vertical_fov_radians) || f->vertical_fov_radians <= 0 ||
        f->vertical_fov_radians >= 3.14159265f)
        return OPTI_BRIDGE_BAD_FRAME;
    return OPTI_BRIDGE_OK;
}

// Presentation serials refer to game frames, never the FG library's presents.
struct Timeline {
    uint64_t serial = 0, pending_serial = 0, last_frame = 0;
    uint32_t width = 0, height = 0;
    bool reset = true;
    uint32_t check(const OptiBridgeFrame& f) const {
        return f.frame_id <= last_frame || pending_serial == serial + 1
            ? OPTI_BRIDGE_DUPLICATE : OPTI_BRIDGE_OK;
    }
    bool needs_reset(const OptiBridgeFrame& f) const {
        return reset || f.width != width || f.height != height ||
            (last_frame && f.frame_id != last_frame + 1) || (f.flags & OPTI_BRIDGE_RESET);
    }
    void accept(const OptiBridgeFrame& f) {
        last_frame = f.frame_id; width = f.width; height = f.height;
        pending_serial = serial + 1; reset = false;
    }
    bool present() {
        ++serial;
        const bool ready = pending_serial == serial;
        if (!ready) reset = true;
        return ready;
    }
};
}
