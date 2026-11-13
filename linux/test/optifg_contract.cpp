// SPDX-License-Identifier: MIT
#include "../optifg/include/frame_contract.hpp"
#include "../optifg/include/sr_contract.hpp"
#include <cassert>
#include <limits>
#include <iostream>

int main() {
    assert(opti_bridge::temporal_extents(1280, 720, 1920, 1080));
    assert(opti_bridge::temporal_extents(1280, 720, 1280, 720));
    assert(opti_bridge::temporal_extents(960, 540, 1920, 1080));
    assert(!opti_bridge::temporal_extents(1920, 1080, 1280, 720));
    assert(!opti_bridge::temporal_extents(1280, 720, 1920, 1200));
    assert(!opti_bridge::temporal_extents(0, 720, 1920, 1080));
    assert(!opti_bridge::temporal_extents(1280, 720, 32768, 18432));
    OptiBridgeFrame f {};
    f.size = sizeof f; f.version = OPTI_BRIDGE_VERSION;
    f.frame_id = 1; f.color = f.motion = f.depth = reinterpret_cast<void*>(1);
    f.width = 1920; f.height = 1080; f.delta_ms = 16.6667f;
    f.camera_near = 0.1f; f.camera_far = 1000.f; f.vertical_fov_radians = 1.2f;
    f.flags = OPTI_BRIDGE_SDR;
    assert(opti_bridge::validate(&f) == OPTI_BRIDGE_OK);
    auto invalid = f; invalid.size -= 8;
    assert(opti_bridge::validate(&invalid) == OPTI_BRIDGE_BAD_VERSION);
    invalid = f; invalid.version++;
    assert(opti_bridge::validate(&invalid) == OPTI_BRIDGE_BAD_VERSION);
    invalid = f; invalid.depth = nullptr;
    assert(opti_bridge::validate(&invalid) == OPTI_BRIDGE_BAD_FRAME);
    invalid = f; invalid.camera_near = std::numeric_limits<float>::quiet_NaN();
    assert(opti_bridge::validate(&invalid) == OPTI_BRIDGE_BAD_FRAME);
    invalid = f; invalid.camera_far = invalid.camera_near;
    assert(opti_bridge::validate(&invalid) == OPTI_BRIDGE_BAD_FRAME);
    invalid = f; invalid.flags = 0x80000000u;
    assert(opti_bridge::validate(&invalid) == OPTI_BRIDGE_BAD_FRAME);

    opti_bridge::Timeline t;
    assert(t.needs_reset(f));
    assert(t.check(f) == OPTI_BRIDGE_OK);
    t.accept(f);
    assert(t.check(f) == OPTI_BRIDGE_DUPLICATE);
    ++f.frame_id;
    assert(t.check(f) == OPTI_BRIDGE_DUPLICATE); // Two producers in one real present.
    assert(t.present());
    assert(!t.needs_reset(f));
    t.accept(f);
    assert(t.present());
    assert(!t.present()); // Missing capture cannot reuse frame 2's guides.
    ++f.frame_id;
    assert(t.needs_reset(f));
    t.accept(f);
    assert(t.present());
    ++f.frame_id; f.width = 2560;
    assert(t.needs_reset(f)); // Resize preserves monotonic source ids.
    t.accept(f); assert(t.present());
    f.frame_id += 2;
    assert(t.needs_reset(f));
    t.accept(f); assert(t.present());
    ++f.frame_id; f.flags |= OPTI_BRIDGE_REVERSED_DEPTH;
    assert(t.needs_reset(f));
    t.accept(f); assert(t.present());
    ++f.frame_id; f.vertical_fov_radians += 0.1f;
    assert(t.needs_reset(f));
    std::cout << "OptiFG ABI, validation, duplicate, stale, gap and resize checks passed\n";
}
