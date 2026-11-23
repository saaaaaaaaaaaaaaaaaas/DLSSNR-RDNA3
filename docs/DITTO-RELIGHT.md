# Ditto Relight 0.5.4 on RDNA3

Starting source: `b0ec7532d579908727039ad46a323142714f6f1c`.
Reference: [DittoSR v0.5.4](https://github.com/Vancete/DittoSR/releases/tag/v0.5.4),
tag resolved locally to `b241aa43c45e11e3942a18dc1004dc9e5ba3ee3f`.
Work stays on local branch `feature/z1e-ditto-relight`; no push or release.
Only Linux/Proton RDNA3 Turbo is built. RDNA4 and Windows are outside this change.

## Acceptance matrix before implementation

| Area | Required behavior and evidence |
| --- | --- |
| Reference | Pin vendor sources to 0.5.4, retain MIT/BSD/CORTAO notices and distinguish adapted integration shaders. |
| Relight | Real AO/GI passes, including spatial/temporal filtering, half-size geometry-aware reconstruction and baked curve; never apply game exposure twice to encoded neural RGB. |
| Low model resolution | Reconstruct the neural edit against the full-resolution reference; use color and geometry at silhouettes, retain original texture and alpha. Test 25%, 33%, 50%, odd sizes and 100% bypass. |
| Motion | Camera motion, a moving thin colored object, disocclusion, reset/cuts, forward/reverse depth and multiple passes. Compare to the previous Ditto path and spatial transport. |
| Cadence | Existing causal temporal NR remains responsible for scheduled model calls; the reconstruction does not launch extra inference or depend on future frames. |
| Composition | Preserve SDR/HDR codec, strengths, preprocess, original jitter and per-feature/per-pass histories. Zero-strength NR still bypasses exactly. |
| Lifecycle | Initialize all sampled history, invalidate on reset/settings/resize/bypass, retire with existing GPU completion tracking; API/synchronization validation. |
| Controls | Replace the Ditto SR menu entry with Ditto Relight, preserve saved selection 4, expose AO/GI strength and a lighting toggle for comparison. |
| Build | Pinned dependencies; affected SPIR-V validation, native stage fixture, RDNA3 ASI build, protected learned-network/RenoDX source comparison. |

Relight is screen-space lighting, not a learned reconstruction network. Geometry
missing from both current guides and the low-resolution model cannot be invented.
Physical game/FG/HDR quality is qualified separately from procedural fixtures.

## Implemented path

Saved `NRLookUpsample=4` now selects **Ditto Relight**. Relight runs once on the
full-size running reference before NR encoding. It retains upstream's AO/GI,
depth/color mips, occluder LUT, sparse filter, separate lighting histories,
fused CACAO blur and baked curve. Its main/filter/history grid is half the
reference size, regardless of the model scale. Composition upsamples by depth
and normal. Full-size normals deliberately retain thin geometry; the optional
packed/half-size prepass is not selected. At 100%, lighting still runs while
the model's reconstruction histories are not allocated.

The SR component is adapted to reconstruct a **signed neural edit**, rather than
the entire neural picture. The low-size dispatch encodes `log2(N/P)` with a
bounded reversible mapping. DittoSR 0.5.4 accumulates this field with its new
reactivity envelope and stability history. A 4x4 joint bilateral transport uses
the current full-size color and depth, rejects other surfaces, clamps carried
edits to current matching evidence and rephases the result to the game's jitter.
It fills both `up_proxy` and `up_neural` for the existing full-size codec resolve.
Original texture/alpha are retained; missing model detail is not fabricated.
The redundant old full-size upsample is skipped, and log-ratios are computed
once at model size rather than for every output tap. FP16 edit/history storage
and approximate transport can change the picture; learned kernels are unchanged.

Each model pass has independent reconstruction histories. Lighting has one
history per stage. Cuts, settings, bypass and resize invalidate the appropriate
state. Disabling lighting retires its images under existing GPU completion
tracking. No queue, CPU history readback, wait or future-frame dependency is
added. NR's existing N=2–4 causal scheduler is unchanged. All new paths are
gated to RDNA3; RDNA4 keeps the previous v0.4.0 vendor shaders and host path.

## Controls

Choose a RenoDX layer, **Before the upscaler**, **Upsampling: Ditto Relight**,
and **Resolution scale: 0.25–1**. Disable **Follow render resolution** to use
that scale. In the **Ditto Relight** group, Lighting controls AO/GI and the
upstream tone curve; disabling it retains guided neural reconstruction.
Ambient occlusion, Global illumination and Samples are adjustable.

The NR guide interface supplies no projection metadata. The explicit Vertical
FOV, Near plane and Far plane controls default to upstream's 60 degrees, 0.1
and 100000 view units. Match them to the game camera when needed. Metres per
view unit are unknown (assumed 1), so the AO distance fade is an estimate.
Relight is experimental screen-space lighting and changes tone intentionally.
Actual game/HDR10/PQ, DRS, OptiScaler SR and FG x4 presentation remain unqualified.

## Verification on 2026-11-23

Native tests ran on **AMD Ryzen Z1 Extreme (RADV PHOENIX)** with
Mesa 26.2.6-arch3.1. The procedural
Motion fixture uses 321x181, 20 frames, analytic identity/material-gain networks,
camera pan/rotation, an independently moving thin blue object and a cut.
These are transport oracles, not learned-model or game quality measurements.

With lighting disabled for an equivalent-image comparison:

| Model scale | Old Ditto SR RMSE | Guided edit RMSE | Old edge RMSE | Guided edge RMSE |
| --- | ---: | ---: | ---: | ---: |
| 25% | 0.018766 | 0.001446 | 0.064606 | 0.004035 |
| 33% | 0.017498 | 0.001742 | 0.059822 | 0.005348 |
| 50% | 0.014056 | 0.001396 | 0.046176 | 0.003962 |

The existing spatial edge-aware path at 33% gives 0.002491 / 0.008388. Forward
and reverse Z now give the same new scores. The old 25% run uses a forced
fixture extent; the previous UI exposed 33% as its minimum. Identity Motion
RMSE is at most 0.000486 and alpha error is zero. All 13 Motion cases run
exactly 20 analytic network calls, with no reconstruction-induced calls.
Lighting comparisons use separate oracles: AO and GI alone change pixels by
0.00366211 and 0.00341797 respectively, apart from the shared curve.

- Zero API/synchronization validation errors in Motion and the stage fixture.
- Stage fixture: SDR/HDR, 25/33/50/100%, true D32/subrect guides, odd sizes,
  two passes, inactive pass, Preprocess, sRGB, resets, ring reuse and live switches.
- Full-reference checker contrast 0.5; half-strength blend error below 0.0002;
  zero-strength HDR bypass exact; independent-pass drift zero. The dark checker
  (0.0001 / 0.002) retains 0.00190079 contrast with zero error. Ratio and guide
  floors match the original codec (1/1024 and 1e-8 respectively).
- Mixed v0.4.0/v0.5.4 shaders are rejected before dispatch by cbuffer-layout checks.
- All 15 Ditto/Relight SPIR-V files pass `spirv-val --target-env vulkan1.1`.
- Vendor HLSL/HLSLI match the pinned tag byte for byte. Original RenoDX,
  learned kernels, planner/calibration, causal NR runtime and Windows sources
  are unchanged. No golden was overwritten and no RDNA4 build was run.
- RDNA3 CPU/descriptor/presentation/QKV/paired-WMMA and generated INI checks pass.

Details and shader hashes: [validation record](DITTO-RELIGHT-VALIDATION.json).
Local logs and raw comparisons: `build/ditto-relight/`.

## Reproduce and install locally

```sh
bash fetch_deps.sh
NR_GPU=rdna3 bash linux/build/build_renodx.sh build/ditto-relight/shaders
g++ -std=c++17 -O1 -DNR_ARCH_RDNA3=1 -Itoolchain/Vulkan-Headers/include \
  -Ilinux/src/core -Ilinux/src/pe linux/test/dittosr_stage.cpp \
  linux/src/core/nr_renodx.cpp -lvulkan -o build/ditto-relight/stage
NR_RENODX_ROBUST=1 build/ditto-relight/stage build/ditto-relight/shaders
toolchain/glslang/bin/glslang -V --target-env vulkan1.1 \
  linux/test/ditto_relight_network.comp -o build/ditto-relight/network.spv
g++ -std=c++17 -O1 -DNR_ARCH_RDNA3=1 -Itoolchain/Vulkan-Headers/include \
  -Ilinux/src/core -Ilinux/src/pe linux/test/ditto_relight_motion.cpp \
  linux/src/core/nr_renodx.cpp -lvulkan -o build/ditto-relight/motion
NR_RENODX_ROBUST=1 build/ditto-relight/motion build/ditto-relight/shaders \
  build/ditto-relight/network.spv build/ditto-relight/motion-current
NR_GPU=rdna3 NR_ARCH=x86_64 NR_SHADER_PROFILE=z1-extreme-turbo \
  bash linux/build/build_asi.sh artifacts/ditto-relight/asi
bash linux/build/package_dittosr_update.sh artifacts/ditto-relight/asi artifacts/ditto-relight/update
```

The native fixture enables Float16 for Slang's half packing, as production NR
already requires. Enable `VK_LAYER_KHRONOS_validation` plus synchronization
validation for the API checks. Test tools/layers stay project-local.
The local update is for the OptiScaler ASI route on an existing RDNA3 0.0.9
installation. Replace the ASI and **all 15 shaders and four notices** together;
keep private models and OptiScaler. No automatic installation or publication.
