# Further Z1 Extreme optimization

Starting source: `e7f7ac84b37b31c87cbbcbc321587a2f47f1f02c`.
Implementation branch: `perf/z1e-aggressive`, separate worktree `DLSSNR-RDNA3-Z1`.
Target: ROG Ally Z1 Extreme / RADV PHOENIX, Mesa 26.2.6-arch3.1.

The owner explicitly permits changes to the picture. Approximate candidates must
be isolated from the existing generic and exact Z1 profiles, with their arithmetic
and image differences documented. Historical goldens are not overwritten.
RDNA4 is not compiled, no release/archive is uploaded, and source pushes to main
use `[skip ci]` so the existing multi-GPU workflow cannot run.

## Acceptance matrix before implementation

| Area | Candidate | Evidence required |
| --- | --- | --- |
| C=32 pre/post | Reduce live fragments and/or narrowing/activation work | Target full-network timing, frame PSNR and deterministic output; compare against the current exact Z1 profile |
| GEMM | Simplify gfx11 weight loads/packing and tune their live operands | Matching host packing, unchanged manifest geometry, compilation, exact comparison for arithmetic-preserving candidates |
| Approximate arithmetic | Selective e4m3 rounding removal and packed-half alternatives | Explicit opt-in profile, separate recorded results, comparisons to both baseline and NVIDIA frames; no silent replacement of generic math |
| Scope | Prioritize Small at 720p and 1080p, cover Silver and installed optimized modes | Warm interleaved repeated measurements, separate quality/correctness/performance results |
| Delivery | Model-free local RDNA3 package, existing precise build preserved | Pinned dependency setup, appropriate CPU/SPIR-V/GPU checks, archive audit and documented rollback |

Keep power/clocks, services, drivers and game installations unchanged. Current GPU
load and automatic clocks must be recorded; isolated network time is not game FPS.

## Selected implementations

- `z1-extreme-fast` inherits the existing exact Z1 profile. C32 activation and
  attention-context values retain FP16 precision, with the original saturation
  bound, rather than being rounded to e4m3. Q/K and probability rounding and
  tensor stores are retained there. The post block uses FP16 at all rounding
  sites. This changes the result and is an explicit approximation.
- `z1-extreme-turbo` retains FP16 values at every e4m3 rounding site in C32
  pipelines. It still clamps to +/-448. Matrix accumulators remain FP32 and
  model weights are unchanged. It is independent of device power modes.
- Generic and the existing exact `z1-extreme` profile keep their original math.
  Derived INT4 pipelines and the legacy INT4 network are unchanged.

Use `NR_SHADER_PROFILE=z1-extreme-fast` or `z1-extreme-turbo` with `NR_GPU=rdna3`
to build a separate local package. No runtime launch variable is needed.

## Rejected research

Native gfx11 GEMM weight records, vector/cooperative loads, exchanging operand
halves through subgroup shuffles and several prefetch depths all produced exact
output. The best combined candidate differed by only 0.03 ms at 1080p and 0.09 ms
at 720p from the corresponding arithmetic-only candidate (two-run medians).
That does not justify changing host weight packing. The prototype is not shipped.

Scaled ReLU in the input C32 block produced roughly 18.6 dB against the current
output for a small speed improvement. Hard-swish, removing C32 MLP streaming and
changing the post projection's FMA spelling were also rejected. Recipes and logs
remain under ignored `build/aggressive/`; the removed prototypes are preserved
locally in `research-experiments.patch`.

## Measurements on Z1 Extreme

The baseline is the already optimized exact `z1-extreme` profile, not generic.
Four rounds alternate baseline/Fast/Turbo and Turbo/Fast/baseline. Each process
warms up 30 times and measures 40 repeats, with per-layer timestamps enabled.
The table shows the median GPU network time and reduction relative to that
baseline in the same experiment.

| Model / output | Exact Z1, ms | Fast, ms | Fast reduction | Turbo, ms | Turbo reduction |
| --- | ---: | ---: | ---: | ---: | ---: |
| Small 720p | 40.477 | 39.222 | 3.10% | 38.473 | 4.95% |
| Small 1080p | 87.095 | 84.188 | 3.34% | 82.383 | 5.41% |
| Silver 720p | 73.242 | 71.719 | 2.08% | 70.911 | 3.18% |
| Silver 1080p | 151.428 | 147.700 | 2.46% | 145.264 | 4.07% |

All 48 outputs are finite and deterministic within their profile. There are no
reported persistent-kernel dependency failures. The machine uses automatic GPU
clocks with desktop applications open; no power limit or clock was changed.
For example, Small 1080p's exact runs span 82.861–88.968 ms, so the final decimal
and the improvement percentage are not guarantees. This is isolated network
time on one test scene, not game FPS. Do not add these percentages to an earlier
session's improvement over generic.

The 720p input is a Lanczos downsample of the existing 1080p test input. It has
no matching NVIDIA reference; only the 1080p image is scored against NVIDIA.
The unchanged host test binary, model/input hashes, shader-tree hashes, each
run's times, output hashes and telemetry are recorded in the validation JSON.
Scores and image exports are computed outside the timed runs.

