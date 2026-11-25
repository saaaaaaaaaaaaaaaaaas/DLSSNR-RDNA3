#pragma once

namespace nr::renodx {
// The extended controls are exposed by the RDNA3 RenoDX route only.
#if NR_ARCH_RDNA3
inline constexpr float kNetworkMax = 4.0f;
inline constexpr float kSkinMax = 4.0f;
#else
inline constexpr float kNetworkMax = 2.0f;
inline constexpr float kSkinMax = 1.0f;
#endif
}
