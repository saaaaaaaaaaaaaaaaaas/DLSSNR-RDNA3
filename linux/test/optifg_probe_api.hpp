// SPDX-License-Identifier: MIT
#pragma once
#include <d3d10.h>
// The same scene exercises actual DX10 and DX11 device/context entry points.
namespace probe {
#ifdef OPTI_PROBE_DX10
using Device = ID3D10Device;
using Context = ID3D10Device;
using Texture = ID3D10Texture2D;
using RenderView = ID3D10RenderTargetView;
using DepthView = ID3D10DepthStencilView;
using DepthState = ID3D10DepthStencilState;
using RasterState = ID3D10RasterizerState;
using Buffer = ID3D10Buffer;
using VertexShader = ID3D10VertexShader;
using PixelShader = ID3D10PixelShader;
using TextureDesc = D3D10_TEXTURE2D_DESC;
using DepthViewDesc = D3D10_DEPTH_STENCIL_VIEW_DESC;
using DepthDesc = D3D10_DEPTH_STENCIL_DESC;
using RasterDesc = D3D10_RASTERIZER_DESC;
using BufferDesc = D3D10_BUFFER_DESC;
using Viewport = D3D10_VIEWPORT;
constexpr auto depthDimension = D3D10_DSV_DIMENSION_TEXTURE2D;
constexpr auto depthWrite = D3D10_DEPTH_WRITE_MASK_ALL;
constexpr auto depthAlways = D3D10_COMPARISON_ALWAYS;
constexpr auto fillSolid = D3D10_FILL_SOLID;
constexpr auto cullNone = D3D10_CULL_NONE;
constexpr auto topology = D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
constexpr const char *vsTarget = "vs_4_0", *psTarget = "ps_4_0";
inline HRESULT createDevice(Com<Device>& device, Com<Context>& context) {
    const auto result = D3D10CreateDevice(nullptr, D3D10_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                         D3D10_SDK_VERSION, device.put());
    if (SUCCEEDED(result)) { context.p = device.p; context.p->AddRef(); }
    return result;
}
inline HRESULT createVS(Device* device, ID3DBlob* code, VertexShader** out) {
    return device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), out);
}
inline HRESULT createPS(Device* device, ID3DBlob* code, PixelShader** out) {
    return device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), out);
}
inline void bindShaders(Context* context, VertexShader* vs, PixelShader* ps) {
    context->VSSetShader(vs); context->PSSetShader(ps);
}
#else
using Device = ID3D11Device;
using Context = ID3D11DeviceContext;
using Texture = ID3D11Texture2D;
using RenderView = ID3D11RenderTargetView;
using DepthView = ID3D11DepthStencilView;
using DepthState = ID3D11DepthStencilState;
using RasterState = ID3D11RasterizerState;
using Buffer = ID3D11Buffer;
using VertexShader = ID3D11VertexShader;
using PixelShader = ID3D11PixelShader;
using TextureDesc = D3D11_TEXTURE2D_DESC;
using DepthViewDesc = D3D11_DEPTH_STENCIL_VIEW_DESC;
using DepthDesc = D3D11_DEPTH_STENCIL_DESC;
using RasterDesc = D3D11_RASTERIZER_DESC;
using BufferDesc = D3D11_BUFFER_DESC;
using Viewport = D3D11_VIEWPORT;
constexpr auto depthDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
constexpr auto depthWrite = D3D11_DEPTH_WRITE_MASK_ALL;
constexpr auto depthAlways = D3D11_COMPARISON_ALWAYS;
constexpr auto fillSolid = D3D11_FILL_SOLID;
constexpr auto cullNone = D3D11_CULL_NONE;
constexpr auto topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
constexpr const char *vsTarget = "vs_5_0", *psTarget = "ps_5_0";
inline HRESULT createDevice(Com<Device>& device, Com<Context>& context) {
    return D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                            D3D11_SDK_VERSION, device.put(), nullptr, context.put());
}
inline HRESULT createVS(Device* device, ID3DBlob* code, VertexShader** out) {
    return device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, out);
}
inline HRESULT createPS(Device* device, ID3DBlob* code, PixelShader** out) {
    return device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, out);
}
inline void bindShaders(Context* context, VertexShader* vs, PixelShader* ps) {
    context->VSSetShader(vs, nullptr, 0); context->PSSetShader(ps, nullptr, 0);
}
#endif
// Bind/clear flags are UINT bitfields shared by the two API versions.
constexpr UINT bindRender = 0x20, bindShader = 0x8, bindDepth = 0x40, bindConstant = 0x4;
constexpr UINT clearDepth = 0x1;
}
