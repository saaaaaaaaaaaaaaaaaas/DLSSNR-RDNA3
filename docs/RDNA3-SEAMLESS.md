# Turbo causal temporal reconstruction

Owner request, 2026-11-21: implement `/home/bozo/Projects/GPT.txt` on Turbo only,
remove Fast and replace the old temporal reuse. This overrides the prompt's
request to keep the legacy mode on RDNA3. RDNA4 and Windows remain unchanged.
Starting fork source: `ff5fff22ad8c45fce13ee2c0901dc7547eb9d48c`.
Reference studied: BeliyG3/optimizer-fps-dlss5 `fe67a80cf462bad1409968bda2eb6fd4ea265342`,
MIT, especially temporal residual, reprojection and coherent-chain validation.

## Acceptance matrix before implementation

| Requirement | Implementation and required evidence |
| --- | --- |
| Strict causal cadence | Full NR at real evaluations 0,N,2N; N=2–4, no motion/cut/history-triggered extra inference. Test counters on Motion including invalid guides, cuts and generated-frame notifications. |
| No added presentation delay | Commands in the host's existing command buffer; no new queue, waits, fences, CPU history readbacks or future-frame dependency. Histories are past-frame textures only. |
| Residual reconstruction | Current input plus carried NR residual; per-link depth/color/edge validation, bounded Catmull–Rom detail, surface-matched low-frequency fill, adaptive fresh-residual adoption. |
| Colored trails | Thin moving blue sword/handle and newly uncovered background in Motion. Compare old reuse, full NR and replacement, with masks and quantitative residual errors. |
| Integration | Existing motion scales/subrect/depth conversion and encoded model domain. Preserve alpha, FP16 and source/render/model resolution mapping. Only NR evaluate advances cadence; FG generation does not. |
| Configuration | Replace RDNA3 [Reuse] with [Temporal], default interval 2, enable switch, depth/color thresholds, hole fill, confidence display and GPU timing/counters. Full NR remains available. |
| Removal | Remove Fast production profile and archive selection; retain historical validation records as history. No new Fast build. Remove old reuse from the RDNA3 executable path; preserve RDNA4 source behavior. |
| Verification | Pinned dependencies, affected SPIR-V validation, CPU schedule/config checks, strict before/after full-NR regressions, deterministic Motion sequences at N=2–4, GPU timings on Z1 if accessible, model-free local package audit. |
| Delivery | Source to main with [skip ci]; Turbo-only local package. No RDNA4 build, release/tag or archive upload. |

The owner permits any Motion scene. Use a deterministic procedural scene with
analytic color, depth and backward vectors; distinguish it from actual game tests.
Camera cuts invalidate history without advancing the scheduled inference.
Completely unrecoverable pixels may use current color as the last resort; do not
force an unrelated residual across a silhouette to hide rejection.

## Implemented behavior

`SeamlessHistory` in `linux/src/core/nr_seamless.hpp` owns each feature's past
residual, exact FP32 color/depth guides and a temporary projected residual.
`nr_temporal_cadence.hpp` advances only on real NR evaluations. Startup runs
full NR, then evaluations 0,N,2N do so; resets, camera movement, invalid vectors
and rejected pixels are not scheduler inputs. Explicit generated-frame calls
return before resource access or cadence updates. The existing OptiScaler
NR Evaluate route remains before SR and FG. Multiple NGX features have independent
histories and cadence. The host session owns a shared per-feature clock, so
cached runtime changes and resource recreation retain phase independently of
image-history migration. Changed extents invalidate reconstruction data without
advancing inference. Actual game DRS integration remains unqualified.

The replacement Vulkan compute pipeline:

1. Reads current backward motion at full model resolution. Full-resolution
   vectors prevent coarse bilinear mixing of the velocities of a thin object and
   its background. Existing scale/subrect conversion is retained.
2. Reprojects only the previous enhancement, not the old rendered frame.
   Endpoint color/chromaticity/luminance and depth tests weight each footprint
   sample. Confidence propagates through each link, so a rejected earlier link
   cannot spontaneously become trusted detail. Catmull–Rom detail is used only
   on accepted surfaces and clamped to the bilinear footprint's range.
