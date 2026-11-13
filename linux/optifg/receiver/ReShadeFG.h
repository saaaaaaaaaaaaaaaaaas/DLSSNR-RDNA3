// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <d3d12.h>
struct ID3D11Device;
// Called only for the game-facing DX11 swapchain, before any generated presents.
void ReShadeFG_BeforePresent(ID3D11Device* device);
void ReShadeFG_Destroy(ID3D11Device* device);

void ReShadeFG_DisplaySize(UINT& width, UINT& height);
bool ReShadeFG_RecordPresentation(ID3D12GraphicsCommandList* cmd, ID3D12Resource* source,
                                  ID3D12Resource* destination, UINT slot);
void ReShadeFG_PresentationSubmitted(ID3D12CommandQueue* queue);

void ReShadeFG_Resize(ID3D11Device* device);
