// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"
#include "ReShadeSR.h"
#include "sr_contract.hpp"
#include <Config.h>
#include <State.h>
#include <NVNGX_Parameter.h>
#include <upscalers/FeatureProvider_Dx12.h>
#include <framegen/IFGFeature_Dx12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>

using Microsoft::WRL::ComPtr;
namespace ReShadeSR {
namespace {
constexpr UINT heapCount = 16;
struct Context {
    ID3D12Device* device = nullptr;
    std::unique_ptr<IFeature_Dx12> feature;
    ComPtr<ID3D12Resource> output;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> shader;
    ComPtr<ID3D12DescriptorHeap> heaps[heapCount];
    ComPtr<ID3D12Fence> fence;
    UINT64 value = 0;
    UINT width = 0, height = 0, sourceWidth = 0, sourceHeight = 0, flags = 0;
    int ffxIndex = -1;
    std::string custom;
    bool hudless = false, gap = true;
    UINT64 frames = 0, fallbacks = 0, retiredAt = 0;
    DXGI_FORMAT sourceFormat = DXGI_FORMAT_UNKNOWN, outputFormat = DXGI_FORMAT_UNKNOWN;
};
std::unique_ptr<Context> current;
std::vector<std::unique_ptr<Context>> retired;

void Transition(ID3D12GraphicsCommandList* cmd, ID3D12Resource* resource,
                D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    if (!resource || before == after) return;
    D3D12_RESOURCE_BARRIER b {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = resource;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = before; b.Transition.StateAfter = after;
    cmd->ResourceBarrier(1, &b);
}
void Collect() {
    std::erase_if(retired, [](const auto& p) {
        return GetTickCount64() - p->retiredAt >= 1000 &&
               (!p->fence || p->fence->GetCompletedValue() >= p->value);
    });
}
void Retire() {
    if (!current) return;
    if (current->feature) {
        auto& state = State::Instance();
        if (state.currentFeature == current->feature.get()) state.currentFeature = nullptr;
        state.changeBackend.erase(current->feature->Handle()->Id);
    }
    // Hold every descriptor/context/output until its final submitted list completes.
    // This also covers provider changes without a CPU or reverse GPU wait.
    current->retiredAt = GetTickCount64();
    retired.push_back(std::move(current));
    Collect();
}
const char* spatialShader = R"(
Texture2D<float4> color : register(t0);
Texture2D<float4> scene : register(t1);
RWTexture2D<float4> output : register(u0);
SamplerState linearClamp : register(s0);
cbuffer Constants : register(b0) { uint width; uint height; uint compose; uint unused; };
[numthreads(8,8,1)]
void main(uint3 p : SV_DispatchThreadID) {
    if (p.x >= width || p.y >= height) return;
    float2 uv = (float2(p.xy) + 0.5) / float2(width, height);
    float4 finalColor = color.SampleLevel(linearClamp, uv, 0);
    if (compose == 0) output[p.xy] = finalColor;
    else {
        // A same-frame HUD color delta, not an invented alpha/reactive mask.
        float3 delta = finalColor.rgb - scene.SampleLevel(linearClamp, uv, 0).rgb;
        output[p.xy] = float4(saturate(output[p.xy].rgb + delta), finalColor.a);
    }
}
)";
bool MakeShader(Context& c) {
    ComPtr<ID3DBlob> code, error, signature;
    if (FAILED(D3DCompile(spatialShader, strlen(spatialShader), nullptr, nullptr, nullptr,
                          "main", "cs_5_0", 0, 0, &code, &error))) return false;
    D3D12_DESCRIPTOR_RANGE ranges[2] {};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; ranges[0].NumDescriptors = 2;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV; ranges[1].NumDescriptors = 1;
    D3D12_ROOT_PARAMETER p[3] {};
    p[0].ParameterType = p[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    p[0].DescriptorTable = {1, &ranges[0]}; p[1].DescriptorTable = {1, &ranges[1]};
    p[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    p[2].Constants.Num32BitValues = 4;
    D3D12_STATIC_SAMPLER_DESC sampler {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    D3D12_ROOT_SIGNATURE_DESC desc {3, p, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error)) ||
        FAILED(c.device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                             IID_PPV_ARGS(&c.root)))) return false;
    D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline {};
    pipeline.pRootSignature = c.root.Get();
    pipeline.CS = {code->GetBufferPointer(), code->GetBufferSize()};
    return SUCCEEDED(c.device->CreateComputePipelineState(&pipeline, IID_PPV_ARGS(&c.shader)));
}
bool Spatial(Context& c, ID3D12GraphicsCommandList* cmd, ID3D12Resource* source,
             ID3D12Resource* scene, UINT slot, bool compose) {
    if (slot >= heapCount || (!c.root && !MakeShader(c))) return false;
    if (!c.heaps[slot]) {
        D3D12_DESCRIPTOR_HEAP_DESC desc {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; desc.NumDescriptors = 3;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(c.device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&c.heaps[slot])))) return false;
    }
    auto heap = c.heaps[slot].Get();
    auto cpu = heap->GetCPUDescriptorHandleForHeapStart();
    auto gpu = heap->GetGPUDescriptorHandleForHeapStart();
    const auto stride = c.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto srv = [&](ID3D12Resource* input) {
        D3D12_SHADER_RESOURCE_VIEW_DESC d {};
        d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        d.Format = input->GetDesc().Format;
        d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        d.Texture2D.MipLevels = 1;
        c.device->CreateShaderResourceView(input, &d, cpu); cpu.ptr += stride;
    };
    srv(source); srv(scene ? scene : source);
    D3D12_UNORDERED_ACCESS_VIEW_DESC u {};
    u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D; u.Format = c.output->GetDesc().Format;
    c.device->CreateUnorderedAccessView(c.output.Get(), nullptr, &u, cpu);
    cmd->SetDescriptorHeaps(1, &heap);
    cmd->SetComputeRootSignature(c.root.Get()); cmd->SetPipelineState(c.shader.Get());
    cmd->SetComputeRootDescriptorTable(0, gpu); gpu.ptr += 2 * stride;
    cmd->SetComputeRootDescriptorTable(1, gpu);
    const UINT constants[] = {c.width, c.height, compose ? 1u : 0u, 0u};
    cmd->SetComputeRoot32BitConstants(2, 4, constants, 0);
    cmd->Dispatch((c.width + 7) / 8, (c.height + 7) / 8, 1);
    D3D12_RESOURCE_BARRIER b {}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; b.UAV.pResource = c.output.Get();
    cmd->ResourceBarrier(1, &b);
    return true;
}

