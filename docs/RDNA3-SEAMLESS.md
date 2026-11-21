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
