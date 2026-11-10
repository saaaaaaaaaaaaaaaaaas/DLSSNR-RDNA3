# Ditto SR

`common.hlsli`, `convert.hlsl`, `motion.hlsl`, `upscale.hlsl` and `sharpen.hlsl`
are unmodified from [DittoSR v0.4.0](https://github.com/Vancete/DittoSR/tree/d37519ce85a84a4cc3083cad17a83335bee0f6d4).
The host's settings and constant builder in `linux/src/core/dittosr/` come from
the same source, with the D3D12 dependencies removed. DittoSR is MIT licensed;
the GSR 2 attribution and BSD terms are in `licenses/`. The NR package carries
both notices next to the compiled shaders.

`prepare.comp` belongs to this integration. It maps the game's guide subrects
to NR's grid and rephases Ditto's reconstruction for the next SR stage.
Motion remains in normalized screen units; jitter reaches Ditto in NR pixels.
Ditto consumes the network's encoded neural image after optional look shaping
and writes `up_neural`. The existing `up_proxy` and full-resolution codec resolve
are retained. Each model pass has its own history. Game HDR/exposure is handled
by NR's codec, without applying it a second time to the encoded neural image.

The Vulkan path uses the reference's FP32 arithmetic and RGBA16F history, with
four separate passes. Its D3D12-only fused sharpening, 10-bit history and
anti-shimmer variants are not enabled here. Slang is pinned by fetch_deps.sh.
