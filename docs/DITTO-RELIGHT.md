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
