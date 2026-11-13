# RDNA3 Linux/Proton 0.0.9 target checks

This candidate requires target GPU qualification. CPU checks and shader builds
do not establish numerical accuracy, frame time or game compatibility.

## Installation and rollback

1. Exit the game. Back up the current plugin, `dlssnr-amd/`, INI and launch options
   outside the game directory. Use a fresh package directory for this candidate.
2. Verify the adjacent archive checksum with `sha256sum -c <archive>.sha256`.
3. Run `bash install.sh "/path/to/game" --dll "/path/to/your/nvngx_dlssnr.dll"`.
   Select the OptiScaler route and copy the installer’s complete launch options.
   Your own 310.9.16 DLL is needed for both base models and fitted variants.
4. Review old INIs before reuse. `NRFastMethod` / `FastMethod` now mean:
   0 Interleaved, 1 Extrapolation, 2 Feedback, 3 Hybrid, 4 Edit strength.
   In 0.0.8.3 these were 0 Extrapolation, 1 Feedback, 2 Hybrid, 3 Edit strength.
   The new OptiScaler default is Interleaved preset 1 when fast passes are selected;
   ReShade defaults to Extrapolation. Check every layer section too.
5. Roll back by exiting the game and restoring the entire backup and launch
   options. Do not mix an old ASI with new shaders or only replace Ditto shaders.

## Numerical checks

On idle gfx11 hardware, from the source checkout after `bash fetch_deps.sh`, use
models extracted from your own DLL. The runners obtain their verification frames
as described in the existing test scripts; the Python dependencies listed in
`linux/test/check_rdna3.py` must be installed in your own test environment.

```sh
python3 linux/test/check_rdna3.py --model /private/CC_SILVER_AARDWOLD.bin
python3 linux/test/check_rdna3_small.py --model /private/CC_SMALL_FP8.bin
python3 linux/test/check_rdna3_int4.py --model /private/CC_SILVER_AARDWOLD.bin
python3 linux/test/check_rdna3_variants.py --installed /path/to/installed/game \
    --reference /private/approved-gfx11-reference --out build/variants-0.0.9
```

Run the baseline, small and legacy checks on both `88d8c9c` and the port commit,
using separate worktrees/toolchains. Arithmetic-preserving paths must remain
EXACT. Keep the checked-in goldens unchanged. Use 32-bit history when comparing
old history behavior. Legacy INT4 is a test-only network, not optimized Ultra.
Variant checks must use independently approved outputs for the same network and
level. Without `--reference` they emit candidates and exit 2 (unverified).

## Game acceptance

Record the GPU, Mesa, kernel, exact Proton build, game build, resolution, SR/FG
provider and multiplier, selected network/level, and the complete INI.

| Scenario | Required result |
| --- | --- |
| Small, Silver, experimental and optimized Quality/Balanced/Performance/Ultra | Correct selected network, no silent fallback or green image; matching model hashes |
| Optimized Ultra with plain encoder/decoder dispatches | `int4 QKV guard` reports consistency; no missing-table error; approved numerical reference |
| Both processing styles, legacy passes 1/2, all five fast methods | Correct method and strength, no green output; reset and scene-cut recovery |
| Interleaved presets 1/2 | Test with valid motion/depth, camera motion, disocclusion and scene cuts; unavailable vectors produce the documented status |
| Resize, dynamic resolution, model resolution and SR changes | Stable output before/during/after queued rebuilds; no slowdown persisting until restart |
| FG off/on, HUD FIX off/auto/captured, menu open during resize | NR applied at the correct cadence; stable HUD/menu, including XeSS MFG XMX if used |
| Ditto SR before SR at 33/50/100%, SDR/HDR, preprocess off/on | Neural reconstructed before normal composition; jitter/guides correct; switching/reset works |
| NR Control under Gamescope/Wayland | Cursor visible while open; game input restored after closing; optional OptiScaler key passthrough works |
| Extended run and memory pressure | No resource growth, GPU fault or persistent degraded state; record GPU time and VRAM/UMA usage |

Collect `dlssnr-amd.log`, `dlssnr-amd-asi.log`, installer log, diagnostic zip,
test reports and comparison frames. Keep models and NVIDIA DLLs private.
For reported regressions include exact reproduction steps and the last working
commit. Existing Z1 Extreme/DD2 observations in the 0.0.8.3 report are historical,
not evidence that this candidate fixes them.
