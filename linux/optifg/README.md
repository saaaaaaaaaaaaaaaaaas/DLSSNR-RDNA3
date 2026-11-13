# Experimental ReShade to OptiFG bridge

Target: Linux/Proton, DX11 and DX10 through DXVK, 64-bit, SDR, FSR3 FG 2x.
This is an opt-in development build. It does not change the released 0.0.9 package.
Game compatibility and interpolation quality need GPU capture validation.

## Build

The ABI header is MIT; the OptiScaler receiver is GPL-3.0-or-later, like its host.
The consumer is built from OptiScaler commit
`e7e3872d410f704479d94d318d68d163b6b58947` plus `optiscaler.patch` and `receiver/`.
`prepare_optiscaler.py` checks the commit and refuses partially applied patches.
The `Experimental ReShade OptiFG` workflow builds the patched DLL with MSVC and
the existing ReShade add-on with MinGW. Download `optifg-consumer-x86_64` from the
workflow run for the **same branch commit**. The normal bundled OptiScaler does
not export the bridge and cannot receive these inputs.

Locally, after `bash fetch_deps.sh`:

```sh
bash linux/test/check_optifg.sh
NR_GPU=rdna3 NR_PROGRAMS_ONLY=1 bash linux/build/build_package.sh
bash linux/build/build_optifg_probe.sh
```

To rebuild the consumer on Windows, clone OptiScaler at the pin with its
submodules, run `python linux/optifg/prepare_optiscaler.py PATH_TO_OPTISCALER`, then
`msbuild PATH_TO_OPTISCALER/OptiScaler.sln /m /p:Configuration=Release /p:Platform=x64`.
The binary is `x64/Release/a/OptiScaler.dll`. Keep the upstream licence and the
patch/receiver/public headers with any experimental binary you distribute.

## Standalone qualification

Set `NR_PROTON` to a local Proton directory, and place the consumer artifact in
`artifacts/optifg-consumer/`. All prefix changes and copies stay in this project.

```sh
NR_PROTON='/path/to/Proton' bash linux/optifg/run_probe.sh baseline 120
NR_PROTON='/path/to/Proton' bash linux/optifg/run_probe.sh direct 600
NR_PROTON='/path/to/Proton' bash linux/optifg/run_probe.sh reshade 1800
NR_PROTON='/path/to/Proton' bash linux/optifg/run_probe.sh baseline-dx10 120
NR_PROTON='/path/to/Proton' bash linux/optifg/run_probe.sh direct-dx10 600
NR_PROTON='/path/to/Proton' bash linux/optifg/run_probe.sh reshade-dx10 1800
```

`baseline` renders through DXVK. `direct` sends analytically known pixel motion
and device depth through the bridge. `reshade` uses the actual ReShade add-on,
VORT and DLSS5_Feed, with NR disabled. The probe has a real hardware depth buffer
so Generic Depth can select it. It uses normal Z, not reversed Z. Shader loading
can take longer than a short probe; inspect `ReShade.log` and use a longer run.

The orange rectangle moves over a stationary grid. Source MV are current to
previous in pixels; foreground depth is 0.3, background 0.95. The `-dx10` variants
use actual D3D10 device/draw/resource entry points and shader model 4.
Debug view is on.
Bridge counters count captures and consumed **real** presents, not generated
frames. Record the output to check intermediate frames, motion direction,
occlusion edges, reset behavior and pacing before claiming successful FG.

## Game setup

Use the matching patched OptiScaler and producer. ReShade must wrap the
game-facing DX11 swapchain, before OptiScaler copies its backbuffer into the
DX12 FG presenter. The probe loads `OptiScaler.dll` explicitly and lets it load
`ReShade64.dll` through `[Plugins] LoadReShade=true`, after installing its hooks.
Loading ReShade first traps its runtime on the hidden internal swapchain, which
Linux does not present. A game needs its normal OptiScaler loading method (e.g. an imported
proxy DLL); filename choice alone does not force an unreferenced DLL to load.
Do not put two different DLLs under the same proxy filename.

The configuration examples here are for the probe. Set actual game near/far/FOV
in `optifg-bridge.ini`, preserve the game's ReShade depth conventions and select
its depth buffer in Generic Depth. Camera parameters have no universal defaults.
For infinite depth, set `InfiniteDepth=1` and retain a finite `CameraFar` greater
than `CameraNear` in the descriptor. F7 resets temporal history after a cut.

The guide publisher is independent of NR. Keep `[DlssNr] Enabled=0` and
`[RenoDX] Enabled=0` for the first test. VORT or Lumenite must be active before
DLSS5_Feed. Missing depth, inactive providers and incompatible guide sizes stop
publication; the consumer clears stale input on the next game present.
Logarithmic, mirrored or multiplied depth and HDR are rejected in this version.

Optional HUDless input reuses the existing ReShade capture with `[HudFix] Mode=3`
or `1`, set **before starting the process**. Only a confirmed same-frame capture
is published, initially with NR off. The final image is never labelled HUDless.
Automatic capture is game-dependent. With NR on, the pre-NR capture is not a
valid match for post-NR color and is deliberately omitted.

## Synchronization

The source lends COM objects for one call. OptiScaler copies the guides through
its DX11/DX12 shared cache and makes private FSRFG `ValidNow` copies. A D3D12
fence protects every source ring slot; a busy slot drops FG for that real frame.
There is no CPU image readback or reverse D3D11 GPU wait in this bridge. The
existing presenter/backend may perform its own allocator/fence waits. A reverse
GPU wait can deadlock on Proton when both APIs share a physical Vulkan queue.

Resize, reload, missing captures and manual reset restart temporal history.
Stream/device/window identity prevents another runtime from taking the stream.
Native upscaler inputs stay separate and do not feed FG while ReShade is selected.
The acceptance matrix and observed results are in `docs/RESHADE-OPTIFG.md`.
