# Causal residual lighting stability

Baseline: `561530a`. The owner observes a second-frame lighting flicker with
**Lighting off** and requests all four reconstruction improvements. This work
therefore targets the RDNA3 causal residual path, independently of Ditto Relight.
Work and commits remain local on `feature/z1e-ditto-relight`. No RDNA4 build.

## Acceptance matrix before implementation

| Area | Required behavior and evidence |
| --- | --- |
| Separate lighting/detail | Geometry-aware low-frequency RGB gain and separate signed detail/confidence; rejected fine detail must not fade the entire lighting field. |
| Photometry | Relative log-gain carries lighting against current model input; exposure/luminance changes rescale carried detail, without modifying learned kernels or adding model passes. |
| Common stabilization | The latest full-NR lighting target and displayed filtered field are distinct. Both full and reconstructed frames run the same adaptive lighting update and compose path. |
| Instability | Three previous normalized NR-target luminance samples plus current detect repeating gains; retain an adaptive instability state between full passes, suppress it on real shading changes, motion and invalid surfaces. |
| Cadence/latency | N=2–4 full NR only on schedule, including cuts/invalid guides. No future frame, additional queue, readback or wait. |
| Tests | Lighting-off synthetic gain/exposure/parity tests against the old shader; actual-model Motion at N=2–4; per-phase low-frequency luma, edges/disocclusion, cuts and FG notifications. |
| Regression | Temporal disabled remains identical. Protect learned SPIR-V, calibration, RenoDX/Ditto vendor shaders and RDNA4/Windows. Retain original goldens. |
| Delivery | Matching host and shader ABI, SPIR-V validation and local model-free package; local commits only. |

These approximations intentionally change temporal output. Procedural tests
and model fixtures do not qualify the owner's actual game/FG/HDR presentation.

## Implementation

1. Full NR is decomposed into a 3x3 depth/chroma-aware RGB log-gain field and
   signed detail. Lighting and detail have independent confidence. A rejected
   detail sample cannot fade the whole lighting correction.
2. Lighting multiplies the current input rather than adding the old frame's
   brightness difference. A small floor handles near-black input. Carried detail
   scales by the current/previous input luminance ratio, bounded to 1/8–8.
   Normalized chroma and depth validate the surface independently of exposure;
   very large brightness changes reduce detail trust. The domain is NR's model
   input, not necessarily scene-linear radiance or a universal HDR encoding.
3. The latest full-NR target is distinct from displayed filtered lighting.
   Both full and reconstructed frames advance the same filter and use the same
   composition pass. Response depends on motion, input shading, local gain
   variance and confidence. Fresh detail has an adaptive neighborhood clamp.
4. A luminance-gain detector compares the current target to three previous
   target measurements. Samples advance on full NR, preventing N=3/4 from
   aliasing a four-real-frame history. Instability persists between full passes
   and lowers filter response. Motion, shading changes and invalid history
   release it; a genuine step is not held as flicker.

Disocclusion fill borrows only scalar lighting from compatible current surfaces,
without creating detail confidence. Cuts/invalid guides cannot request extra
inference. No production queue, CPU readback or synchronization wait was added.
Alpha is restored from current input.

