# ReShade inputs for OptiFG on Linux

Implementation branch: `feature/reshade-optifg`, based on `c518f65` (0.0.9).
OptiScaler source baseline: `e7e3872d410f704479d94d318d68d163b6b58947`.
All generated files and dependency checkouts belong to this worktree.

The producer is the existing Linux ReShade add-on. The consumer is a patched
OptiScaler with an explicit ReShade input. It reuses OptiScaler's DX11/DX12
shared resources and DX12 FG presenter. Neither an engine upscaler nor NR is
required. Native Windows and 32-bit FG are outside the initial target.

## Feature and acceptance matrix

| Area | Required behavior | Evidence |
| --- | --- | --- |
| Disabled | Existing NR and upscaler inputs behave as before | RDNA3 CPU/static regression and 32-bit add-on compilation pass; native inputs remain separate |
| ABI | Versioned C ABI; reject incompatible/truncated descriptors | C, GCC, MinGW and MSVC builds; contract tests pass |
| Capture | Publish after DLSS5_Feed, once per real frame, even with NR off | Full ReShade/VORT DX11 and DX10 probes pass; Tekken 7 with LumeniteFX accepts over 17,000 frames, NR off |
| Guides | RG16F current-to-previous pixel MV and raw R32F depth, same frame/extent | Analytic guides and ReShade guides accepted on Z1 Extreme |
| Camera | Explicit near/far/vertical FOV; no unlabelled camera estimates | Manual provenance flag and validation; real-game calibration pending |
| Transport | GPU copies and GPU fence dependencies; no image readback in the bridge | Per-slot copy fence, skip busy slots; long GPU probe runs pass |
| Ownership | Inputs borrowed for the call only; receiver copies before reuse | Shared input cache plus private FSRFG copies; strict COM ownership with ReShade unwrapping |
| Presentation | Actual DX11 game swapchain connected to DX12 FG presenter | Probe verified; Tekken 7 requires imported winmm proxy plus late ReShade64 loading |
| Invalid input | Missing/stale/duplicate data never reuses previous guides as a new frame | CPU timeline tests pass; missing capture explicitly deactivates that FG present |
| Lifecycle | Reload/resize/gap/scene cut reset history; stream unregisters on teardown | CPU reset/resize/projection tests and normal GPU probe teardown pass; in-game resize/alt-tab pending |
| HUD | Optional same-frame HUDless color; no claim that final color is HUDless | Tekken 7 confirms capture before HUD with Mode=3 and over 8,000 accepted frames; visual quality across scenes remains under evaluation |
| DX10 | DXVK D3D10 objects queried for their D3D11 interfaces | Actual D3D10 device/draw/resources and full ReShade path pass on GPU |
| FG | Start with FSR3 FG 2x, SDR, x86_64; verify actual intermediate frames | 589 successful one-frame generation callbacks; probe output recorded and inspected |
| NR | NR may run on real frames before FG; generated presents never feed history | Pending target GPU check |
| Packaging | Opt-in experimental bundle with matching producer/consumer/source patch | CI builds a developer ZIP with commit checks, hashes, source archive and licences; no release tag |

## Validation sequence

1. Build the producer with MinGW and the consumer with MSVC against the pinned
   source. Check ABI sizes on both sides and compile the public header as C.
2. Exercise descriptor validation and frame identity/reset behavior in CPU tests.
3. Use a standalone DX11 test application with known depth, a moving rectangle,
   and a changing frame marker. Validate the normal presenter before enabling FG.
4. Test on Proton/RADV with FG off, then FSR3 FG 2x, NR off and HUD off.
   Inspect real/intermediate frames, pacing and latency rather than the FPS label.
5. Test two DX11 titles, then DX10, resizing, alt-tab, loading, pause, missing
   depth/provider, effect reload, FG toggles, HUD capture and optional NR.

VORT/Lumenite produce estimated optical flow. They cannot supply hidden geometry,
true object motion or universal projection parameters. Their per-pixel rejection
mask is not an FSR reactive mask. Bridge correctness and interpolation quality
must be qualified separately. Do not advertise game compatibility from compilation.

## Delivery

