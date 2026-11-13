// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"
#include "ReShadeFG.h"
#include "ReShadeSR.h"
#include "frame_contract.hpp"
#include <Config.h>
#include <State.h>
#include <NVNGX_Parameter.h>
#include <with_dx12/dx11_with_dx12.h>
#include <with_dx12/with_dx12.h>
#include <framegen/IFGFeature_Dx12.h>
#include <wrl/client.h>
#include <mutex>

using Microsoft::WRL::ComPtr;
namespace {
std::mutex bridgeMutex;
ComPtr<ID3D11Device> sourceDevice;
Dx11WithDx12::D3D11_UPSCALER_RESOURCE_CACHE_C cache {};
Dx11WithDx12::D3D11_TEXTURE2D_RESOURCE_C hudlessCache[BUFFER_COUNT] {};
ComPtr<ID3D12Device> fenceDevice;
ComPtr<ID3D12Fence> copiedFence;
uint64_t copySerial = 0, slotSerial[BUFFER_COUNT] {};
opti_bridge::Timeline timeline;
OptiBridgeStats stats { sizeof(OptiBridgeStats), OPTI_BRIDGE_VERSION };
uint64_t token = 0, nextToken = 0, startedSerial = 0;
OptiBridgeFrame srFrame {};
bool srReady = false, srReset = true, srHudless = false;
UINT srSlot = 0;

ComPtr<IUnknown> Unwrap(IUnknown* object) {
    ComPtr<IUnknown> current = object;
    // ReShade 6.8 com_utils.hpp IID_UnwrappedObject: QueryInterface returns an
    // owned reference. No private proxy layout or adapter-only comparison.
    constexpr GUID unwrapped = {0x7f2c9a11, 0x3b4e, 0x4d6a, {0x81,0x2f,0x5e,0x9c,0xd3,0x7a,0x1b,0x42}};
    for (unsigned i = 0; current && i < 4; ++i) {
        ComPtr<IUnknown> original;
        if (FAILED(current->QueryInterface(unwrapped, reinterpret_cast<void**>(original.GetAddressOf()))) ||
            !original || original.Get() == current.Get()) break;
        current = original;
    }
    return current;
}
bool SameObject(IUnknown* a, IUnknown* b) {
    if (!a || !b) return false;
    ComPtr<IUnknown> x, y;
    if (SUCCEEDED(a->QueryInterface(IID_PPV_ARGS(&x))) &&
        SUCCEEDED(b->QueryInterface(IID_PPV_ARGS(&y))) && x.Get() == y.Get()) return true;
    auto originalA = Unwrap(a), originalB = Unwrap(b);
    x.Reset(); y.Reset();
    return originalA && originalB && SUCCEEDED(originalA->QueryInterface(IID_PPV_ARGS(&x))) &&
        SUCCEEDED(originalB->QueryInterface(IID_PPV_ARGS(&y))) && x.Get() == y.Get();
}
uint32_t Result(uint32_t status) {
    if (stats.last_status != status)
        LOG_INFO("ReShade FG bridge status: {}", status);
    stats.last_status = status;
    if (status != OPTI_BRIDGE_OK) ++stats.rejected;
    return status;
}
bool Selected() { return State::Instance().activeFgInput == FGInput::ReShade; }
void DropStream() {
    ReShadeSR::Reset();
    srReady = false; srReset = true;
    token = 0;
    timeline = {};
    startedSerial = 0;
    sourceDevice.Reset();
    // DeferredRelease holds the D3D12 allocations until outstanding work completes.
    Dx11WithDx12::ResetUpscalerResourceCache(cache);
    for (auto& entry : hudlessCache) Dx11WithDx12::ReleaseSharedResource(&entry);
    copiedFence.Reset(); fenceDevice.Reset();
    copySerial = 0;
    for (auto& value : slotSerial) value = 0;
}

uint32_t OPTI_BRIDGE_CALL Register(void* object, uint64_t hwnd, uint64_t* out) {
    std::lock_guard lock(bridgeMutex);
    if (!object || !out) return Result(OPTI_BRIDGE_BAD_FRAME);
    *out = 0;
    auto& state = State::Instance();
    if (!Selected()) return Result(OPTI_BRIDGE_DISABLED);
    if (state.isShuttingDown || state.swapchainInteropApi != SwapchainInteropApi::Dx11wDx12 ||
        !state.currentFG || !state.currentSwapchain || !WithDx12::IsInited())
        return Result(OPTI_BRIDGE_NOT_READY);
    if (state.activeFgOutput != FGOutput::FSRFG) return Result(OPTI_BRIDGE_UNSUPPORTED);
    ComPtr<ID3D11Device> device;
    auto original = Unwrap(static_cast<IUnknown*>(object));
    if (!original || FAILED(original->QueryInterface(IID_PPV_ARGS(&device))) ||
        !SameObject(device.Get(), state.currentD3D11Device) ||
        (uint64_t)(uintptr_t)state.currentFG->Hwnd() != hwnd)
        return Result(OPTI_BRIDGE_WRONG_DEVICE);
    // Only one producer owns the main game swapchain. An unrelated add-on must
    // not silently replace its stream or reset its history.
    if (token) return Result(OPTI_BRIDGE_DUPLICATE);
    sourceDevice = device;
    fenceDevice = WithDx12::GetD3D12Device();
    if (!copiedFence && FAILED(fenceDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&copiedFence)))) {
        DropStream(); return Result(OPTI_BRIDGE_GPU_ERROR);
    }
    token = ++nextToken;
    timeline = {};
    *out = token;
    LOG_INFO("ReShade FG stream {} registered (display-resolution pixel guides)", token);
    return Result(OPTI_BRIDGE_OK);
}

