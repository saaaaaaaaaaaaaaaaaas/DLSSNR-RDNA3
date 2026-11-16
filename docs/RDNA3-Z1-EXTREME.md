# Z1 Extreme optimization of 0.0.9

Source: `c518f6508dac6ffacda96331ec6df56e6ef7a281`, branch
`perf/z1-extreme-0.0.9`, separate worktree `DLSSNR-RDNA3-Z1`.
Target: ROG Ally RC71L, AMD Ryzen Z1 Extreme (RADV PHOENIX, device 1002:15bf),
Mesa 26.2.6-arch3.1. Linux/Proton only. Existing game installations are read only.

## Acceptance matrix (before implementation)

| Area | Intended change | Required evidence |
| --- | --- | --- |
| Persistent scheduling | Measure workgroup caps on the 12-CU APU; retain a change only with a repeatable full-network benefit | Interleaved baseline/candidate warm runs at 720p/900p/1080p, median and range; retain environment metadata |
| WMMA shaders | Compare token/head wave distributions and the C=128 straggler threshold | Compile with pinned glslang; test candidates on target; unchanged arithmetic must produce identical frame/layer values |
| Precision | Preserve baseline network arithmetic and weights | Strict Silver/Small checks before and after; distinguish inherited historical-golden failures from before/after changes; do not rewrite goldens |
| Isolation | Select target tuning only for the Z1 Extreme; preserve other RDNA3 and RDNA4 devices | CPU selection/override checks and baseline SPIR-V comparison where shader source is unchanged |
| Benchmark tools | Make target-resolution comparisons reproducible and reject failed or empty runs | Explicit network/model, warmup/repeats/timeout, JSON results and correct failure propagation |
| Delivery | Build a model-free Linux/Proton candidate and document exact measured scope | Local build/static checks, archive audit, focused Git commits and review branch |

## Measurement notes

GPU access requires execution outside the managed sandbox. Disable implicit
Vulkan layers only for test processes with `VK_LOADER_LAYERS_DISABLE='~implicit~'`.
Do not change global drivers, clocks, power limits or services. Record the current
power/clock state; do not label results as fixed 15/20/30 W without controlling it.
An isolated network timing is not a game FPS measurement.

