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

## Checks so far

All six generic and exact-Z1 shader sets rebuilt byte-identically: 514 + 520
files. All 850 Fast/Turbo SPIR-V files across six networks pass `spirv-val`.
Each approximate network changes only its 18 selected SPIR-V files and GPU
profile marker; host constants and derived INT4 shader bytes are unchanged.

The initial strict Silver/Small checks retain their known historical golden
failures. Matching before/after baseline results, quality scores, repeated timings
and delivery evidence will be recorded after the final runs. No historical golden
is replaced to accept an approximation.