3. Reconstructs rejected areas from two rings of donors on compatible current
   surfaces. The low-frequency addition is scaled by current luminance and
   aligned with current chroma; stale blue chroma is not transplanted into the
   newly exposed background. Confidence blends detail into conservative tone.
   A cut with no compatible information can show raw current color until the
   next scheduled full pass; fabricating a new surface's neural detail is unsafe.
4. On full passes, clamps the carried residual to the fresh residual's 3x3 range
   and adopts it adaptively (at most 0.6, fading with motion and confidence).
   A separate compose dispatch avoids reading/writing neighbors of the same
   output image concurrently. Alpha is restored from the current input.

This adapts the synchronous residual, validated carry, bounded sampling, hole
fill and adaptive adoption ideas from the MIT reference. It deliberately uses a
single short carry chain (at most three skipped frames), not the reference's
D3D12 async queues, background frame stores, dual-chain machine or FG stage.
Reconstruction already runs at NR input/model resolution; no extra coarse grid
is inserted into the host's existing model-resolution scaling. Uniform far
surfaces, including sky, receive the same compatible low-frequency field.

True expected-depth reprojection requires previous-depth/matrix or 3D motion
data which this Vulkan host interface does not provide. The implementation uses
conservative forward/reverse-Z device-depth validation instead; Z changes may
reject detail. It does not claim to implement the reference's matrix-based
expected-depth or dedicated SkyField passes.

No new presentation queue, frame delay, fence, CPU history readback or future
frame dependency is introduced. Commands use the caller's current command
buffer; image initialization during recording defers its barriers into that
buffer. The existing nonblocking timestamp ring supplies diagnostics. This
does add reconstruction processing time, and full/short frame alternation
remains. Physical input-to-display latency has not been measured.

The private reconstruction images cost 56 bytes per model pixel (49.2 MiB at
720p, 110.7 MiB at 1080p), plus full-resolution motion/depth and the existing
network/estimator histories. These are past-frame resources, not queued frames.
They are independent per feature and retired with the existing GPU-use proofs.

## Configuration

RDNA3's `[Reuse]` settings and motion gate are retired. `[Temporal]` is emitted
by the normal INI generator and exposed in NR Control's Network page:

```ini
[Temporal]
Enabled = 1
Interval = 2
DepthTolerance = 0.050
ColorTolerance = 0.080
HoleFill = 1
Confidence = 0
GpuTiming = 1
```

`Enabled=0` selects full NR on every real frame. Interval is clamped to 2–4.
DepthTolerance=0 disables the optional depth test. The debug view is green for
trusted detail and red for rejected/filled history. The log reports actual full
and reconstructed evaluations, unexpected launches and complete reconstruction
frame GPU time (including adapters/copies). Timing queries never use WAIT.
The old `reuse_*` C++ member names remain internal compatibility plumbing; the
legacy implementation and shaders are excluded from the RDNA3 build. RDNA4 and
the separate Windows source tree keep their existing modes.

## Checks on 2026-11-21

All **422 Turbo SPIR-V files** across the six networks pass `spirv-val`. Network
and temporal pre/post bytes match the previously validated Turbo build; only
the four old runtime reuse passes are replaced by `runtime_seamless.spv`.
No Fast or RDNA4 network is compiled. Fast is no longer a production profile.

Full-NR regression: all **13** saved Silver/Small frame/engine results at
1080p, 1440p and 4K match their pre-change Turbo bytes. Before/after strict check
logs are identical, including the expected FAIL verdicts against historical
original-model goldens: Turbo already changes quantization. Historical goldens
are unchanged. The new approximation is tested against full-NR Turbo separately.

The Motion fixture includes camera rotation through 51.6 degrees, translation,
an independently moving thin blue handle/blade, disocclusion, a cut on skipped
frame 13, nonfinite vectors on frame 15 and a static tail. It is run at 640x360
and 1280x720 with full NR, N=2/3/4 and the preserved old runtime. Every 24-frame
new-mode run has exactly 12/8/6 full evaluations, zero motion-gated/unexpected
launches and no persistent-kernel dependency errors. N=2 repeats byte-identically
at both sizes. Three explicitly tagged FG calls between real frames do no work
and do not change counters. This is a scheduling simulation, not a real FG x4
presentation test.

Additional GPU runs pass forward/reverse Z, absent depth, 641x359 output,
confidence visualization and two interleaved feature histories. The final native
runtime reproduces the recorded N=2 images byte for byte. The CPU port suite,
cadence invariants, INI generation and Python/shell syntax checks pass.

