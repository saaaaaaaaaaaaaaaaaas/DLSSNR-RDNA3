# Audited Turbo temporal package

Prepared locally on 2026-11-21 for Linux/Proton on ROG Ally Z1 Extreme.

- Archive: `linux/package/DLSS-NR-AMD-Vulcan-Linux-0.0.9-Z1E-Turbo-Temporal-RDNA-3.tar.gz`
- Size: **271,530,741 bytes** (259 MiB).
- SHA-256: `0f47b8465f95c031cee63fc7239adf829359dcfa0ef3905529c44bf162e25cac`
- Adjacent `.tar.gz.sha256`: `sha256sum -c` passes.
- `check_package.py`: **PASS, 1,164 files**, no model containers, unexpected
  tensor files or known NVIDIA DLLs.
- All archive entries match staging byte for byte. All **517 profile files**
  match the validated builds; all six markers select `z1-extreme-turbo`.
- The four old runtime reuse shaders are absent. Fast is removed from the build
  profiles and active archives. Historical research/validation records remain.
- The reference's MIT license is included at
  `dlssnr-amd/shaders/runtime/Seamless-MIT-LICENSE.txt`.

The package defaults to `[Temporal] Enabled=1, Interval=2`, also exposed in NR
Control's Network page. `Enabled=0` runs full NR on every real frame. Use the
normal installer with your own DLL/models; no game launch variable is needed.
`Turbo-Temporal.txt` explains the change. The old exact/Turbo packages may be
reinstalled for rollback. The intermediate build archive was removed.

## Component provenance

The full package build, including its loader, ReShade add-ons, ASI, all six
networks and INT4 tools/layer, completes successfully. Full host source is
`179ca16`; only `nr_seamless.hpp` changes between that source and `ac2eba9`.
The three runtime translation units are rebuilt and their consumers relinked
at `ac2eba9` with the same architecture/route/INT4/version flags. All other
production C++ and data sources are identical. The internal version is
`0.0.9-Turbo-Temporal`. Packaged qualification documents are from `81d7a50`.
The outer archive is given the shorter filename above after assembly.

| Runtime consumer | SHA-256 |
| --- | --- |
| `reshade/dlssnr_amd.addon64` | `269d61719b9e30004b14638b7da67d651269ab2d0aa9c6440dd99d94680df5b9` |
| `int4/reshade/dlssnr_amd.addon64` | `bf96819469ec02764904c912a43d58ee6f6fecc2101c742c941c0dfa6a71095d` |
| `optiscaler/asi/dlssnr-amd.asi` | `93b9e8f719e7833f8120fec3a5c79f0bc201c5972203260503504f5e4097788e` |

All three consumers link successfully and have no static `vulkan-1.dll` import.
The local audit records file hashes and inputs in `build/seamless/package-audit.json`.
Build logs are `build-package-final.log` and `relink-{base,int4,asi}.log` there.

## Verification and limits

See [implementation and Motion results](RDNA3-SEAMLESS.md) and the
[machine-readable record](RDNA3-SEAMLESS-VALIDATION.json). On the procedural
Motion 720p fixture, median complete NR stage time is 76.86 ms for full NR and
40.25 ms for N=2 (**47.6% lower**). Reconstruction costs 2.48 ms there.
The thin-object blue error is substantially reduced in this fixture.

N=2/3/4 cadence, generated-frame notifications, rotation, cuts, invalid motion,
reversed Z, absent depth, odd extent, multiple features and runtime recreation
are GPU-tested. All seven network modes pass selection/cadence smoke checks.
All 422 SPIR-V files validate. The 13 full-NR regression outputs and both strict
logs remain identical to before; Turbo's historical-golden FAILs are unchanged.

These are harness results, not game FPS or measured physical input latency.
Actual game/OptiScaler/FG x4 presentation, HDR/PQ and game DRS qualification
remain pending. No models or raw private test outputs are included.

No RDNA4 network, Fast build, release tag, release creation or archive upload
is part of this delivery. Source is integrated into the requested main branch
with `[skip ci]` so the multi-GPU packaging/upload workflow is suppressed.
