// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace opti_bridge {
// Temporal SR accepts actual render buffers and a larger output of the same
// aspect ratio. Unsupported geometry can still use the spatial presenter.
inline bool temporal_extents(uint32_t iw, uint32_t ih, uint32_t ow, uint32_t oh) {
    return iw >= 20 && ih >= 20 && ow >= iw && oh >= ih && ow <= 16384 && oh <= 16384 &&
           uint64_t(iw) * oh == uint64_t(ih) * ow;
}
}
