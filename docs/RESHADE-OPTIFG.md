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
| Disabled | Existing NR and upscaler inputs behave as before | Pending |
| ABI | Versioned C ABI; reject incompatible/truncated descriptors | Pending |
| Capture | Publish after DLSS5_Feed, once per real frame, even with NR off | Pending |
| Guides | RG16F current-to-previous pixel MV and raw R32F depth, same frame/extent | Pending |
| Camera | Explicit near/far/vertical FOV; no unlabelled camera estimates | Pending |
| Transport | GPU copies and GPU fence dependencies; no image readback in the bridge | Pending |
| Ownership | Inputs borrowed for the call only; receiver copies before reuse | Pending |
| Presentation | Actual DX11 game swapchain connected to DX12 FG presenter | Pending target GPU check |
| Invalid input | Missing/stale/duplicate data never reuses previous guides as a new frame | Pending |
| Lifecycle | Reload/resize/gap/scene cut reset history; stream unregisters on teardown | Pending |
| HUD | Optional same-frame HUDless color; no claim that final color is HUDless | Pending |
| DX10 | DXVK D3D10 objects queried for their D3D11 interfaces | Pending target GPU check |
| FG | Start with FSR3 FG 2x, SDR, x86_64; verify actual intermediate frames | Pending target GPU check |
| NR | NR may run on real frames before FG; generated presents never feed history | Pending target GPU check |
| Packaging | Opt-in experimental bundle with matching producer/consumer/source patch | Pending |

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
