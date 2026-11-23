# Ditto Relight / DittoSR 0.5.4

Vendor HLSL/HLSLI files are unmodified from tag `v0.5.4`, resolved to
[`b241aa43c45e11e3942a18dc1004dc9e5ba3ee3f`](https://github.com/Vancete/DittoSR/tree/b241aa43c45e11e3942a18dc1004dc9e5ba3ee3f).
`prepare.comp` and `residual.comp` are this integration's shaders.

The API-neutral settings and constant builders in `linux/src/core/dittosr/`
retain the same defaults and layouts. `dittosr-legacy/` preserves the previous
v0.4.0 shaders/settings for the unchanged RDNA4 path; it is not built for RDNA3.

The RDNA3 integration runs FP32 SR on an encoded signed log-ratio edit, not on
the whole neural image. Full-size color/depth guide its final reconstruction;
NR's original full-size composition retains source texture and alpha.
Relight operates on the full-size reference before NR encoding, with its main
lighting/filter/history passes at half size, then depth/normal-aware upsampling.
Lighting resolution is independent of model resolution. The curve is baked.

MIT: `LICENSE`; Qualcomm GSR2 BSD: `licenses/LICENSE-snapdragon-gsr.txt`;
CORTAO MIT: `licenses/LICENSE-cortao.txt`; FidelityFX CACAO MIT:
`licenses/LICENSE-fidelityfx-sdk.txt`. All four notices accompany the binaries.
See `docs/DITTO-RELIGHT.md` for controls, qualification and limitations.
