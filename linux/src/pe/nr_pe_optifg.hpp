// SPDX-License-Identifier: MIT
#pragma once
#include "../../optifg/include/frame_contract.hpp"
#include "nr_path_win.hpp"
#include "nr_pe_log.hpp"
#include <windows.h>
#include <algorithm>
#include <cstdlib>
#include <string>

namespace nr::pe::optifg {
class Client {
    const OptiReShadeBridge* api_ = nullptr;
    HMODULE module_ = nullptr;
    uint64_t stream_ = 0, sequence_ = 0, polled_ = 0, probed_ = 0;
    uint32_t status_ = ~0u;
    bool enabled_ = false, reset_ = true, key_down_ = false, infinite_ = false;
    float near_ = 0, far_ = 0, fov_ = 0;
    LARGE_INTEGER last_ {}, frequency_ {};

    void status(uint32_t value) {
        if (value != status_) {
            log("[optifg] bridge status %u (0 ready, 1 waiting, 2 disabled, 4 invalid data, 7 unsupported)", value);
            status_ = value;
        }
    }
    void discover() {
        const auto now = GetTickCount64();
        if (now - probed_ < 1000) return;
        probed_ = now;
        using Enum = BOOL (WINAPI*)(HANDLE, HMODULE*, DWORD, LPDWORD);
        auto enumModules = reinterpret_cast<Enum>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "K32EnumProcessModules"));
        if (!enumModules) return;
        HMODULE modules[1024]; DWORD bytes = 0;
        if (!enumModules(GetCurrentProcess(), modules, sizeof modules, &bytes)) return;
        for (unsigned i = 0; i < std::min<unsigned>(bytes / sizeof(HMODULE), 1024); ++i) {
            auto get = reinterpret_cast<OptiGetReShadeBridge>(GetProcAddress(modules[i], OPTI_BRIDGE_EXPORT));
            if (!get) continue;
            const auto* api = get(OPTI_BRIDGE_VERSION);
            if (!api || api->version != OPTI_BRIDGE_VERSION || api->size < sizeof(*api) ||
                !api->Register || !api->Submit || !api->Reset || !api->Unregister || !api->GetStats) continue;
            // Keep function pointers valid until close(), outside the loader lock.
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                    reinterpret_cast<LPCWSTR>(get), &module_)) continue;
            api_ = api;
            log("[optifg] matching OptiScaler ReShade bridge found");
            return;
        }
    }
public:
    void poll(const std::string& path) {
        const auto now = GetTickCount64();
        if (polled_ && now - polled_ < 1000) return;
        polled_ = now;
        auto number = [&](const char* key) {
            char value[64] {};
            u8::profile("OptiFGBridge", key, "", value, sizeof value, path.c_str());
            char* end = nullptr;
            const float result = std::strtof(value, &end);
            return end != value && *end == '\0' ? result : 0.0f;
        };
        const bool enabled = number("Enabled") == 1;
        const bool infinite = number("InfiniteDepth") == 1;
        const float cameraNear = number("CameraNear"), cameraFar = number("CameraFar");
        const float fov = number("VerticalFovDegrees") * (3.14159265f / 180.0f);
        if (enabled != enabled_ || infinite != infinite_ || cameraNear != near_ || cameraFar != far_ || fov != fov_)
            reset();
        enabled_ = enabled; infinite_ = infinite;
        near_ = cameraNear; far_ = cameraFar; fov_ = fov;
        if (!enabled_ && stream_) close();
    }
    bool enabled() const { return enabled_; }
    void reset() {
        reset_ = true; last_.QuadPart = 0;
        if (api_ && stream_) api_->Reset(stream_);
    }
    void close() {
        if (api_ && stream_) api_->Unregister(stream_);
        stream_ = 0; api_ = nullptr;
        if (module_) FreeLibrary(module_);
        module_ = nullptr; reset_ = true; last_.QuadPart = 0;
    }
    void submit(void* device, uint64_t hwnd, OptiBridgeFrame frame) {
        if (!enabled_) return;
#if UINTPTR_MAX != UINT64_MAX
        status(OPTI_BRIDGE_UNSUPPORTED); return;
#else
        const bool key = (GetAsyncKeyState(VK_F7) & 0x8000) != 0;
        if (key && !key_down_) reset();
        key_down_ = key;
        LARGE_INTEGER now; QueryPerformanceCounter(&now);
        if (!frequency_.QuadPart) QueryPerformanceFrequency(&frequency_);
        const double delta = last_.QuadPart ? double(now.QuadPart - last_.QuadPart) * 1000.0 / frequency_.QuadPart : 16.6667;
        last_ = now;
        frame.size = sizeof frame; frame.version = OPTI_BRIDGE_VERSION;
        frame.frame_id = ++sequence_;
        frame.flags |= OPTI_BRIDGE_ESTIMATED_MOTION | OPTI_BRIDGE_MANUAL_CAMERA;
        if (reset_ || delta > 250.0) frame.flags |= OPTI_BRIDGE_RESET;
        if (infinite_) frame.flags |= OPTI_BRIDGE_INFINITE_DEPTH;
        frame.delta_ms = float(std::clamp(delta, 0.01, 1000.0));
        frame.camera_near = near_; frame.camera_far = far_; frame.vertical_fov_radians = fov_;
        auto result = opti_bridge::validate(&frame);
        if (result != OPTI_BRIDGE_OK) { reset(); status(result); return; }
        if (!api_) discover();
        if (!api_) { status(OPTI_BRIDGE_NOT_READY); return; }
        if (!stream_) {
            result = api_->Register(device, hwnd, &stream_);
            if (result != OPTI_BRIDGE_OK) { status(result); return; }
        }
        result = api_->Submit(stream_, &frame);
        status(result);
        if (result == OPTI_BRIDGE_OK) reset_ = false;
        else {
            reset_ = true;
            if (result == OPTI_BRIDGE_NOT_READY || result == OPTI_BRIDGE_WRONG_DEVICE) close();
        }
        if (stream_ && sequence_ % 300 == 0) {
            OptiBridgeStats stats { sizeof stats, OPTI_BRIDGE_VERSION };
            if (api_->GetStats(stream_, &stats) == OPTI_BRIDGE_OK)
                log("[optifg] captured %llu, consumed presents %llu, rejected %llu; source: estimated flow, manual camera",
                    (unsigned long long)stats.submitted, (unsigned long long)stats.presented, (unsigned long long)stats.rejected);
        }
#endif
    }
};
}
