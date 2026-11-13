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

The workflow also produces `optifg-developer-bundle-x86_64`: both DLLs, native
DX11/DX10 probes, hashes, source/build instructions and licences. It contains no
models, LumeniteFX or third-party runtimes. To assemble it locally from the two
CI artifacts for the current clean commit:

```sh
python3 linux/optifg/package_dev.py --consumer artifacts/consumer \
  --producer artifacts/producer --output artifacts/DLSSNR-RDNA3-OptiFG-dev.zip
```

The packager checks both artifacts' `source.txt` against the current commit.

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
LumeniteFX Kernel and DLSS5_Feed, with NR disabled. Set `NR_OPTIFG_MOTION=vort`
to compare VORT. LumeniteFX is downloaded directly from its author's pinned
GitHub archive through the existing installer's interface/license checks; its
files are not included in CI artifacts or redistributed bundles.
The probe has a real hardware depth buffer
so Generic Depth can select it. It uses normal Z, not reversed Z. Shader loading
can take longer than a short probe; inspect `ReShade.log` and use a longer run.

The orange rectangle moves over a stationary grid. Source MV are current to
previous in pixels; foreground depth is 0.3, background 0.95. The `-dx10` variants
use actual D3D10 device/draw/resource entry points and shader model 4.
Debug view is on.
Set `NR_OPTIFG_10BIT=1` to test an SDR R10G10B10A2 swapchain, like Tekken 7.
The producer tags SDR explicitly; the bit depth alone does not establish HDR.
For DX10, ReShade 6.8's public color-space query returns unknown. The publisher
reads ReShade's per-swapchain record of successful `SetColorSpace1` calls; a
legacy fixed-point DXGI surface with no recorded change uses the SDR default.
Explicit non-SDR metadata still stops publication. The 10-bit probe sets SDR
through DXGI explicitly.
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

Tekken 7 was tested with OptiScaler named `winmm.dll`, ReShade named
`ReShade64.dll`, `[Plugins] LoadReShade=true` in `OptiScaler.ini`, and
`WINEDLLOVERRIDES="winmm=n,b" %command%` as the Steam launch option. Using
OptiScaler as `dxgi.dll` in this game bound ReShade to an internal swapchain.
Its SDR R10G10B10A2 backbuffer requires the matching SDR10-capable producer and
consumer. LumeniteFX Kernel runs before DLSS5_Feed with provider 3 and reversed
depth enabled. See the acceptance report for the tested build and limitations.

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

## Additional upscaling before FG

The patched consumer can reconstruct a real low-resolution game backbuffer into
a separate high-resolution DX12 presenter before generating intermediate frames.
For 1280x720 input and 1920x1080 output, add to `OptiScaler.ini`:

```ini
[ReShadeSR]
Enabled=true
OutputWidth=1920
OutputHeight=1080
[Upscalers]
Dx12Upscaler=ffx
[Menu]
OverlayMenu=true
ShortcutKey=45
```

Restart after changing output dimensions or enabling SR. Set the actual game's
render resolution to 1280x720. This pass does not lower the engine's render
resolution by itself. The output presenter is 1920x1080; the window/desktop must
also display that resolution to see the full result. Window scaling is separate
from the render-buffer size. Source and target aspect ratios must match for
temporal reconstruction. The default is SR off.

Press Insert and use OptiScaler's Upscaling tab to select the DX12 provider.
The first target is FSR 3.1; FSR 2.1/2.2 and XeSS use their existing providers.
XeSS additionally needs the matching `OptiScaler/libxess.dll` and licence from
the pinned nightly. Keep FG input `OptiFG (ReShade)` and output `FSR FG`; disable
FG with its Enabled toggle to test reconstruction alone.

Depth/MV remain at the actual input resolution. FSR SR takes pixel MV with scale
1; FSR FG takes their normalized scale 1/input extent. No engine jitter or
reactive mask is invented. This is post-render reconstruction using estimated
flow; quality must be assessed separately from engine-integrated temporal SR.
Menus/loading without guides use a spatial pass and reset temporal history on
recovery. Confirmed HUDless input is reconstructed before adding the same-frame
HUD color delta; the pre-HUD display-resolution image is supplied to FG. This
color delta is an approximation, not recovered HUD alpha.

Qualification commands (after downloading the matching consumer):

```sh
NR_OPTIFG_SR=1 NR_OPTIFG_HUD=1 NR_OPTIFG_GUIDE_GAP=300 NR_OPTIFG_RECREATE=500 \
  NR_PROTON='/path/to/Proton' bash linux/optifg/run_probe.sh direct 1200
NR_OPTIFG_SR=1 NR_OPTIFG_10BIT=1 NR_OPTIFG_HUD=1 \
  NR_PROTON='/path/to/Proton' bash linux/optifg/run_probe.sh reshade 2400
```

Set `NR_OPTIFG_UPSCALER=fsr21`, `fsr22` or `xess` to qualify another provider.

## Synchronization details

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