uint32_t Texture(void* object, const OptiBridgeFrame& f, DXGI_FORMAT format,
                 ComPtr<ID3D11Texture2D>& texture) {
    auto original = Unwrap(static_cast<IUnknown*>(object));
    if (!original || FAILED(original->QueryInterface(IID_PPV_ARGS(&texture))))
        return OPTI_BRIDGE_BAD_FRAME;
    ComPtr<ID3D11Device> owner;
    texture->GetDevice(&owner);
    if (!SameObject(owner.Get(), sourceDevice.Get())) return OPTI_BRIDGE_WRONG_DEVICE;
    D3D11_TEXTURE2D_DESC desc {};
    texture->GetDesc(&desc);
    if (desc.Width != f.width || desc.Height != f.height || desc.SampleDesc.Count != 1 ||
        desc.ArraySize != 1 || (format != DXGI_FORMAT_UNKNOWN && desc.Format != format))
        return OPTI_BRIDGE_BAD_FRAME;
    return OPTI_BRIDGE_OK;
}

uint32_t SubmitImpl(uint64_t stream, const OptiBridgeFrame* frame) {
    if (!token || stream != token) return Result(OPTI_BRIDGE_NOT_READY);
    auto status = opti_bridge::validate(frame);
    if (status != OPTI_BRIDGE_OK) { timeline.reset = true; return Result(status); }
    auto& state = State::Instance();
    if (!Selected() || (!Config::Instance()->IsFGEnabled() && !ReShadeSR::Enabled())) {
        timeline.reset = true;
        return Result(OPTI_BRIDGE_DISABLED);
    }
    if (state.activeFgOutput != FGOutput::FSRFG) return Result(OPTI_BRIDGE_UNSUPPORTED);
    auto* fg = state.currentFG;
    if (!fg || state.isShuttingDown || !WithDx12::IsInited() ||
        state.swapchainInteropApi != SwapchainInteropApi::Dx11wDx12 ||
        !SameObject(sourceDevice.Get(), state.currentD3D11Device))
        return Result(OPTI_BRIDGE_NOT_READY);
    if ((status = timeline.check(*frame)) != OPTI_BRIDGE_OK) return Result(status);

    ComPtr<ID3D11Texture2D> color, motion, depth, hudless;
    if ((status = Texture(frame->color, *frame, DXGI_FORMAT_UNKNOWN, color)) != OPTI_BRIDGE_OK ||
        (status = Texture(frame->motion, *frame, DXGI_FORMAT_R16G16_FLOAT, motion)) != OPTI_BRIDGE_OK ||
        (status = Texture(frame->depth, *frame, DXGI_FORMAT_R32_FLOAT, depth)) != OPTI_BRIDGE_OK ||
        (frame->hudless && (status = Texture(frame->hudless, *frame, DXGI_FORMAT_UNKNOWN, hudless)) != OPTI_BRIDGE_OK)) {
        timeline.reset = true;
        return Result(status);
    }
    D3D11_TEXTURE2D_DESC colorDesc {};
    color->GetDesc(&colorDesc);
    // Initial qualification is SDR; HDR requires matching transfer functions,
    // white level and FG context flags on both sides of the presentation bridge.
    const bool sdr10 = colorDesc.Format == DXGI_FORMAT_R10G10B10A2_UNORM && (frame->flags & OPTI_BRIDGE_SDR);
    if (!sdr10 && colorDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM && colorDesc.Format != DXGI_FORMAT_B8G8R8A8_UNORM &&
        colorDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB && colorDesc.Format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
        return Result(OPTI_BRIDGE_UNSUPPORTED);
    if (hudless) {
        D3D11_TEXTURE2D_DESC desc {}; hudless->GetDesc(&desc);
        if (desc.Format != colorDesc.Format) return Result(OPTI_BRIDGE_BAD_FRAME);
    }

    auto* device12 = WithDx12::GetD3D12Device();
    if (device12 != fenceDevice.Get()) return Result(OPTI_BRIDGE_NOT_READY);
    const UINT nextIndex = cache.FrameIndex + 1;
    const UINT slot = nextIndex % BUFFER_COUNT;
    const auto completed = copiedFence->GetCompletedValue();
    if (completed == UINT64_MAX) return Result(OPTI_BRIDGE_GPU_ERROR);
    // No GPU-side reverse wait: DXVK and vkd3d-proton may share one VkQueue.
    // A busy ring slot drops this frame rather than creating a queue cycle.
    if (completed < slotSerial[slot]) { timeline.reset = true; return Result(OPTI_BRIDGE_BUSY); }
    ComPtr<ID3D11DeviceContext> context;
    sourceDevice->GetImmediateContext(&context);
    Dx11WithDx12::Init(sourceDevice.Get(), context.Get(), device12, WithDx12::GetD3D12CommandQueue());
    NVNGX_Parameters parameters(API::DX11, false);
    parameters.Set(NVSDK_NGX_Parameter_MotionVectors, static_cast<ID3D11Resource*>(motion.Get()));
    parameters.Set(NVSDK_NGX_Parameter_Depth, static_cast<ID3D11Resource*>(depth.Get()));
    const auto mask = Dx11WithDx12::ResourceMask::Mv | Dx11WithDx12::ResourceMask::Depth;
    const auto id = Dx11WithDx12::NextUpscalerFrameId();
    const bool legacySharing = Config::Instance()->DontUseNTShared.value_or_default();
    if (!Dx11WithDx12::PrepareUpscalerResources(cache, &parameters, mask, nextIndex,
                                                id, legacySharing, false, false).Success ||
        (hudless && !Dx11WithDx12::PrepareTextureFrom11To12("ReShadeHudless", device12, hudless.Get(),
                        &hudlessCache[slot], true, false, legacySharing, id)) ||
        !Dx11WithDx12::SyncDx11ToDx12()) {
        timeline.reset = true;
        return Result(OPTI_BRIDGE_GPU_ERROR);
    }
    const bool srEnabled = ReShadeSR::Enabled();
    const bool reset = timeline.needs_reset(*frame);
    srFrame = *frame; srFrame.color = srFrame.motion = srFrame.depth = srFrame.hudless = nullptr;
    srReset = reset; srHudless = hudless != nullptr; srSlot = slot;
    auto acceptSR = [&]() {
        timeline.accept(*frame);
        ++stats.submitted; stats.last_frame_id = frame->frame_id;
        return Result(OPTI_BRIDGE_OK);
    };
    if (!Config::Instance()->IsFGEnabled()) return acceptSR();
    UINT displayWidth = frame->width, displayHeight = frame->height;
    ReShadeSR::DisplaySize(displayWidth, displayHeight);
    FG_Constants constants {};
    constants.displayWidth = displayWidth; constants.displayHeight = displayHeight;
    if (!srEnabled) constants.flags |= FG_Flags::DisplayResolutionMVs;
    if (frame->flags & OPTI_BRIDGE_REVERSED_DEPTH) constants.flags |= FG_Flags::InvertedDepth;
    if (frame->flags & OPTI_BRIDGE_INFINITE_DEPTH) constants.flags |= FG_Flags::InfiniteDepth;
    if (Config::Instance()->FGAsync.value_or_default()) constants.flags |= FG_Flags::Async;
    fg->EvaluateState(device12, constants);
    // EvaluateState owns the FG mutex itself. Only protect resource publication
    // after it returns; wrapping that call would recursively lock a shared_mutex.
    OwnedLockGuard fgLock(fg->Mutex, 4);
    fg->StartNewFrame();
    startedSerial = timeline.serial + 1;
    if (fg->IsPaused() || !fg->IsActive())
        return srEnabled ? acceptSR() : Result(OPTI_BRIDGE_WARMING_UP);
    float nearPlane = frame->camera_near;
    float farPlane = (frame->flags & OPTI_BRIDGE_INFINITE_DEPTH) ? FLT_MAX : frame->camera_far;
    if (frame->flags & OPTI_BRIDGE_REVERSED_DEPTH) std::swap(nearPlane, farPlane);
    fg->SetCameraValues(nearPlane, farPlane, frame->vertical_fov_radians,
                        float(frame->width) / float(frame->height), 1.0f);
    fg->SetFrameTimeDelta(frame->delta_ms);
    fg->SetMVScale(1.0f / float(frame->width), 1.0f / float(frame->height));
    fg->SetJitter(0.0f, 0.0f);
    fg->SetReset(reset ? 1 : 0);
    fg->SetInterpolationRect(displayWidth, displayHeight);

    auto* cmd = fg->GetUICommandList();
    bool copied = cmd != nullptr;
    auto set = [&](FG_ResourceType type, ID3D12Resource* resource) {
        Dx12Resource input {};
        input.type = type; input.resource = resource; input.cmdList = cmd;
        input.width = frame->width; input.height = frame->height;
        input.state = D3D12_RESOURCE_STATE_COMMON;
        input.validity = FG_ResourceValidity::ValidNow;
        return resource && cmd && fg->SetResource(&input);
    };
    // FSRFG copies ValidNow inputs to its own per-frame buffers. Submit those
    // copies before returning. The next reuse of this ring slot checks their fence.
    copied = set(FG_ResourceType::Velocity, cache.CurrentMv().Dx12Resource) && copied;
    copied = set(FG_ResourceType::Depth, cache.CurrentDepth().Dx12Resource) && copied;
    if (hudless && !srEnabled) copied = set(FG_ResourceType::HudlessColor, hudlessCache[slot].Dx12Resource) && copied;
    const bool submitted = fg->FinishExternalInputs();
    const auto copyValue = ++copySerial;
    const bool signalled = SUCCEEDED(WithDx12::GetD3D12CommandQueue()->Signal(copiedFence.Get(), copyValue));
    slotSerial[slot] = copyValue;
    if (!submitted || !signalled || !copied) {
        timeline.reset = true;
        return Result(OPTI_BRIDGE_GPU_ERROR);
    }
    timeline.accept(*frame);
    ++stats.submitted; stats.last_frame_id = frame->frame_id;
    return Result(OPTI_BRIDGE_OK);
}

uint32_t OPTI_BRIDGE_CALL Submit(uint64_t stream, const OptiBridgeFrame* frame) {
    std::lock_guard lock(bridgeMutex);
    try { return SubmitImpl(stream, frame); }
    catch (...) { timeline.reset = true; return Result(OPTI_BRIDGE_GPU_ERROR); }
}
void OPTI_BRIDGE_CALL Reset(uint64_t stream) {
    std::lock_guard lock(bridgeMutex);
    if (token && stream == token) { timeline.reset = true; timeline.pending_serial = 0; }
}
void OPTI_BRIDGE_CALL Unregister(uint64_t stream) {
    std::lock_guard lock(bridgeMutex);
    if (token && stream == token) DropStream();
}
uint32_t OPTI_BRIDGE_CALL GetStats(uint64_t stream, OptiBridgeStats* out) {
    std::lock_guard lock(bridgeMutex);
    if (!out || out->size < sizeof(OptiBridgeStats) || out->version != OPTI_BRIDGE_VERSION)
        return OPTI_BRIDGE_BAD_VERSION;
    if (!token || stream != token) return OPTI_BRIDGE_NOT_READY;
    *out = stats;
    return OPTI_BRIDGE_OK;
}
const OptiReShadeBridge api { sizeof(OptiReShadeBridge), OPTI_BRIDGE_VERSION,
                            Register, Submit, Reset, Unregister, GetStats };
}

