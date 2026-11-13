// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <d3d12.h>
#include "opti_reshade_bridge.h"
namespace ReShadeSR {
bool Enabled();
void DisplaySize(UINT& width, UINT& height);
bool Record(ID3D12GraphicsCommandList* cmd, ID3D12Resource* source, ID3D12Resource* destination,
            ID3D12Resource* motion, ID3D12Resource* depth, ID3D12Resource* hudless,
            const OptiBridgeFrame* frame, bool reset, UINT slot);
void Submitted(ID3D12Fence* fence, UINT64 value);
void Reset();
}
