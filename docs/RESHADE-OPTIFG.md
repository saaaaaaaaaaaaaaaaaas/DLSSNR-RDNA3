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

## Observed GPU evidence

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
- LumeniteFX is the user's preferred provider and the probe runner's default. Download
  it directly from the author's pinned archive; do not redistribute its files.

Tekken 7 Steam build 10546199 exposed additional requirements: retain the actual
DX11 runtime rather than a hidden/probe or DX12 presenter runtime; load OptiScaler
as the game's imported `winmm.dll`, then ReShade64 via `LoadReShade=true`; accept
R10G10B10A2 only when the producer explicitly tags SDR. The camera values in the
transport test are provisional and are not a calibrated game profile.

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