extern "C" __declspec(dllexport) const OptiReShadeBridge* OPTI_BRIDGE_CALL
OptiScaler_GetReShadeBridge(uint32_t version) {
    return version == OPTI_BRIDGE_VERSION ? &api : nullptr;
}

void ReShadeFG_BeforePresent(ID3D11Device* device) {
    std::lock_guard lock(bridgeMutex);
    if (!Selected()) return;
    if (token && !SameObject(device, sourceDevice.Get())) return;
    const bool ready = timeline.present();
    srReady = ready;
    if (ready) ++stats.presented;
    else if (auto* fg = State::Instance().currentFG) {
        OwnedLockGuard fgLock(fg->Mutex, 4);
        // An absent capture must never dispatch the previous real frame's guides.
        if (startedSerial != timeline.serial) fg->StartNewFrame();
        fg->SetReset(1);
        // Disable this present explicitly: a backend may otherwise choose an
        // older queued frame even though the newest slot has no valid guides.
        // EvaluateState reactivates the existing context on the next good frame.
        fg->Deactivate();
    }
}
void ReShadeFG_Destroy(ID3D11Device* device) {
    std::lock_guard lock(bridgeMutex);
    if (token && SameObject(device, sourceDevice.Get())) DropStream();
    else if (!token) ReShadeSR::Reset();
}