Keep the public ABI and receiver overlay in `linux/optifg/`; keep the upstream
OptiScaler patch and preparation/build instructions alongside it. No downloaded
NVIDIA DLLs or model data belong in the bundle. The normal 0.0.9 release stays
independent of this opt-in experiment.

## Additional upscaling pass: acceptance matrix

Requested target: render 1280x720, reconstruct 1920x1080, then run FG. The
existing ABI continues to describe the actual source color/depth/MV extent.
Output dimensions belong to the consumer/presenter, not a resized declaration
of the source textures. Existing FG-only installations remain opt-in unchanged.

| Area | Required behavior | Evidence |
| --- | --- | --- |
| Real scaling | Separate low-resolution game buffers and high-resolution DX12 presenter; no downsample-then-upsample shortcut | GPU probes log 1280x720 -> 1920x1080; CPU geometry contract passes |
| Upscaler | Reuse OptiScaler's DX12 providers and menu selection, initially FSR 3.1 on RDNA3 | FFX 4.1.2 -> 3.1.6 switched live; FSR 2.2.1 and XeSS pass separate GPU runs |
| Ordering | ReShade guides -> SR on real frames -> FG; generated frames never enter SR history | SR records in the real source copy list before FG presentation; counters continue with FG disabled |
| Guides | Preserve render-resolution depth/MV and their pixel scale for SR/FG | Analytic and Lumenite SDR10 runs consume source-sized guides; SR scale 1, FG scale 1/input extent |
| Missing guides | Spatial fallback in menus/loading, reset SR/FG on recovery; no stale temporal input | 90-frame deliberate guide gap uses spatial fallback, then temporal reconstruction resumes |
| HUD | Same-frame HUDless SR when available, preserve HUD separately, publish display-resolution HUDless to FG | Analytic white HUD bar and full Lumenite pre-HUD capture inspected; all-scene quality pending |
| Lifetime | Present allocator/fences protect SR output, inputs and provider changes; no reverse GPU wait | 9,000-frame run survives buffer recreation and guide gap; live FFX version change and normal teardown pass |
| Controls | Explicit output size, off by default, provider selection through OptiScaler; native inputs keep their ownership | Insert menu verified, FSR 3.1.6 selection saved, End toggles FG while SR continues; native owner guarded in code |
| Qualification | 720p -> 1080p analytic probe, ReShade/Lumenite probe, reset/change/resize, and Tekken 7 | Standalone probes and Tekken 7 training pass; native 1080p screen presentation remains pending |

ReShade optical flow is estimated and no engine jitter is injected or recovered.
This post-render temporal reconstruction is not equivalent to an engine-integrated
upscaler with pre-TAA color, true motion, jitter and engine-provided reactive masks.
Window/desktop presentation size and render-buffer size must be verified separately.

SR evidence is under `artifacts/optifg-evidence/`: `sr-direct-gap-hud-7a9bbfb/`
(810 accepted real frames, 90 fallback frames), `sr-lumenite-sdr10-7a9bbfb/`
(at least 2,400 captured frames), `sr-recreate-9666cf7/` (8,910 accepted frames),
`sr-fsr22-9666cf7/` (1,200 accepted frames), and `sr-xess-9666cf7/` (1,800).
The default FFX provider in those early runs was 4.1.2, despite its generic
`FSR 3.X` name in older logs. `sr-ui-switch-086c74d/` records 28,805 accepted
frames without rejection, a live change to FSR 3.1.6 and FG off/on. Numeric SR
version logging and an explicit FG reset on SR history changes follow in
`65b0a2b`. The SR-disabled regression retains 589 accepted frames and 11 warmup
frames out of 600. These are functional transport/reconstruction checks, not
image-quality or performance comparisons between providers.

The later `65b0a2b` FSR 3.1.6 probe completes 1,200 real frames: 1,110 accepted
and consumed, zero rejected, a 90-frame guide gap and buffer recreation at frame
500. Numeric version logs confirm SR 3.1.6 before and after recreation.