On the 720p fixture, positive blue error in the exposed-background mask averages
**7.01** RGB8 levels with old reuse and **0.99** with N=2, relative to full NR.
On the four frames where both implementations actually skip NR, the respective
means are **25.48** and **2.07**. The old runtime launches full NR 16 times,
including six motion-gate trips; the new one launches 12 times. The legacy test
uses the original runtime with the same current test harness, depth, input,
motion, explicit cut and timestamps. Its quarter-resolution guides and old gate
are deliberately preserved as part of that baseline.

These scores describe an analytic regression fixture, not the owner's game or
an independent new-model reference. They demonstrate rejection of stale color
on a thin moving surface; they do not prove the real sword-handle issue is solved
in every renderer. HDR/PQ, real-game resizing, pre-SR with the actual OptiScaler
build, FG/HUD interaction and moving-scene perception still need game testing.
The residual is carried in the existing NR model color domain; the host's HDR
encoding/decoding remains outside it.

See `build/seamless/motion-comparison.png`, `720/motion-comparison.png` and
the [machine-readable validation record](RDNA3-SEAMLESS-VALIDATION.json).

## Dedicated GPU timing

Three interleaved rounds on Z1 Extreme / Mesa 26.2.6-arch3.1, with no simultaneous
builds or GPU tests. Each process evaluates all 24 Motion frames. GPU timestamps
cover the complete runtime recording; upload/readback sit outside the pair.
The first 12 frames are excluded so the measured 12 contain complete N=2/3/4
periods. The table gives medians of the three per-sequence means at 1280x720:

| Mode | Complete NR stage average, ms | Range, ms | Reconstructed-frame median, ms |
| --- | ---: | ---: | ---: |
| Full NR | 76.861 | 76.805–77.064 | — |
| New N=2 | 40.250 | 40.165–40.443 | 2.476 |
| New N=3 | 27.902 | 27.747–27.982 | 2.749 |
| New N=4 | 21.859 | 21.791–21.881 | 2.997 |
| Old reuse N=2 | 51.671 | 51.384–51.781 | 1.153 |

N=2 reduces this fixture's average NR stage time by **47.6%** relative to full
NR, and by **22.1%** relative to the old gate-driven mode. New full frames cost
about 78.1–78.2 ms: residual storage/adoption and guide handling add processing.
Reconstruction is more expensive than the old unvalidated warp but remains much
cheaper than inference and avoids its extra motion-triggered launches.

These are this harness's complete NR stage times, not the isolated graph timings
from the earlier Turbo optimization and not the owner's stated 10 ms in a game.
The harness synchronizes uploads/readbacks between samples; automatic clocks and
desktop load still affect GPU frequency. Power/clocks were not changed. Sensor
snapshots and all per-frame timings are in the JSON. This proves no future-frame
dependency; it is not a measured physical latency or game-FPS improvement.

Reproduce after building the test runner and preparing private models:

```sh
python3 linux/test/motion_scene.py build/motion --width 1280 --height 720
python3 linux/test/bench_motion.py --scene build/motion \
  --installed /path/to/private-test-install --runner build/run_sequence \
  --rounds 3 --out build/motion-timing
```

The final Vulkan image-usage correction is included in these timings. Both
sampled and storage usages are declared for the history images; the write views
are sampler-free. All 24 N=2 Motion 720p outputs remain byte-identical to the
recorded quality run. All seven modes also repeat their four-frame outputs
exactly after this correction.

In normal moving frames 4–12 before the cut or invalid-vector fault, exposed-mask
blue error at 720p is 11.14 RGB8 levels for old reuse and 0.32 for the new mode.
The local comparison image shows frame 5, where both implementations skip NR.

The complete Turbo package is built locally, including the ReShade add-ons,
OptiScaler ASI, Vulkan loader/layer and all six shader sets. Its full host build
is from `179ca16`; only `nr_seamless.hpp` changed afterwards. The three runtime
translation units are rebuilt and their consumers relinked at `ac2eba9`. All
other production C++ and data sources are byte-identical between those commits.
The internal package version is `0.0.9-Turbo-Temporal`; final artifact checksums
and component provenance are recorded separately after packaging.
