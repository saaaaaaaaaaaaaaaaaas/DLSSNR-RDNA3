# Interleaved Fast passes and causal temporal reconstruction

Baseline `51fc51d`: the owner reports severe ghosting and intermittent shadow/color
shifts while turning the camera with `NRFastInterleavedPasses=3` and Interleaved
preset. Test both presets, with Lighting off unless explicitly stated.

| Area | Acceptance |
| --- | --- |
| Vector age | A pass called every K real frames must reproject its own temporal history by K composed real-frame backward vectors, not the current frame alone. |
| Network history | A temporal skip must not write an invented NN history; the next full network call follows the existing causal gate. |
| Cadence | No extra inference during steady-state turns, invalid vectors or generated frames; document the existing multi-pass reset behavior separately. |
| Motion | Test actual Vulkan Interleaved and temporal code together on static and moving fixtures, including three slots, N=2/3/4, cuts and silhouettes. Compare to the original implementation. |
| Regressions | Temporal-off output and ordinary single-pass paths remain unchanged; Vulkan synchronization validation stays clean. |
| Delivery | RDNA3 Z1 Extreme Turbo only; model-free local package and local commits. Actual game validation is separate. |

## Cause and implementation

On a normal displayed frame only one Interleaved slot is evaluated. Each slot's
`SeamlessHistory` therefore advances only once per slot cycle, but the original
integration passed the current frame's one-frame vector to that history. With
three slots, the stored residual and guide are usually three frames old. The
one-frame lookup can put shadows and colors in the wrong screen positions.

When causal reconstruction is active, the stage now composes the backward
vectors from the current pixel through each intervening real frame, and passes
that vector to the selected slot's runtime. Each intermediate vector is read
at the position reached by the previous step. The vector ring scales to 2–8
slots. A non-finite or off-screen trajectory is invalidated; it is not clamped
onto an unrelated edge. The stage retains its existing per-frame motion and
surface validation for moving and filling the individual increments. The
network sees its own slot history in the same coordinate system. The existing
causal gate prevents an invented neural history from being fed to the next full
inference after a skipped inference.

With an odd number of Interleaved slots and temporal interval 2, each slot's
first full inference is preserved but subsequent slot phases are staggered. For
three slots this changes the steady-state pattern from `SSS FFF SSS FFF` to
`S F S F S F` (S = reconstruction, F = full inference). A reset retains the
existing Interleaved initialization: it visits every slot, and does not inject
an extra inference merely for motion or a rejected pixel. Consequently startup
or a cut can have an irregular cadence. Other interval/slot combinations retain
their existing per-slot phase rules. Generated frames do not advance them.

Changing this mode invalidates the stage's prior Interleaved history, avoiding
mixed one-frame and multi-frame vectors. With temporal reconstruction off, the
stage continues to create its previous one-frame network history. Interleaved
also keeps depth in the required Vulkan layout across its dispatches and gives
the network its requested depth layout while it runs. The diagnostic GPU query
ring has room for eight reset-time passes recorded in one command buffer.

`renodx_il_mv.spv` has a new 18-binding / 48-byte push ABI; the host rejects the
old shader. Update runtime consumers and shaders together. The user-facing
setting is still **Passes (fast) → Interleaved**, not the removed Fast GPU build
profile. The RDNA3 learned kernels, vendor RenoDX shaders and Windows code are
unchanged. This fix targets the RenoDX-style NR Fast route used by
`NRFastInterleavedPasses`; wilsjo2 layer Fast has a separate implementation.

## Verification on Z1 Extreme

Actual `CC_SMALL_FP8` Turbo, Lighting off, procedural Motion 640x360,
24 displayed frames, a cut at 13 and invalid vectors at 15. Full NR on the same
Interleaved preset is the reference, not game ground truth. Both presets and
N=2/3/4 pass synchronization validation. Slot counts 2, 3 and 8 have actual
model runs. Full-NR images with temporal reconstruction off match the previous
implementation byte for byte across 48 frames, both presets. The N=2 test
checks the exact alternating inference sequence on frames 1–12, before the cut.

| Interleaved 3 / N=2, frames 4–23 | Before | After |
| --- | ---: | ---: |
| Preset 1 mean RGB RMSE, 8-bit codes | 2.574 | 2.065 |
| Preset 2 mean RGB RMSE, 8-bit codes | 2.763 | 2.041 |
| Preset 1 exposed positive-blue error, 8-bit codes | 1.269 | 1.677 |
| Preset 2 exposed positive-blue error, 8-bit codes | 1.140 | 1.512 |

Overall error decreased on this fixture but some exposed edges have a small
increase in blue error. Turning off Interleaved's fill made both metrics worse,
so that experiment is not shipped. The attached in-game screenshots and the
owner's camera motion remain the final test: these measurements cannot prove
that all game-specific ghosting is gone.

`interleaved_motion_vectors.cpp` independently exercises the shipped Vulkan
shader at ages 2–8 using varying spatially-dependent vectors. It checks the
composed coordinates, non-finite motion and trajectories leaving the image;
all pass with synchronization validation on RADV PHOENIX. No new learned model
pass, CPU readback, present queue or wait is added to the production path.

## Local package and 720p timing

Artifact built from commit `ee2f4a456685c64cb041a3b15c0fac4a7ecd3dd3`:

```text
artifacts/fast-temporal/release/DLSS-NR-AMD-Vulcan-Linux-0.0.9-z1e.2-1-gee2f4a4-Z1E-Turbo-Temporal-RDNA-3.tar.gz
SHA-256 d40dbcdaa4bde60dc39bf39a332a1026d83033f2e0904ed6bca1bb0bb8b50a81
```

The model-free archive passes the package audit (1203 files, 442 validated
SPIR-V shaders); its Interleaved motion shader is byte-identical to the GPU
fixture shader. All 370 learned/grade shaders match the preceding release.
The archive and adjacent checksum/manifest are local, as are the commits. No
RDNA4 target was built. The stricter audit and a negative GPU test reject the
old motion shader's ABI. Reverse Z and three generated-frame notifications per
real frame pass with Vulkan synchronization validation.

For a coarse cost check, two interleaved rounds of the actual `CC_SMALL_FP8`
model used 1280x720 Motion, three Interleaved passes, preset 1 and interval 2,
with Lighting off. GPU timestamp means over frames 12–23 were 53.76 and 54.14 ms
before, 54.07 and 54.40 ms after. The mean difference is about +0.29 ms, close
to the run-to-run range. Automatic clocks, the fixture's idle gaps and its cut
limit this comparison; it is not a game FPS measurement.