## Image differences

These profiles intentionally change the network's rounding. More retained
precision does not guarantee a better picture, because the model expects its
original quantization. Fast is the more conservative trial; Turbo trades a larger
change for another 1–2 ms here. Neither becomes the generic/default source build.

At 1080p, using clipped, nearest-even RGB8 for both metrics:

| Model / profile | PSNR vs exact Z1, dB | SSIM vs exact Z1 | PSNR vs NVIDIA, dB | SSIM vs NVIDIA |
| --- | ---: | ---: | ---: | ---: |
| Small exact | exact | 1 | 49.435 | 0.997078 |
| Small Fast | 49.184 | 0.997206 | 49.132 | 0.997187 |
| Small Turbo | 40.481 | 0.995527 | 40.429 | 0.995469 |
| Silver exact | exact | 1 | 45.620 | 0.996070 |
| Silver Fast | 47.005 | 0.996769 | 45.244 | 0.996264 |
| Silver Turbo | 42.518 | 0.995775 | 42.300 | 0.995181 |

Fast mainly changes fine detail on this scene; Turbo also changes broad tonal
regions. The local `final-small-1080p/comparison.png` and
`final-silver-1080p/comparison.png` show the pictures and an absolute difference
amplified eight times. A single frame cannot establish motion stability or
quality in other scenes. NVIDIA's original-model images are not independent
references for fitted/INT4 networks or approval of a new trained model.

## Checks

All six generic and exact-Z1 shader sets rebuilt byte-identically: 514 + 520
files. All 850 Fast/Turbo SPIR-V files across six networks pass `spirv-val`.
Each approximate network changes only its 18 selected SPIR-V files and GPU
profile marker; host constants and derived INT4 shader bytes are unchanged.

Before/after strict Silver/Small logs are identical, including their known
historical frame-golden FAIL/CLOSE verdicts. Layer hashes remain identical.
The quantizer reports zero mismatches for all FP16 values, all 2^32 FP32 bit
patterns, and all 2^32 FP16 pairs; WMMA lane halves pass. All 13 saved exact-Z1
frame/engine outputs at 1080p, 1440p and 4K match the pre-change bytes. No golden
is replaced to accept an approximation.

Fast and Turbo each run all seven installed modes twice at 1080p: Small, Silver,
Experimental, Quality, Balanced, Performance and Ultra. Network selections match
the request with no fallback, and every pair is deterministic. These checks exit
2, `UNVERIFIED (no approved reference)`, as designed: the original-model NVIDIA
frames cannot approve fitted/INT4 modes. Their image differences against the
previous exact Z1 outputs are recorded separately in the validation JSON.

Small and Silver also each pass the FP16 in-place `record_engine` harness twice
per profile, including temporal shader variants, two frames and zero motion.
This is a runtime smoke test, not moving-scene quality acceptance. The CPU port
suite passes descriptor aliases, presentation state, Ultra paired WMMAs/QKV
routing (including odd dimensions), paths, ImGui and INI defaults. Python and
shell syntax checks pass.

See [machine-readable evidence](RDNA3-Z1E-FAST-VALIDATION.json). Local logs and
private candidate frames remain under `build/aggressive/`; no weights or raw
candidate frames are added to Git.

## Local packages and rollback

The delivered Fast/Turbo packages reuse the host components of the audited exact
archive. All C++ and calibration sources and host compilation defines are
byte-identical to that build. Each package replaces all 520 network/profile files
with the freshly validated profile trees, including temporal variants. Everything
else in the baseline package stays byte-identical; the new qualification documents
and `Z1-PROFILE.txt` identify the trial. Host binaries retain their internal
`0.0.9-z1-preview-g72a157f` stamp; shader source is `1a1a481`.

The model-free archives and adjacent checksums are in `linux/package/`, named
`DLSS-NR-AMD-Vulcan-Linux-0.0.9-Z1E-Optimized-Fast-RDNA-3.tar.gz` and
`DLSS-NR-AMD-Vulcan-Linux-0.0.9-Z1E-Optimized-Turbo-RDNA-3.tar.gz`.
Use the normal installer with your own DLL/models. No game launch variable is
needed. To roll back, reinstall the preserved exact archive without `-Fast-` or
`-Turbo-`. No game installation was changed during testing.

A full rebuild is also supported; for example:

```sh
bash fetch_deps.sh
NR_ARCH=x86_64 bash linux/build/build_vulkan_loader.sh
NR_GPU=rdna3 NR_ARCH=x86_64 NR_SHADER_PROFILE=z1-extreme-fast \
  NR_VERSION=0.0.9 bash linux/build/build_package.sh
```

Use `z1-extreme-turbo` for the other trial. RDNA4 is not built by these commands.
No release/tag or archive upload is part of this delivery; main source commits
use `[skip ci]` to suppress the existing multi-GPU packaging workflow.