ASUS specifies a 1920x1080 panel for the original Ally; 1280x720 and 1600x900
are useful 16:9 reduced-resolution cases. 1280x800 is an additional test extent,
not the Ally's native panel resolution. See [ASUS specifications](https://rog.asus.com/us/gaming-handhelds/rog-ally/rog-ally-2023/spec/).
The existing FP16 path already uses wave32 WMMA; operand replication requirements
are described in [AMD's WMMA guide](https://gpuopen.com/learn/wmma_on_rdna3/).

## Implementation

Use `NR_SHADER_PROFILE=z1-extreme` with `NR_GPU=rdna3` to build a separate target
package. It changes two pipelines, after applying each model's overlay:

- `attn`: `NR_APIPE=1` rather than 2. Fewer live FP16 operand fragments reduce
  register pressure on gfx11; the WMMA operations keep the same order.
- `gemmproj`: `NR_A_LDSV=1`. Waves sharing the same token fragments load a chunk
  once into LDS rather than each fetching it from global memory. The K order,
  tile geometry, weight layout and quantization boundaries stay the same.

The profile does not propagate to derived INT4 kernels such as `attn4` and
`gemmproj4s`. Their code, host constants and temporal passes stay byte-identical.
All six current network choices have a compiled profile. Baseline Silver and
Small share their shader directory. The legacy INT4 check is a separate network.

This is an explicit Linux/Proton target build. There is no runtime device guess,
Windows ACO change, driver change or automatic network/quality selection.
Generic RDNA3 and RDNA4 build behavior is preserved. The profile's SPIR-V files
are separate under `build/linux/rdna3/network[-<overlay>]-z1-extreme`; installed
profile directories carry `gpu-profile.txt`. The package filename ends in
`-rdna3-z1-extreme.tar.gz`. To roll back, reinstall the generic 0.0.9 package.
Explicit package selection replaces the initial automatic-selection idea in
the matrix: it keeps generic shaders untouched and makes the exact configuration
reviewable. The profile is rejected for RDNA4 and the legacy INT4 harness.

The CLI profiler now selects hwmon by the actual Vulkan physical device's vendor
and device ID; it previously searched only for RX 9070 XT (1002:7550).

## Reproduce the comparison

```sh
bash fetch_deps.sh
python3 linux/test/check_rdna3.py --model /private/CC_SILVER_AARDWOLD.bin
NR_SHADER_PROFILE=z1-extreme python3 linux/test/check_rdna3.py --model /private/CC_SILVER_AARDWOLD.bin
NR_SHADER_PROFILE=z1-extreme python3 linux/test/check_rdna3_small.py --model /private/CC_SMALL_FP8.bin
python3 linux/test/check_rdna3_profiles.py build/linux/rdna3/network build/linux/rdna3/network-z1-extreme
```

Existing strict historical-golden failures on this host remain failures; do not
use `--update-golden` to hide them. Compare before/after output bytes separately.
The Small checker must be built once for its planner; the common `nr_graph`
executable can run either native plan.

Create `build/z1-cases.json`:

```json
{
  "baseline": {"spv_dir": "build/linux/rdna3/network"},
  "z1-extreme": {"spv_dir": "build/linux/rdna3/network-z1-extreme"}
}
```

```sh
VK_LOADER_LAYERS_DISABLE='~implicit~' python3 linux/test/bench_rdna3.py \
  --model /private/CC_SILVER_AARDWOLD.bin --resolution 720p \
  --cases build/z1-cases.json --rounds 4 --warmup 30 --repeats 40 \
  --out build/z1-comparison-720p
```

Repeat for `900p` and `1080p`. For Small add `--model-kind small` and its own
model. Arbitrary extents such as `1280x800` are supported. The input is the
supplied 1080p frame resized with Lanczos, stored in the result directory.
The harness records raw logs, RGBA32F outputs, model/input hashes, GPU timing,
per-family totals, clocks, power and temperatures. Case order reverses each
round. A failed process, timeout, empty timings, dependency-wait error, invalid
output or byte difference cannot yield a successful comparison. Timing and
equality against the first case are separate from independent model acceptance.

## Target timings

GPU timestamps for the isolated network on the actual Ally, 2026-11-16.
Four alternating runs per case (AB, BA, AB, BA), each with 30 warmup and 40
measured frames. Values are medians; they exclude model/pipeline creation.
No game was running as part of this test. Desktop applications remained open.
Automatic clocks and power control stayed in effect; these are not fixed-power
15/20/30 W results and not a measure of game FPS.

| Network | Extent | Generic, ms | Z1, ms | Time saved |
| --- | --- | ---: | ---: | ---: |
| Silver | 1280x720 | 75.104 | 73.297 | 2.4% |
| Silver | 1600x900 | 112.976 | 109.812 | 2.8% |
| Silver | 1920x1080 | 156.911 | 151.747 | 3.3% |
| Small | 1280x720 | 41.840 | 40.802 | 2.5% |
| Small | 1920x1080 | 91.202 | 87.121 | 4.5% |

All **40 RGBA32F outputs** are byte-identical to their matching generic case.
Small 1080p ranges: generic 91.017..91.581 ms, Z1 87.041..87.359 ms.
Silver 1080p: generic 156.673..157.228 ms, Z1 151.438..151.904 ms.
The first Silver 720p baseline was cooler/faster; the median uses all four runs.
Per-run clocks, reported power, temperatures and output hashes are preserved in
[the validation record](RDNA3-Z1-VALIDATION.json). Raw logs, exact inputs and
outputs are under the ignored `build/z1/final-*` directories.

## Rejected experiments

- Persistent caps scaled down to the APU, per-width workgroup counts, and
  C=128 `NR_YOUNG_CUS=12/24/48/64`: no convincing repeatable full-network gain.
  The 24/12/6 cap experiment produced tile-counter wait failures and was rejected.
- C=32 `NR_FWAVES=1/4`, including one-wave input/output: slower or within run drift.
- Multi-head token splitting: the current gfx11 port explicitly rejects it;
  it cannot be enabled by just changing `NR_HWAVES`. Porting its operand-fragment
  indexing would be separate shader work, requiring full numerical validation.
- Attention prefetch depth 3, shared weight staging in the wide GEMMs, smaller
  projection K chunks and removal of LDS occupancy padding: no better than the
  selected profile. Prefetch depth 3 made attention substantially slower.

The `NR_YOUNG_CUS` value alone is not a CU count correction: one-item persistent
runs already disable straggler routing at some smaller extents. No scheduler,
precision, model-scale, INT4 calibration or temporal reuse setting was changed.

## Build checks

Pinned glslang 16.5.0 builds all six generic and target shader variants.
All **514 generic files** across those variants match the existing 0.0.9 build
byte for byte. Each target variant changes only `g_attn.spv`, `g_gemmproj.spv`
and its new `gpu-profile.txt`; all host markers, temporal passes and derived
INT4 shaders remain identical. `check_rdna3_profiles.py` enforces this boundary.
`spirv-val --target-env vulkan1.3` passes on all **425 target SPIR-V files**.
The existing CPU port checks pass, including descriptors, presentation, Ultra
QKV routing at five extents and generated INI defaults.

The original and modified builders produce **76 identical RDNA4 files** from
the same unchanged RDNA4 sources and pinned compiler. Invalid RDNA4/legacy
profile combinations fail before output directories are changed.

## Runtime regression

Silver and Small strict checks ran before and after on Z1 Extreme. All **13
RGBA8 outputs** match byte for byte: two runs each at 1080p, 1440p and 4K for
each model, plus Silver's in-place FP16 engine route. Both models' layer hashes
remain identical. FP8 quantization, rounding and WMMA lane tests pass before
and after. No golden file or model was changed.

Both strict scripts still exit 1 for their inherited historical frame-golden
differences, with exactly the same scores/verdicts before and after. Silver:
45.62/47.93/49.11 dB against NVIDIA; Small: 49.43/49.55/50.62 dB. This profile
does not resolve those historical differences or establish game/FG/HUD/resize
qualification. See `build/z1/{before,after}/` and the validation JSON.

Optimized-network and package evidence will be recorded after completion.
