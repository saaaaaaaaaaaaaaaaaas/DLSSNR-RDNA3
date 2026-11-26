# Adaptive neural edit reconstruction

Baseline `c1cbcf9`. Owner requests separate adaptive reinforcement of the neural
edit, better full-resolution transport at 25–50% model resolution, and Ditto SR
sharpness level 1.0 by default. Work is local RDNA3 Turbo only.

| Area | Acceptance |
| --- | --- |
| Gain | Separate low-frequency lighting/color and fine neural-edit gain; confidence attenuates extra amplification on motion, disocclusion and incompatible surfaces. Expose intuitive controls, including unity. |
| Transport | Use full-resolution reference and geometry to guide the neural edit without replacing original detail or inventing high-frequency structure. Preserve alpha, codec, full-NR cadence and history lifecycle. |
| Ditto sharpness | Default level 1.0 must actually be used by the reconstruction. Test it separately from Lighting, which can be off. |
| Fidelity | Uniform/identity edits, darks, HDR/SDR, forward/reverse Z, cut and invalid vector handling stay finite. Compare neutral controls against the previous transport and quantify tradeoffs. |
| Motion | Test static, pan/rotation, disocclusion and thin moving features at 25%, 33%, 50% and full-res, with real model and procedural oracles where available. No unscheduled NR. |
| ABI | New shader and host descriptors/push constants must agree; reject a mismatched old shader and audit package. |
| Delivery | Local commits and model-free RDNA3 Turbo package. No RDNA4 compilation or remote publication. |

## Implementation

Ditto's encoded, bounded signed log-ratio edit is separated into a broad
surface-matched component and a fine component. Their individually adjustable
extra edits are bounded and reduced when the full-resolution motion, surface
match or current/history disagreement indicates less reliable reconstruction.
The full-resolution color and depth guidance, current-edit clamp, original
texture, codec and alpha remain in the transport. Controls at 1, 1 and sharpness
0 reproduce the previous path. Ditto's RCAS pass operates on the edit; it does
not sharpen the whole frame. No network weights or learned kernels were changed.

## Local verification on 2026-11-26

- AMD Ryzen Z1 Extreme / RADV, Vulkan synchronization validation: procedural
  321x181 motion and static scenes at 25%, 33%, 50% and full scale; moving
  thin feature, disocclusion, pan/rotation, cut, forward and reversed depth,
  identity edit, original alpha and Lighting off. Separate tests confirm light,
  detail and sharpness controls each change the result with Lighting off.
- Actual CC_SMALL_FP8 model, 640x360 motion scene, 24 real frames, scales 25%,
  33%, 50%, temporal interval 2, cut and simulated FG x4 cadence. Previous,
  neutral and new-default variants all passed synchronization validation; no
  extra network dispatches. At output RGBA8 precision, neutral matched the
  old path on all frames at all scales. Mean absolute network-edit magnitude
  increased from 0.01243 to 0.01488, 0.01410 to 0.01705, and 0.00917 to
  0.01101 respectively. RMSE against an existing full-resolution, every-frame
  model run increased from 0.01434 to 0.01685, 0.01531 to 0.01851, and
  0.01020 to 0.01183; this deliberately stronger look is not evidence of more
  accurate full-resolution output.
- Passes Fast Interleaved, 3 passes, 33%, interval 2, motion/cut and simulated
  FG x4 passed on the actual model with synchronization validation.
- Two short alternating 720p/33% runs, interval 2, Lighting off: mean wall time
  per frame was 10.13 ms old and 10.47 ms new (48 timed frames each run). This
  is a local synthetic comparison, not a game FPS prediction.
- Wine round-trip exercised the actual RenoDX metadata, parser and INI writer;
  shader ABI audit rejects the old residual and accepts the new one.

The tests are synthetic. Game presentation, visual tuning, HDR10/PQ, real FG
and the resulting ghosting or flicker in a particular game remain unverified.
Local test logs and image buffers live under `build/adaptive-edit/`; private
model data remains outside Git and distribution packages.
