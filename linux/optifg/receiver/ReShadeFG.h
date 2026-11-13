// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
struct ID3D11Device;
// Called only for the game-facing DX11 swapchain, before any generated presents.
void ReShadeFG_BeforePresent(ID3D11Device* device);
void ReShadeFG_Destroy(ID3D11Device* device);