struct BarrierSettings {
    Config& c = *Config::Instance();
    decltype(c.ColorResourceBarrier) color = c.ColorResourceBarrier;
    decltype(c.MVResourceBarrier) motion = c.MVResourceBarrier;
    decltype(c.DepthResourceBarrier) depth = c.DepthResourceBarrier;
    decltype(c.OutputResourceBarrier) output = c.OutputResourceBarrier;
    decltype(c.FsrNonLinearSRGB) srgb = c.FsrNonLinearSRGB;
    BarrierSettings() {
        c.ColorResourceBarrier.set_volatile_value(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        c.MVResourceBarrier.set_volatile_value(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        c.DepthResourceBarrier.set_volatile_value(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        c.OutputResourceBarrier.set_volatile_value(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        c.FsrNonLinearSRGB.set_volatile_value(true);
    }
    ~BarrierSettings() {
        c.ColorResourceBarrier = color; c.MVResourceBarrier = motion;
        c.DepthResourceBarrier = depth; c.OutputResourceBarrier = output; c.FsrNonLinearSRGB = srgb;
    }
};
}

bool Enabled() { return Config::Instance()->ReShadeSREnabled.value_or_default(); }
void DisplaySize(UINT& width, UINT& height) {
    if (State::Instance().activeFgInput != FGInput::ReShade || !Enabled()) return;
    const auto w = Config::Instance()->ReShadeSRWidth.value_or_default();
    const auto h = Config::Instance()->ReShadeSRHeight.value_or_default();
    if (w >= 64 && h >= 64 && w <= 16384 && h <= 16384) { width = w; height = h; }
}
void Reset() { Retire(); }
void Submitted(ID3D12Fence* fence, UINT64 value) {
    if (current) { current->fence = fence; current->value = value; }
}

bool Record(ID3D12GraphicsCommandList* cmd, ID3D12Resource* source, ID3D12Resource* destination,
            ID3D12Resource* motion, ID3D12Resource* depth, ID3D12Resource* hudless,
            const OptiBridgeFrame* frame, bool reset, UINT slot) {
    Collect();
    if (!cmd || !source || !destination) return false;
    auto input = source->GetDesc(), output = destination->GetDesc();
    if (!Enabled() && input.Width == output.Width && input.Height == output.Height) return false;
    auto& state = State::Instance(); auto& cfg = *Config::Instance();
    ID3D12Device* device = nullptr;
    if (FAILED(destination->GetDevice(IID_PPV_ARGS(&device)))) return false;
    device->Release(); // resources own their device throughout this call
    const UINT depthFlags = frame ? frame->flags & (OPTI_BRIDGE_REVERSED_DEPTH | OPTI_BRIDGE_INFINITE_DEPTH) : 0;
    bool changed = current && current->feature &&
                   (state.changeBackend[current->feature->Handle()->Id] || state.newBackend != Upscaler::Reset);
    if (current && (current->device != device || current->width != output.Width || current->height != output.Height ||
        current->sourceWidth != input.Width || current->sourceHeight != input.Height || changed ||
        current->sourceFormat != input.Format || current->outputFormat != output.Format ||
        (frame && current->feature && current->flags != depthFlags))) {
        Retire(); reset = true;
    }
    if (!current) {
        current = std::make_unique<Context>();
        current->device = device; current->width = UINT(output.Width); current->height = output.Height;
        current->sourceWidth = UINT(input.Width); current->sourceHeight = input.Height;
        current->sourceFormat = input.Format; current->outputFormat = output.Format;
        D3D12_HEAP_PROPERTIES heap {}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        output.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &output,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&current->output)))) {
            current.reset(); return false;
        }
        LOG_INFO("ReShade SR presentation {}x{} -> {}x{}", input.Width, input.Height, output.Width, output.Height);
    }
    auto& c = *current;
    const bool useScene = hudless != nullptr;
    bool temporal = false;
    Transition(cmd, source, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (hudless) Transition(cmd, hudless, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (frame && motion && depth && Enabled() && state.activeFgInput == FGInput::ReShade &&
        opti_bridge::temporal_extents(UINT(input.Width), input.Height, UINT(output.Width), output.Height) &&
        (!state.currentFeature || state.currentFeature == c.feature.get())) {
        NVNGX_Parameters p(API::DX12, false);
        int flags = NVSDK_NGX_DLSS_Feature_Flags_MVLowRes | NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
        if (depthFlags & OPTI_BRIDGE_REVERSED_DEPTH) flags |= NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;
        p.Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, flags);
        p.Set(NVSDK_NGX_Parameter_Width, UINT(input.Width)); p.Set(NVSDK_NGX_Parameter_Height, input.Height);
        p.Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, UINT(input.Width));
        p.Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, input.Height);
        p.Set(NVSDK_NGX_Parameter_OutWidth, UINT(output.Width)); p.Set(NVSDK_NGX_Parameter_OutHeight, output.Height);
        p.Set(NVSDK_NGX_Parameter_PerfQualityValue, int(NVSDK_NGX_PerfQuality_Value_MaxQuality));
        p.Set(NVSDK_NGX_Parameter_Color, useScene ? hudless : source);
        p.Set(NVSDK_NGX_Parameter_MotionVectors, motion); p.Set(NVSDK_NGX_Parameter_Depth, depth);
        p.Set(NVSDK_NGX_Parameter_Output, c.output.Get());
        p.Set(NVSDK_NGX_Parameter_ExposureTexture, static_cast<ID3D12Resource*>(nullptr));
        p.Set(NVSDK_NGX_Parameter_Jitter_Offset_X, 0.0f); p.Set(NVSDK_NGX_Parameter_Jitter_Offset_Y, 0.0f);
        p.Set(NVSDK_NGX_Parameter_MV_Scale_X, 1.0f); p.Set(NVSDK_NGX_Parameter_MV_Scale_Y, 1.0f);
        p.Set(NVSDK_NGX_Parameter_Sharpness, 0.0f);
        p.Set(NVSDK_NGX_Parameter_Reset, UINT(reset || c.gap || c.hudless != useScene));
        p.Set(NVSDK_NGX_Parameter_FrameTimeDeltaInMsec, frame->delta_ms);
        p.Set("FSR.frameTimeDelta", frame->delta_ms);
        float nearPlane = frame->camera_near;
        float farPlane = (depthFlags & OPTI_BRIDGE_INFINITE_DEPTH) ? FLT_MAX : frame->camera_far;
        if (depthFlags & OPTI_BRIDGE_REVERSED_DEPTH) std::swap(nearPlane, farPlane);
        p.Set("FSR.cameraNear", nearPlane); p.Set("FSR.cameraFar", farPlane);
        p.Set("FSR.cameraFovAngleVertical", frame->vertical_fov_radians);
        p.Set("FSR.viewSpaceToMetersFactor", 1.0f);
        p.Set(NVSDK_NGX_Parameter_DLSS_Pre_Exposure, 1.0f);
        p.Set("FSR.reactive", static_cast<ID3D12Resource*>(nullptr));
        p.Set("FSR.transparencyAndComposition", static_cast<ID3D12Resource*>(nullptr));
        BarrierSettings barriers;
        if (!c.feature) {
            auto backend = state.newBackend == Upscaler::Reset ? cfg.Dx12Upscaler.value_or(Upscaler::FFX) : state.newBackend;
            // These are AMD-capable providers. Never select an NVIDIA fallback implicitly.
            if (backend != Upscaler::FFX && backend != Upscaler::FSR21 && backend != Upscaler::FSR22 && backend != Upscaler::XeSS)
                backend = Upscaler::FFX;
            if (FeatureProvider_Dx12::GetFeature(backend, IFeature::GetNextHandleId(), &p, &c.feature) &&
                c.feature->Init(device, cmd, &p)) {
                c.flags = depthFlags; c.ffxIndex = cfg.FfxUpscalerIndex.value_or_default();
                c.custom = cfg.CustomUpscaler.value_or("");
                state.currentFeature = c.feature.get();
                state.changeBackend[c.feature->Handle()->Id] = false; state.newBackend = Upscaler::Reset;
                LOG_INFO("ReShade SR initialized {} {}x{} -> {}x{}; estimated motion, no engine jitter",
                         c.feature->Name(), input.Width, input.Height, output.Width, output.Height);
            } else {
                // Keep a failed object alive until its recorded initialization work completes.
                LOG_WARN("ReShade SR initialization failed; spatial fallback until another provider is selected");
            }
            state.newBackend = Upscaler::Reset;
        }
        if (c.feature && c.feature->IsInited()) {
            Transition(cmd, motion, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            Transition(cmd, depth, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            temporal = c.feature->Evaluate(cmd, &p);
            Transition(cmd, depth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
            Transition(cmd, motion, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
        }
    }
    if (!temporal) {
        if (!Spatial(c, cmd, source, nullptr, slot, false)) return false;
        c.gap = true; ++c.fallbacks;
    } else {
        c.gap = false; c.hudless = useScene; ++c.frames;
        if (useScene) {
            if (auto* fg = state.currentFG; fg && fg->IsActive() && cfg.IsFGEnabled()) {
                OwnedLockGuard fgLock(fg->Mutex, 4);
                Dx12Resource r {}; r.type = FG_ResourceType::HudlessColor;
                r.resource = c.output.Get(); r.cmdList = cmd;
                r.width = c.width; r.height = c.height;
                r.state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS; r.validity = FG_ResourceValidity::ValidNow;
                fg->SetResource(&r);
            }
            if (!Spatial(c, cmd, source, hudless, slot, true)) return false;
        }
        if (c.frames == 1 || c.frames % 300 == 0)
            LOG_INFO("ReShade SR reconstructed {} real frames, {} spatial fallbacks", c.frames, c.fallbacks);
    }
    if (hudless) Transition(cmd, hudless, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    Transition(cmd, source, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    Transition(cmd, c.output.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    Transition(cmd, destination, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
    cmd->CopyResource(destination, c.output.Get());
    Transition(cmd, destination, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
    Transition(cmd, c.output.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    return true;
}
}
