// SPDX-License-Identifier: MIT
// Standalone process: never creates a probe device inside an existing game.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include "../optifg/include/opti_reshade_bridge.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <stdexcept>

template<class T> struct Com {
    T* p = nullptr;
    ~Com() { if (p) p->Release(); }
    T* operator->() const { return p; }
    T** put() { return &p; }
};
static void check(HRESULT result, const char* operation) {
    if (FAILED(result)) {
        std::fprintf(stderr, "%s: HRESULT %08lx\n", operation, (unsigned long)result);
        throw std::runtime_error(operation);
    }
}
static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static const char* shader = R"(
cbuffer Frame : register(b0) { float x; float previousX; float width; float height; };
float4 VS(uint id : SV_VertexID) : SV_Position {
    return float4(id == 2 ? 3 : -1, id == 1 ? 3 : -1, 0.5, 1);
}
struct Output {
    float4 color : SV_Target0;
    float2 motion : SV_Target1;
    float depth : SV_Target2;
    float z : SV_Depth;
};
Output PS(float4 position : SV_Position) {
    float2 uv = position.xy / float2(width, height);
    bool object = abs(uv.x - x) < 0.08 && abs(uv.y - 0.5) < 0.18;
    float grid = fmod(floor(uv.x * 32) + floor(uv.y * 18), 2);
    Output o;
    o.color = object ? float4(0.9, 0.25, 0.03, 1) : float4(0.04 + grid * 0.12, 0.12 + grid * 0.12, 0.2, 1);
    o.motion = object ? float2((previousX - x) * width, 0) : float2(0, 0);
    o.depth = o.z = object ? 0.3 : 0.95;
    return o;
}
)";

