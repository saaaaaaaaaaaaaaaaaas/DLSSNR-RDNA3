# RDNA3 Linux port of DLSSNR-AMD 0.0.9

## Pinned inputs and scope

- Fork source: `88d8c9c` (`0.0.8.3-v.3`, branch `rdna3`).
- Upstream base: `931c50fe2f6096e8ac8bf60d3536ffc70db4a846` (`v0.0.8.3`).
- Upstream target: `9503f6b180df45305b38fbd21cd9f705164f2ab1` (`v0.0.9`).
- Writable remote: `https://github.com/saaaaaaaaaaaaaaaaaas/DLSSNR-RDNA3-0.0.8.3`.
- Implementation branch: `port/rdna3-0.0.9`, in a separate worktree.
- Linux/Proton and gfx11; preserve Ditto SR, NR Control cursor fixes, temporal
  reuse, FP16 twin arenas, raw descriptors, weight packing and WMMA rewrites.
- Native Windows D3D12 passthrough and Windows ACO binaries are outside this
  Linux port. Windows sources remain at the fork's existing revision.

## Acceptance matrix (recorded before implementation)

Initial state: planned and unverified. Build checks do not prove GPU correctness.

| Upstream change | Implementation acceptance | Verification acceptance |
| --- | --- | --- |
| Fast passes: Interleaved presets 1/2, Extrapolation, Feedback, Hybrid, Edit strength | Import both processing styles, config, route defaults, shaders and UI together | Compile shaders/host; config defaults and method range; target reset/MV/FG/resize checks |
| Processing fusion and reduced-resolution resolve | Port RenoDX/preprocess changes while retaining Ditto SR reconstruction and composition | Shader compilation and source audit; target Ditto on/off, SDR/HDR and preprocess |
| History image rotation and GPU completion tracking | Retain feature ownership, precision switching and RDNA3 descriptors | Native and PE builds; target multi-feature and resize/history tests |
| Unified HUD FIX and D3D12 HUD capture | Port Linux/Proton hooks, inventory, descriptor/root/indirect tracking and UI | Build plugin/shaders; target HUD capture/FG/DX12 integration |
| Optimized Ultra QKV table guard | Generate marker; ensure gfx11 encoder-only INT4 QKV routes do not run decoder blocks | Compile all variants; inspect routing/marker; target independent Ultra reference |
| Installer, diagnostics and localization | Bring Linux config/install/log/UI changes and migration notes | Syntax, generated config, path/ImGui guards, package audit |
| Existing networks and fork features | Keep baseline/legacy arithmetic and goldens, all optimized levels, Ditto SR and pointer fixes | Baseline SPIR-V comparison, CPU descriptor/variant checks; target EXACT goldens |
| Delivery | Focused commits; model-free x86_64 RDNA3 package; build evidence | Complete package build and audit; no release tag before target checks |

## Environment and pending target checks

The managed sandbox does not expose `/dev/dri`; checking outside it revealed the
host's AMD Ryzen Z1 Extreme (RADV PHOENIX), Mesa 26.2.6-arch3.1, Vulkan 1.4.354.
The owner's existing DD2 installation contains models matching the expected
SHA-256 checksums. Tests use private copies under ignored `build/`; the game
installation is read only and no model enters a distribution package.
Frame timings and game compatibility remain unqualified.
Existing goldens must remain unchanged. New processing modes need their own
matching reference; they must not be compared to a different network's golden.

Implementation, actual checks, artifacts and target commands are recorded below
as the port progresses.

## Implementation and verification

- `ebd08be` ports the Linux delta of upstream `9503f6b` relative to `931c50f`.
  The upstream release is a single commit; the Linux-only transfer preserves
  the fork's Ditto SR, cursor handling, XeSS MFG telemetry and temporal reuse.
- Ditto's path disables the new fused upsample/decode resolve while it
  reconstructs neural output. Both Interleaved and Ditto views are retired;
  descriptor pools cover their combined sampled/storage/uniform/sampler needs.
  Shared motion/depth fields retain a single declaration.