The original residual reference remains under its MIT attribution. Exposure
compensation and luminance-instability handling also follow general principles
described in [AMD FSR's luminance instability documentation](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/super-resolution-upscaler/#compute-luminance-instability).
This is an independent implementation without FSR accumulation or future frames.

## Verification on Z1 Extreme

Device: AMD Ryzen Z1 Extreme / RADV PHOENIX, Mesa 26.2.6-arch3.1. Lighting is
**off** in every test below. Private model weights are outside Git and packages.
Machine-readable measurements: [validation record](RESIDUAL-STABILITY-VALIDATION.json).

`linux/test/residual_stability.cpp` exercises the actual GPU history and shader
against an analytic full-NR oracle: 48 real frames, seven cases, N=2/3/4.
The frozen baseline is `561530a`; the old host/header and old shader are paired.

| Check | Before | After |
| --- | ---: | ---: |
| Alternating 8% input exposure, luma RMS (stops) | 0.0106–0.0153 | <0.000002 |
| Alternating 50% input exposure, luma RMS (stops) | 0.0706–0.1323 | <0.000001 |
| Oscillating NR gain, luma RMS (stops), N=2 | 0.03147 | 0.00402 |
| Oscillating NR gain, N=3 | 0.03140 | 0.00615 |
| Oscillating NR gain, N=4 | 0.03147 | 0.00823 |
| Colored gain with changing confidence, RGB RMSE | 0.0190–0.0391 | <0.000014 |

All 21 cases pass, including alpha, cut/invalid-vector handling, expected full
counts and no unexpected dispatch. A real exposure/lighting step converges
within 0.02 stops by its third real frame. The analytic fixture has no learned
model and is not a game-quality measurement.

Actual `CC_SMALL_FP8` Turbo Motion, 640x360, 24 frames per case, passes at N=1–4
with synchronization validation. N=2/3/4 perform exactly 12/8/6 full passes;
generated-frame notifications do not advance cadence. Reverse-Z and runtime
recreation are checked separately. Temporal disabled is byte-identical to the
old runtime across 72 output frames at 360p/720p. No learned kernels, calibration, vendor lighting shaders, original
goldens, RDNA4 or Windows sources changed.

Motion's 16x16-averaged luma error against full NR improves slightly, from
0.00700/0.00777/0.00900 stops to 0.00687/0.00767/0.00886 at N=2/3/4.
This is not a large general quality improvement: the exposed-pixel positive
blue error during movement increases from 0.383/0.522/0.545 to
0.491/0.583/0.588 code values out of 255. Relative lighting and the smoother
detail carry trade a small edge error for brightness stability. See per-frame
and per-phase measurements; the artificial scene does not reproduce the owner's
game lighting or prove that every observed flicker is eliminated.

## Cost and ABI

Two interleaved rounds of actual-model Motion at 1280x720 use GPU timestamps,
frames 12–23 (whole periods of N=2/3/4), with no concurrent build/GPU test.
Means across rounds, automatic clocks, validation disabled for timing:

| Interval | Before mean ms | After mean ms | Before reconstruction ms | After reconstruction ms |
| --- | ---: | ---: | ---: | ---: |
| N=1 | 75.36 | 75.53 | — | — |
| N=2 | 40.01 | 41.21 | 2.58 | 4.14 |
| N=3 | 27.37 | 29.18 | 2.75 | 4.35 |
| N=4 | 21.41 | 23.11 | 2.93 | 4.65 |

The extra filtering costs processing time, not an added presentation frame.
History/scratch payload rises from 56 to 136 bytes per model pixel per feature:
49.2 to 119.5 MiB at 720p, excluding allocation alignment. Lower model resolution
reduces this quadratically. N=1 never allocates a feature's reconstruction history.

The residual ABI now uses 19 descriptors and a 60-byte push block. The host
rejects the old shader before dispatch; the release audit requires the matching
shader in all six network folders and the ABI guard in all three runtime
consumers. Update host and shaders together. An ASI-only replacement is
insufficient for this change.

Standalone native GPU tools now enable the Int16/device-scope memory-model
features already declared by the RDNA3 learned shaders. This removes validation
errors in the test-device setup; borrowed game devices and network arithmetic
are unaffected. The shared stage fixture also enables subgroup size control.

## Reproduction

After `bash fetch_deps.sh`, build only RDNA3/Turbo and use a private installed
model tree. Compile and validate the residual shader with pinned glslang:

```sh
mkdir -p build/residual-stability
toolchain/glslang/bin/glslangValidator -V --target-env vulkan1.3 \
    linux/shaders/passes/runtime_seamless.comp \
    -o build/residual-stability/runtime_seamless.spv
spirv-val --target-env vulkan1.3 build/residual-stability/runtime_seamless.spv
g++ -std=c++17 -O1 -DNR_ARCH_RDNA3=1 -Itoolchain/Vulkan-Headers/include \
    -Ilinux/src/core -Ilinux/src/pe linux/test/residual_stability.cpp \
    -lvulkan -o build/residual-stability/oracle
build/residual-stability/oracle build/residual-stability build/residual-stability/oracle-results
```

For actual-model testing use `linux/test/run_sequence.cpp`, engine motion/depth,
`DLSSNR_MODEL=CC_SMALL_FP8`, `RUN_SEQUENCE_CUT=13`, `RUN_SEQUENCE_FG4=1` and the
Motion fixture from `linux/test/motion_scene.py`. `score_residual_stability.py`
compares paired before/after N=1–4 output folders. No original golden is replaced.
