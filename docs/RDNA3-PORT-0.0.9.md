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

The current build environment has no `/dev/dri`. NVIDIA DLLs and extracted
models have not been supplied. GPU execution, numerical EXACT checks, model
extraction, frame timings and game compatibility must be recorded as unrun.
Existing goldens must remain unchanged. New processing modes need their own
matching reference; they must not be compared to a different network's golden.

Implementation, actual checks, artifacts and target commands are recorded below
as the port progresses.