- `a97d3a1` adapts Ultra's plain C=256 routing: encoder blocks 15-22 get
  `fswin256q`; the decoder keeps FP16 QKV and the existing INT4 expansion.
  The host's upstream QKV-table guard and compiler's `int4-qkv.txt` agree.
  Packaging copies this marker alongside the other INT4 markers.
- CPU routing checks use actual compiled markers, shipped calibration tables
  and native frame plans at 720p, 1080p, 1918x1078, 1440p and 4K. Each case
  covers six plain encoder and six plain decoder layers. Reintroducing the old
  marker/routing fails on decoder block 49, as required.
- Pinned glslang 16.5.0 compiles all six RDNA3 variants and legacy INT4, and
  all six RDNA4 variants. Pinned Slang 2026.19 builds RenoDX, the new reduced
  resolve programs, Interleaved/Preprocess and Ditto SR.
- The independently compiled `88d8c9c` baseline matches all **72** baseline
  network, temporal, runtime and marker files byte for byte. Goldens are unchanged.
- `spirv-val --target-env vulkan1.3` passes on **539** RDNA3 network/temporal/
  runtime and RenoDX/Ditto SPIR-V files. This is structural validation only.
- Existing CPU descriptor-retarget and presentation tests pass. The consolidated
  `bash linux/test/check_rdna3_port.sh` also checks generated route defaults:
  OptiScaler method 0, ReShade method 1, preset 1 and preserved `[Reuse]`.
- Shell/Python syntax and path/ImGui guards pass. The Ditto integration harness
  compiles and passes on the host's real Z1 Extreme outside the sandbox: SDR/HDR,
  preprocess, sRGB, detail/strength composition and independent pass histories.
  Half-blend error is 0.000183105; two-pass history drift is 0.00146484 with 14
  network callbacks. It uses a deterministic identity network and is not an NR
  model quality test or a game test.
- Linux RDNA4 network sources and all Windows sources remain unchanged.
  Windows continues to select its previous pinned Linux ACO source snapshot;
  it does not consume the new Linux RDNA3 pipeline, so no Windows ACO import is
  part of this port. Native Windows D3D12 passthrough remains out of scope.
- GitHub Actions now builds explicit RDNA3 and RDNA4 package jobs, runs the CPU
  regression on RDNA3, and accepts pushes to `rdna3` and `port/rdna3-*`.
  A push does not publish a release; release creation still requires a tag.

## Reproduction and target handoff

Use this worktree's own dependencies and outputs:

```sh
bash fetch_deps.sh
NR_ARCH=x86_64 bash linux/build/build_vulkan_loader.sh
bash linux/test/check_rdna3_port.sh
NR_GPU=rdna3 NR_ARCH=x86_64 bash linux/build/build_package.sh
```

Prerequisites are the existing Linux build tools from README.md, a native
Vulkan development library for the CPU descriptor harness, and MinGW. Nothing
is installed globally by these commands. The current build host uses GCC 16.2.1
and MinGW GCC 16.2.0; shader compilers and third-party inputs are pinned by
`fetch_deps.sh`. No cross-host binary reproducibility claim is made.

This selective port does not make upstream's tag an ancestor of the fork;
without a reachable `v*` tag, the normal version script emits a commit ID.
For a named trial archive, the supported `NR_VERSION` override can be set to
`0.0.9-linux-preview-g<source-commit>` for the complete build. This does not create
a release tag. Keep the exact source commit in the build record.

The packaged [0.0.9 target checklist](RDNA3-TARGET-TESTING-0.0.9.md) gives
installation, rollback, model inputs, numerical commands and game scenarios.
Full model numerical and game acceptance results are recorded separately below.
In particular this port does not establish a fix for the historical Z1
Extreme/DD2 green output or FG flicker without reproducing those game scenarios.
