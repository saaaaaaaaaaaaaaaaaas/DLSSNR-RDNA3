# RenoDX Network controls up to 4

Baseline: `244e24a`. The owner requests the four RenoDX Network sliders
Intensity, Local structure, Local tone and Skin structure up to 4.00.

| Area | Acceptance |
| --- | --- |
| UI / INI | RDNA3 RenoDX Network controls reach 4.00; INI parsing and serialization preserve 0..4 (-1..4 for skin) with unchanged defaults. |
| Runtime | All four values actually reach the model's existing push constants; normal non-RenoDX and RDNA4 limits stay unchanged. |
| Composition | Intensity uses the existing bounded post-block and Interleaved composition; no learned shader changes. Values above 2 are an extended artistic range, not a claim of original model calibration. |
| Masks | Skin control remains conditional on Auto mask. Independent skin continues to cap the global structure sent to the network at 1 when Auto mask is on; turning Independent skin off permits the full global range. |
| Verification | Boundary parser/UI tests, actual Z1 Extreme model output at 2 and 4 for each setting, defaults unchanged, finite outputs, temporal/Interleaved smoke and archive audit. |
| Delivery | Local work/commits and RDNA3 Turbo package only. No RDNA4 build or publication. |

## Behavior

The four **RenoDX → Network** values reach 4.00 in the RDNA3 build; the -1
sentinel for skin and all defaults are retained. The extension also applies to
RenoDX layers using these same four keys. Existing per-pass overrides and other
routes retain their own ranges. The runtime's extended validation is enabled
only by the RDNA3 RenoDX route. Full-precision push constants reach the existing
network and its existing bounded composition; no learned or RenoDX vendor
shader was changed. Intensity above 1 uses the existing bounded luminance-ratio
mapping, so the visual change is not an unbounded linear multiplier.

The network's mask semantics are unchanged: Skin structure needs Auto mask.
With **Independent skin** and Auto mask on, Local structure is deliberately
limited to 1 at the model boundary to keep skin independent. Set Independent
skin off to pass Local structure above 1 while Auto mask is enabled. Without
Auto mask the full Local structure range is already used.

## Verification

- The actual Windows config parser, UI key metadata and write/read INI round
  trip pass under Wine with 4.00 for all four, -1 for skin and rejection of
  4.01. Existing default values and other UI/INI ranges are unchanged.
- On Z1 Extreme / RADV, `CC_SMALL_FP8` Turbo, each setting at 4.00 changed the
  GPU output compared with 2.00, with no NaN/Inf or Vulkan validation errors.
  The 640x360 fixture's mean absolute RGB8 differences were: intensity 1.55,
  tone 5.90, structure 2.35, skin 0.44 code values. These are measurements of
  that fixture, not a guarantee of a desired appearance on every scene.
- The original defaults are byte-identical to the prior runtime. Without the
  RenoDX extended flag, the original runtime range still rejects Intensity 4.
- Interleaved 3 with temporal N=2, all four at 4, including a reset and FG
  notifications, completed with finite output and clean Vulkan validation.
  These tests do not qualify a user's game, HDR presentation or all models.
- A separate Fast Interleaved N=2 comparison at Intensity 2 versus 4 changed
  550,916 RGB channels in frame 2, confirming that Interleaved's own
  composition also uses the expanded intensity.