Tekken 7 with matching `65b0a2b` producer/consumer reaches the offline training
arena with LumeniteFX and HUD capture Mode=3. Logs confirm source 1280x720,
presenter 1920x1080, SR 3.1.6 initialization and a live change to SR 4.1.2.
At least 3,000 captures are accepted without rejection; later SR counters pass
4,500 frames after the provider change. The training image and OptiScaler menu
were inspected. Logs/settings/window capture are saved in
`artifacts/optifg-evidence/tekken7-sr-training-65b0a2b/`.

This game test uses a 1280x720 window: the 1920x1080 presenter is scaled back to
that client area. It proves the reconstruction/FG chain, not native 1080p screen
output. An initial `-fullscreen` launch instead selected a 4300x1800 source
(the desktop extent), so the geometry guard correctly retained spatial fallback.
Alt+Enter did not change the training window in this setup. Fullscreen output
and a different actual render resolution need further game-specific qualification;
do not describe that startup fallback as temporal 720p-to-1080p reconstruction.

## NR and XeSS MFG extension: acceptance matrix

Keep NR on real frames before SR/FG. The requested MFG test plugin is the
owner-provided `MFGUnlocker.asi` (V1.6.1 build path, SHA-256
`24e3e6a8c07639fd9cbd79274e4eb21cc65a13f0453e1157720620c3d6286b33`).
Load it only through OptiScaler's ASI plugin loader; do not redistribute it.

| Area | Required behavior | Evidence |
| --- | --- | --- |
| NR ordering | Process each real source frame once, then SR and FG; generated frames do not enter NR history | NR/SR/FSR FG probe completes 2,719 real frames; over 1,356 enhanced, NR off/on resets history |
| NR/HUD | Never label pre-NR color as post-NR HUDless; reset history on NR state changes | Pending |
| XeFG routing | Permit explicit XeFG output with ReShade input; retain FSR FG behavior | Pending |
| XeFG guides | Pixel MV scale 1, matching depth/MV extents, valid view/projection and explicit manual camera provenance | CPU finite/reversed/infinite projection and MV-unit checks pass; initial SDK probe reports 276 generated frames from 300 real presents |
| XeFG lifetime | Tag same-frame resources on executed command lists and retain them through SDK copies | Pending |
| MFG plugin | Load the supplied file as an ASI, query supported multiplier, validate actual generated/presented frames | Pending |
| Controls | Expose existing XeFG multiplier controls; test reset, missing guides and FG off/on | Pending |
| Regression | NR disabled, FSR FG and SR-only retain qualified behavior | Pending |
| Game | Tekken 7 training with NR, then XeFG/MFG; distinguish loading and presenter/screen extents | Pending |

Contracts are checked against Intel's [XeSS-FG developer guide](https://github.com/intel/xess/blob/main/doc/xess_fg_developer_guide_english.md).
The plugin uses the author's [OptiScaler ASI installation route](https://github.com/dashdogy/MFG-Unlocker/blob/main/docs/optiscaler.md).
Its supplied V1.6.1 binary is tested as supplied; newer documentation does not
establish that this older binary supports every newer feature.

Initial XeFG qualification exposed a menu re-entry: the SR framebuffer-scale
calculation queried the XeFG proxy's `GetDesc` from the SDK's native Present
callback while its lock was already held. The initial process raised a C++
exception in `libxess_fg`. Disabling only the splash isolated it and allowed
300 accepted real frames, 576 total SDK-presented frames and 276 generated frames.
The fix derives SR framebuffer scale from the owned fixed output configuration,
without re-entering the SDK proxy. Normal splash/open-menu qualification follows.


## Earlier FG GPU evidence

Host: AMD Ryzen Z1 Extreme, RADV Mesa 26.2.6, Wine 11.0 from Proton-CachyOS
Latest. Standalone observations below qualify the harness; the separate Tekken 7
test qualifies the bridge in that game, not universal interpolation quality.

- DX11 baseline: 120 real frames, normal DXVK presenter.
- Direct DX11/DX10 initial runs: 235 captures out of 240; the initial prototype
  reported its five warmup iterations as errors. Warmup now has a distinct status
  and advances exactly once per real frame.
