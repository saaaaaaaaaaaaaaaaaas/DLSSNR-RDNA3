# Audited Z1 Extreme trial package

Built and audited on 2026-11-16 for Linux/Proton on ROG Ally Z1 Extreme.
Production code: `72a157f`. Packaged qualification documentation: `f5ccee4`.
No release tag was created.

- Archive: `linux/package/DLSSNR-AMD-Vulkan-Linux-0.0.9-z1-preview-g72a157f-x86_64-rdna3-z1-extreme.tar.gz`
- Size: 273,185,228 bytes.
- SHA-256: `9d13e81a8d226e6aa770f332210bce018e43bbcc216dc9d51fcb8f469431e662`
- The adjacent `.tar.gz.sha256` passes `sha256sum -c`.
- `check_package.py`: PASS, 1,165 files; no model containers, unexpected tensors
  or known NVIDIA DLLs. Model extraction tools and source-built forwarders remain.
- All six shader directories carry `gpu-profile.txt = z1-extreme`.
- All **520 packaged profile files** match the validated local builds byte for byte,
  including SPIR-V, markers and temporal passes. Packaged qualification documents
  match commit `f5ccee4`.

Build:

```sh
bash fetch_deps.sh
NR_ARCH=x86_64 bash linux/build/build_vulkan_loader.sh
NR_GPU=rdna3 NR_ARCH=x86_64 NR_SHADER_PROFILE=z1-extreme \
  NR_VERSION=0.0.9-z1-preview-g72a157f bash linux/build/build_package.sh
```

The archive already selects the target profile; no game launch variable is needed.
Use its normal installer and your own extracted models/NVIDIA DLL. The actual game
installation and user settings were not modified during this work.

See [measurements and checks](RDNA3-Z1-EXTREME.md) and the
[machine-readable validation record](RDNA3-Z1-VALIDATION.json). Historical strict
frame-golden failures remain unchanged; optimized/fitted independent references
and real game/FG/HUD/resize qualification remain separate work. The measured
2.4–4.5% reduction is isolated Silver/Small network time, not a game FPS result.

Local build/audit logs: `build/z1/build-package.log`, `build/z1/package-audit.json`
and `build/z1/build-loader.log`. Private models and raw test outputs stay in ignored
build directories and are not in this archive or Git.
