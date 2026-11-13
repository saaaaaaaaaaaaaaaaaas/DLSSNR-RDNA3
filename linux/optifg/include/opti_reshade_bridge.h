// SPDX-License-Identifier: MIT
#ifndef OPTI_RESHADE_BRIDGE_H
#define OPTI_RESHADE_BRIDGE_H
#include <stdint.h>

#ifdef _WIN32
#define OPTI_BRIDGE_CALL __cdecl
#else
#define OPTI_BRIDGE_CALL
#endif
#ifdef __cplusplus
extern "C" {
#endif

#define OPTI_BRIDGE_VERSION 1u
#define OPTI_BRIDGE_EXPORT "OptiScaler_GetReShadeBridge"
enum OptiBridgeStatus {
    OPTI_BRIDGE_OK = 0, OPTI_BRIDGE_NOT_READY, OPTI_BRIDGE_DISABLED,
    OPTI_BRIDGE_BAD_VERSION, OPTI_BRIDGE_BAD_FRAME, OPTI_BRIDGE_WRONG_DEVICE,
    OPTI_BRIDGE_DUPLICATE, OPTI_BRIDGE_UNSUPPORTED, OPTI_BRIDGE_GPU_ERROR, OPTI_BRIDGE_BUSY,
    OPTI_BRIDGE_WARMING_UP
};
enum OptiBridgeFlags {
    OPTI_BRIDGE_RESET = 1u, OPTI_BRIDGE_REVERSED_DEPTH = 2u,
    OPTI_BRIDGE_INFINITE_DEPTH = 4u, OPTI_BRIDGE_ESTIMATED_MOTION = 8u,
    OPTI_BRIDGE_MANUAL_CAMERA = 16u, OPTI_BRIDGE_SDR = 32u
};

#pragma pack(push, 8)
// All objects are borrowed IUnknown pointers, valid for the duration of Submit.
// DXVK D3D10 objects must expose ID3D11Texture2D/ID3D11Device through QueryInterface.
// Submit copies guides on the GPU and orders source reuse through shared fences.
// Color identifies the game backbuffer; it is NOT implicitly treated as HUDless.
typedef struct OptiBridgeFrame {
    uint32_t size, version;
    uint64_t frame_id;
    void *color, *motion, *depth, *hudless;
    uint32_t width, height, flags, reserved;
    // Full-resolution pixel displacement: previous_uv = current_uv + mv / extent.
    // Raw device depth in [0,1]. No linearized ReShade depth is accepted.
    float delta_ms, camera_near, camera_far, vertical_fov_radians;
    // Non-jittered, display-resolution estimated flow is the initial contract.
} OptiBridgeFrame;

typedef struct OptiBridgeStats {
    uint32_t size, version, last_status, reserved;
    uint64_t submitted, presented, rejected, last_frame_id;
} OptiBridgeStats;

typedef struct OptiReShadeBridge {
    uint32_t size, version;
    uint32_t (OPTI_BRIDGE_CALL *Register)(void *device, uint64_t hwnd, uint64_t *stream);
    uint32_t (OPTI_BRIDGE_CALL *Submit)(uint64_t stream, const OptiBridgeFrame *frame);
    void (OPTI_BRIDGE_CALL *Reset)(uint64_t stream);
    void (OPTI_BRIDGE_CALL *Unregister)(uint64_t stream);
    uint32_t (OPTI_BRIDGE_CALL *GetStats)(uint64_t stream, OptiBridgeStats *stats);
} OptiReShadeBridge;
#pragma pack(pop)

typedef const OptiReShadeBridge *(OPTI_BRIDGE_CALL *OptiGetReShadeBridge)(uint32_t version);
#ifdef __cplusplus
}
#if INTPTR_MAX == INT64_MAX
static_assert(sizeof(OptiBridgeFrame) == 80, "ReShade bridge ABI layout");
static_assert(sizeof(OptiBridgeStats) == 48, "ReShade bridge stats ABI layout");
static_assert(sizeof(OptiReShadeBridge) == 48, "ReShade bridge function table ABI layout");
#endif
#endif
#endif