- Direct DX10 after the warmup fix: 589 accepted captures and 589 consumed real
  presents out of 600. FSR 3.1.6 logged 589 successful generation callbacks, each
  requesting one intermediate frame. `artifacts/optifg-evidence/direct-dx10.mkv`
  records only the probe window; its analysis JSON reports 334 distinct rectangle
  pose changes in 3.007 seconds (111.07 Hz), with source dispatches at 58.43 Hz.
  The container duplicates frames on a 1000 Hz timebase; its frame count is not
  the displayed frame rate. This is a limited synthetic interpolation check.
- Full ReShade/VORT DX11: at least 2,389 accepted captures after shader loading.
- Full ReShade/VORT DX10: at least 4,789 accepted captures after shader loading.
- Full ReShade/LumeniteFX native DX10, after the SDR metadata fix: separate
  1,800-real-frame runs with 8-bit default SDR and explicitly tagged SDR10 each
  logged at least 1,189 accepted captures and 1,188 consumed real presents,
  with 11 warmup rejections and no hard bridge status errors. The 8-bit run made
  no `SetColorSpace1` call. Logs/settings are in
  `artifacts/optifg-evidence/lumenite-sdr8-dx10/` and `lumenite-sdr10-dx10/`;
  `lumenite-sdr10-dx10.png` shows the debug inputs and correct scene output.
- LumeniteFX is the user's preferred provider and the probe runner's default. Download
  it directly from the author's pinned archive; do not redistribute its files.

Tekken 7 Steam build 10546199 exposed additional requirements: retain the actual
DX11 runtime rather than a hidden/probe or DX12 presenter runtime; load OptiScaler
as the game's imported `winmm.dll`, then ReShade64 via `LoadReShade=true`; accept
R10G10B10A2 only when the producer explicitly tags SDR. The camera values in the
transport test are provisional and are not a calibrated game profile.

The later SDR guard exposed ReShade 6.8's unknown color-space report on DX10.
The publisher now uses the selected runtime's color-space private data maintained
by [ReShade's DXGI wrapper](https://github.com/crosire/reshade/blob/v6.8.0/source/dxgi/dxgi_swapchain.cpp).
With no recorded change, legacy fixed-point surfaces use the
[DXGI SDR default](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/high-dynamic-range).
Explicit non-SDR metadata is rejected. The SDR10 probe calls `SetColorSpace1`
explicitly; an 8-bit run also checks the default without that call.

Tekken 7, NR off, LumeniteFX Kernel at
`f80e98d716ad72e12af6f7baa727bcfeb4be5094`:

- The first successful SDR10 training run logged at least 17,389 accepted captures
  and 17,388 consumed real presents, with 11 warmup rejections. The scene and
  colors were inspected in a screenshot and a short recording of only the game
  window. HUD interpolation artifacts were visible with capture disabled.
- After restarting with `[HudFix] Mode=3`, the log confirms `captured before the
  HUD`. At least 8,389 captures and 8,388 consumed real presents were accepted,
  again with 11 warmup rejections and no hard bridge status errors. Loading
  transitions temporarily remove depth and resume without stale input.
- `artifacts/optifg-evidence/tekken7-lumenite-baseline/` and
  `tekken7-lumenite-hud/` contain logs/settings. The adjacent PNG/MKV captures
  show the training scene and HUD. This establishes transport and basic visual
  operation; it does not establish latency, game-wide HUD quality or calibrated
  projection. Capture heuristics may miss a boundary and then omit HUDless input.
- LSFG-VK was disabled for the measured test process with `DISABLE_LSFGVK=1`.
  The system's layer configuration was not changed.
- Toggling the bridge off for five seconds and back on without restarting the
  game deactivated FG, registered a fresh stream, and resumed accepted captures.
  The original configuration was restored after the test.

Still unqualified: a second DX11 game, an actual DX10 game (the native DX10
harness passes), in-game resize/alt-tab, HDR, and combined NR + FG. Estimated
optical flow and provisional camera settings remain quality limitations.

The accidental old NR/OptiScaler install was moved according to its original
manifest into `artifacts/backups/tekken7-nr-opti-20261113-164400/`. Its cleanup JSON
records hashes and verifies the game executable was unchanged. The game test
uses a private copy of the Proton prefix in `build/tekken7-test/pfx`; the original
Steam prefix is not used for test writes. Experimental game files have their own
`optifg-test-install.json` manifest, separate from the removed NR install.