int main(int argc, char** argv) {
    try {
        bool baseline = false, reshade = false;
        unsigned frames = 600;
        for (int i = 1; i < argc; ++i) {
            if (!std::strcmp(argv[i], "--baseline")) baseline = true;
            else if (!std::strcmp(argv[i], "--reshade")) reshade = true;
            else if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) frames = unsigned(std::stoul(argv[++i]));
            else throw std::runtime_error("usage: optifg_probe.exe [--baseline | --reshade] [--frames N]");
        }
        const OptiReShadeBridge* api = nullptr;
        if (!baseline) {
            HMODULE module = LoadLibraryW(L"OptiScaler.dll");
            if (!module) throw std::runtime_error("Cannot load the patched OptiScaler.dll beside the probe");
            auto get = reinterpret_cast<OptiGetReShadeBridge>(GetProcAddress(module, OPTI_BRIDGE_EXPORT));
            if (!get || !(api = get(OPTI_BRIDGE_VERSION))) throw std::runtime_error("ReShade bridge export missing");
            Sleep(500); // OptiScaler initializes hooks on its worker after DllMain.
        }
        WNDCLASSW wc {}; wc.lpfnWndProc = windowProc; wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"OptiFGBridgeProbe";
        RegisterClassW(&wc);
        HWND hwnd = CreateWindowW(wc.lpszClassName, L"OptiFG bridge: analytic motion and depth", WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, 1280, 720, nullptr, nullptr, wc.hInstance, nullptr);
        if (!hwnd) throw std::runtime_error("CreateWindow failed");
        ShowWindow(hwnd, SW_SHOW);
        Com<ID3D11Device> device; Com<ID3D11DeviceContext> context;
        D3D_FEATURE_LEVEL level;
        check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                D3D11_SDK_VERSION, device.put(), &level, context.put()), "D3D11CreateDevice");
        Com<IDXGIDevice> dxgiDevice; Com<IDXGIAdapter> adapter; Com<IDXGIFactory> factory;
        check(device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(dxgiDevice.put())), "IDXGIDevice");
        check(dxgiDevice->GetAdapter(adapter.put()), "GetAdapter");
        check(adapter->GetParent(__uuidof(IDXGIFactory), reinterpret_cast<void**>(factory.put())), "GetFactory");
        DXGI_SWAP_CHAIN_DESC desc {};
        desc.BufferDesc.Width = 1280; desc.BufferDesc.Height = 720;
        desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1; desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2; desc.OutputWindow = hwnd; desc.Windowed = TRUE;
        desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        Com<IDXGISwapChain> swapchain;
        check(factory->CreateSwapChain(device.p, &desc, swapchain.put()), "CreateSwapChain");
        Com<ID3D11Texture2D> color;
        check(swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(color.put())), "GetBuffer");
        Com<ID3D11RenderTargetView> colorView;
        check(device->CreateRenderTargetView(color.p, nullptr, colorView.put()), "Color RTV");
        auto texture = [&](DXGI_FORMAT format, UINT bind, Com<ID3D11Texture2D>& out) {
            D3D11_TEXTURE2D_DESC d {}; d.Width = 1280; d.Height = 720;
            d.MipLevels = d.ArraySize = d.SampleDesc.Count = 1; d.Format = format; d.BindFlags = bind;
            check(device->CreateTexture2D(&d, nullptr, out.put()), "CreateTexture2D");
        };
        Com<ID3D11Texture2D> motion, depth, gameDepth;
        texture(DXGI_FORMAT_R16G16_FLOAT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, motion);
        texture(DXGI_FORMAT_R32_FLOAT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, depth);
        texture(DXGI_FORMAT_R32_TYPELESS, D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE, gameDepth);
        Com<ID3D11RenderTargetView> motionView, depthView;
        check(device->CreateRenderTargetView(motion.p, nullptr, motionView.put()), "Motion RTV");
        check(device->CreateRenderTargetView(depth.p, nullptr, depthView.put()), "Depth RTV");
        D3D11_DEPTH_STENCIL_VIEW_DESC dsv {}; dsv.Format = DXGI_FORMAT_D32_FLOAT;
        dsv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        Com<ID3D11DepthStencilView> gameDepthView;
        check(device->CreateDepthStencilView(gameDepth.p, &dsv, gameDepthView.put()), "Game depth DSV");
        Com<ID3D11DepthStencilState> depthState;
        D3D11_DEPTH_STENCIL_DESC ds {}; ds.DepthEnable = TRUE;
        ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; ds.DepthFunc = D3D11_COMPARISON_ALWAYS;
        check(device->CreateDepthStencilState(&ds, depthState.put()), "Depth state");
        D3D11_RASTERIZER_DESC rd {}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE;
        rd.DepthClipEnable = TRUE;
        Com<ID3D11RasterizerState> rasterizer;
        check(device->CreateRasterizerState(&rd, rasterizer.put()), "Rasterizer");
        Com<ID3DBlob> vsCode, psCode, error;
        check(D3DCompile(shader, std::strlen(shader), nullptr, nullptr, nullptr, "VS", "vs_5_0", 0, 0,
                        vsCode.put(), error.put()), "Compile VS");
        check(D3DCompile(shader, std::strlen(shader), nullptr, nullptr, nullptr, "PS", "ps_5_0", 0, 0,
                        psCode.put(), nullptr), "Compile PS");
        Com<ID3D11VertexShader> vs; Com<ID3D11PixelShader> ps;
        check(device->CreateVertexShader(vsCode->GetBufferPointer(), vsCode->GetBufferSize(), nullptr, vs.put()), "Create VS");
        check(device->CreatePixelShader(psCode->GetBufferPointer(), psCode->GetBufferSize(), nullptr, ps.put()), "Create PS");
        D3D11_BUFFER_DESC bd {}; bd.ByteWidth = 16; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        Com<ID3D11Buffer> constants;
        check(device->CreateBuffer(&bd, nullptr, constants.put()), "Constants");
        uint64_t stream = 0;
        if (api && !reshade) {
            const auto status = api->Register(device.p, uint64_t(uintptr_t(hwnd)), &stream);
            if (status != OPTI_BRIDGE_OK) {
                std::fprintf(stderr, "Register returned %u; inspect OptiScaler.log and the DX11/DX12 presenter\n", status);
                return 2;
            }
        }
        LARGE_INTEGER frequency, previous; QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&previous);
        unsigned accepted = 0, rejected = 0;
        for (unsigned frame = 1; frame <= frames; ++frame) {
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) { frames = frame - 1; break; }
                TranslateMessage(&msg); DispatchMessageW(&msg);
            }
            if (frame > frames) break;
            const auto position = [](unsigned n) { return 0.5f + 0.3f * std::sin(float(n) * 0.025f); };
            float values[] = {position(frame), position(frame - 1), 1280, 720};
            context->UpdateSubresource(constants.p, 0, nullptr, values, 0, 0);
            ID3D11RenderTargetView* views[] = {colorView.p, motionView.p, depthView.p};
            context->OMSetRenderTargets(3, views, gameDepthView.p);
            context->OMSetDepthStencilState(depthState.p, 0);
            context->RSSetState(rasterizer.p);
            D3D11_VIEWPORT viewport {0, 0, 1280, 720, 0, 1}; context->RSSetViewports(1, &viewport);
            context->ClearDepthStencilView(gameDepthView.p, D3D11_CLEAR_DEPTH, 1, 0);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context->VSSetShader(vs.p, nullptr, 0); context->PSSetShader(ps.p, nullptr, 0);
            context->PSSetConstantBuffers(0, 1, &constants.p);
            context->Draw(3, 0);
            context->OMSetRenderTargets(0, nullptr, nullptr);
            LARGE_INTEGER now; QueryPerformanceCounter(&now);
            if (stream) {
                OptiBridgeFrame input {};
                input.size = sizeof input; input.version = OPTI_BRIDGE_VERSION; input.frame_id = frame;
                input.color = color.p; input.motion = motion.p; input.depth = depth.p;
                input.width = 1280; input.height = 720; input.flags = frame == 1 ? OPTI_BRIDGE_RESET : 0;
                input.delta_ms = float(double(now.QuadPart - previous.QuadPart) * 1000 / frequency.QuadPart);
                if (input.delta_ms < 0.01f) input.delta_ms = 0.01f;
                input.camera_near = 0.1f; input.camera_far = 1000; input.vertical_fov_radians = 1.04719755f;
                const auto status = api->Submit(stream, &input);
                if (status == OPTI_BRIDGE_OK) ++accepted;
                else { ++rejected; if (rejected < 8) std::printf("frame %u rejected: %u\n", frame, status); }
            }
            previous = now;
            check(swapchain->Present(0, 0), "Present");
            Sleep(16);
        }
        if (stream) {
            OptiBridgeStats stats {sizeof stats, OPTI_BRIDGE_VERSION};
            api->GetStats(stream, &stats);
            std::printf("Captured %u, rejected %u, consumed real presents %llu.\n", accepted, rejected,
                        (unsigned long long)stats.presented);
            api->Unregister(stream);
        }
        std::printf("Probe completed %u real frames. Intermediate frame quality/pacing needs capture validation.\n", frames);
        return stream && !accepted ? 3 : 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what()); return 1;
    }
}