void ReShadeFG_DisplaySize(UINT& width, UINT& height) { ReShadeSR::DisplaySize(width, height); }
bool ReShadeFG_RecordPresentation(ID3D12GraphicsCommandList* cmd, ID3D12Resource* source,
                                  ID3D12Resource* destination, UINT slot) {
    std::lock_guard lock(bridgeMutex);
    return ReShadeSR::Record(cmd, source, destination,
        srReady ? cache.CurrentMv().Dx12Resource : nullptr,
        srReady ? cache.CurrentDepth().Dx12Resource : nullptr,
        srReady && srHudless ? hudlessCache[srSlot].Dx12Resource : nullptr,
        srReady ? &srFrame : nullptr, srReset, slot);
}
void ReShadeFG_PresentationSubmitted(ID3D12CommandQueue* queue) {
    std::lock_guard lock(bridgeMutex);
    if (!copiedFence) {
        if (FAILED(queue->GetDevice(IID_PPV_ARGS(&fenceDevice))) ||
            FAILED(fenceDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&copiedFence)))) return;
    }
    const auto value = ++copySerial;
    if (SUCCEEDED(queue->Signal(copiedFence.Get(), value))) {
        slotSerial[srSlot] = value;
        ReShadeSR::Submitted(copiedFence.Get(), value);
    }
}

void ReShadeFG_Resize(ID3D11Device* device) {
    std::lock_guard lock(bridgeMutex);
    if (token && !SameObject(device, sourceDevice.Get())) return;
    timeline.reset = true; timeline.pending_serial = 0;
    srReady = false; srReset = true;
    ReShadeSR::Reset();
}
