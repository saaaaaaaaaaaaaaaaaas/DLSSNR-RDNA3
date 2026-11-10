# Z1 Extreme trial: DLSSNR-AMD 0.0.8.3 RDNA3 port

Status: build-tested candidate. GPU numerical acceptance, frame timings, memory
usage and Dragon's Dogma 2 compatibility have not been established on homeserver.
The source includes every requested Linux runtime feature and network mode; no
new mode is being represented by legacy INT4 kernels/tables.

## Install and rollback

1. Exit the game. Back up the existing mod installation and settings outside the
   game directory: `dlssnr-amd/`, `dlssnr-amd.ini`, the DLLs installed by the old
   route, and ReShade/OptiScaler configuration. Keep the old package too. Do not
   mix DLLs, shaders or calibration tables from different packages.
2. Extract the new tar.gz into a fresh writable directory. Use your own
   `nvngx_dlssnr.dll` 310.9.16 (or its zip) to get all models. 310.8.0 alone
   cannot supply the small or small-experimental network.
3. For the OptiScaler route (DX12 Dragon's Dogma 2), from the extracted directory:

   ```sh
   GAME='/path/to/steamapps/common/Dragons Dogma 2'
   bash install.sh "$GAME" optiscaler --dll '/path/to/your/310.9.16.zip' --model CC_SMALL_FP8
   ```

   Follow the installer for the processing style. Copy its entire Steam launch
   line and prepend `PROTON_LOG=1 ` for this trial. End opens NR Control; Insert
   opens OptiScaler. The owner confirmed OptiScaler was the route used for the
   reported Dragon's Dogma 2 regressions. For other games using ReShade, select
   `reshade-feeder` and its printed launch line instead.
4. The installer reuses old `dlssnr.bin` / `dlssnr-small.bin` from the package or
   game only after their SHA-256 matches the unchanged source models. It rebuilds
   experimental/optimized and optimized INT4 from this package's deltas/tables;
   old derived INT4 is never relabelled as optimized. Confirm the installer says
   all four networks are installed, with all optimized levels available.
5. Rollback: exit the game, run `bash install.sh "$GAME" remove` from this
   package, restore the backed-up mod files and configuration, then restore the
   previous Steam launch options. Removal is not a restore of the old package.

Never share a package after the installer has cached models inside it. Keep trial
logs and screenshots separate from DLLs/models; `install.sh logs` collects logs.

## Numerical checks on idle target hardware

From the source worktree/checkout with its own `fetch_deps.sh` dependencies,
native g++, Vulkan development headers/loader, Python numpy and Pillow:

```sh
python3 linux/test/check_rdna3.py --model "$GAME/dlssnr-amd/CC_SILVER_AARDWOLD.bin"
python3 linux/test/check_rdna3_small.py --model "$GAME/dlssnr-amd/CC_SMALL_FP8.bin"
python3 linux/test/check_rdna3_int4.py --model "$GAME/dlssnr-amd/CC_SILVER_AARDWOLD.bin"
```

The third command deliberately builds the **legacy** INT4 test path with the
old tables/overlay and golden. It does not test optimized. These tests still
require EXACT; no golden or threshold has been loosened or replaced. They run
1080p, 1440p and 4K. Use the device index in `NR_GPU=<index>` only if the native
harness reports multiple AMD candidates; do not export the build selector
`NR_GPU=rdna3` into runtime commands. The Z1 Extreme's integrated device is now
accepted by the harness; the WMMA support check still runs.

Test every installed mode on the same input frames:

```sh
python3 linux/test/check_rdna3_variants.py --installed "$GAME" --sizes 1080p --out build/z1-variants
# Then include 1440p and 4k when the target has sufficient memory for those tests.
python3 linux/test/check_rdna3_variants.py --installed "$GAME" --out build/z1-variants-full
```

This verifies actual network selection (a fallback fails), output size and two
identical executions per model/level. Without an approved independent gfx11
reference it exits **2 / UNVERIFIED**, even if deterministic. `--reference DIR`
requires byte equality to the explicitly supplied `mode-size-0.rgba8` files;
it never records or approves goldens automatically.

New-network references must use exactly the target v0.0.8.3 model packs and
calibration for that network/level, identical inputs, controls and history.
Upstream v0.0.8.3 on RDNA4 is an independent implementation for diagnostic
comparisons (PSNR/SSIM against its per-mode outputs and NVIDIA outputs); gfx11
and gfx12 have different established reductions, so a cross-architecture metric
alone does not justify a new gfx11 golden. Review divergences per layer, preserve
the known baseline discrepancy, compare held-out motion clips and approve
mode-specific gfx11 goldens only after numerical review. No such reference,
quality thresholds derived from target measurements or new golden is fabricated
by this port. This numerical qualification remains outstanding.

For old temporal/history comparisons set `[Network] History Precision = 32`;
16 is upstream's intentional new default. Exercise switching both ways. Run
`--perf` / `--profile` only with the GPU idle and record clocks and repeated runs.

## Dragon's Dogma 2 acceptance matrix

Start with FG off, one layer and one pass; test RenoDX and wilsjo2 separately.
Record exact Proton CachyOS build, game build, resolution, SR mode/quality,
processing, model, level, pass mode/count, History Precision, TDP/clocks and UMA
memory setting. The reported baseline is Z1 Extreme, Mesa 26.2.5, DX12 through
Proton CachyOS Latest; the Vulkan backend runs under the game's DX12 translation.

| Scenario | Required observation / evidence |
| --- | --- |
| Small and Silver, each at one pass | Stable colours, no green image; correct model in diagnostics; compare F5 captures |
| Experimental; optimized Quality/Balanced/Performance/Ultra | No fallback; stable image; mode-specific screenshots, logs and timing |
| Switch all networks and optimized levels repeatedly | Background build completes, NR resumes on the requested network, no persistent green/stall |
| Model resolution 100 -> 75 -> 50 -> 25 -> 100% | No restart required; no green output or unbounded slowdown; build/prefetch events in log |
| Change display resolution, then SR quality/scale; repeat in reverse order | No persistent stale frame/history, correct extent in diagnostics; record recovery frames |
| Repeat resizing with NR Control open | No hang/deadlock; menu stays usable |
| Legacy passes 1 -> 2 -> 3 -> 1, then fast passes | Second and later passes remain valid; no green screen; repeat after restart; both processing styles |
| FG off/on (game FG and OptiScaler FG separately, combined if available) | Once-per-game-frame NR; consistent generated-frame appearance; no menu flicker; record FG provider |
| FG plus model/SR/resolution/pass changes, menu open/closed | No persistent low-FPS state, flicker or hang; note transient build bypass separately |
| History Precision 16 -> 32 -> 16 | Expected history reset on format change; no stale green history |
| Temporal reuse Every 0 -> 2 -> 3 -> 0, with FG off/on | Own fork feature preserved; no stale edit after resize or model change |
| Memory Guard default off; optional on under pressure | Explicit diagnostics and recovery; record system/shared memory as well as GPU usage |
| At least 30 minutes, repeated resize and model switches | No monotonic memory growth, device loss or degradation; compare memory after settling |

Use the same scene/save for timing: at least three runs per condition at the
same clocks/TDP. Record NR network time and processing time, total frame time,
frame pacing, and GPU/shared/system memory before/after each transition. A lower
single minimum is not evidence of a speedup. No RX 9070 XT memory/timing result
is claimed for this APU.

## Logs to return

- `dlssnr-amd.log`, `OptiScaler.log` (or `ReShade.log`), configuration, NR Control
  diagnostic package/statistics and `nr-report.txt`.
- `steam-<appid>.log` from `PROTON_LOG=1`; exact Proton build and `vulkaninfo --summary`.
- Before/after F5 captures or a short video of the green output/menu flicker,
  with a sequence of actions and timestamps.
- The `build/z1-variants*/` reports/logs and the existing golden-test console logs.
- A convenient archive, from the extracted package:

  ```sh
  bash install.sh "$GAME" logs
  ```

The original regressions are **unverified**, not marked fixed by compilation.
