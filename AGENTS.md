# Working on DLSSNR-AMD

Notes for contributors and coding assistants. README.md covers what the project is and how to build it.

## Layout rules

- `linux/` is the reference implementation. `windows/` is a separate copy for the AMD Windows driver
  (its shader compiler, LLPC) and is changed on its own. The two trees share no files on purpose.
- A workaround for the Windows driver goes into `windows/` only. A change goes into `linux/` only if
  it helps Linux. When a fix applies to both, make it in both, deliberately.
- Build scripts `cd` to the repository root themselves; run them from anywhere. Everything under
  `toolchain/`, `artifacts/` and `build/` is downloaded or generated and never committed.

## Things that must stay true

- **No NVIDIA files.** Never commit `nvngx_dlssnr.dll`, the weights, `dlssnr.bin`, `dlssnr-small.bin`, tensors extracted
  from the DLL, or packages that contain them. The model reaches users only through
  `linux/package/model-tools/extract_model.sh` run on their own DLL. CI checks that packages carry no model.
- **Host and shaders agree.** The C++ host is compiled with the defines in `*/build/arch/rdna4.sh`,
  the shaders with those in `*/shaders/rdna4/pipelines.json`; `shader-constants.txt` ties them together
  and the runtime refuses a shader folder that disagrees. Change both sides together.
- **Precision floor.** Keep at least the original network's precision at every operation: FP8 only
  where the original uses FP8, FP16/FP32 elsewhere. No new lower-precision boundary, no truncated
  inputs or lookup tables. Changing the order of a reduction changes the picture too - it is not a free
  reordering.
  The optional optimized networks' INT4 layers (`NR_INT4=1`, the `pipelines-int4.json`,
  `pipelines-performance.json` and `pipelines-ultra.json` overlays) and fitted experimental networks
  intentionally change the network. They require their own references, never the baseline golden.
- **RenoDX's shaders stay RenoDX's.** `linux/shaders/renodx/` is RenoDX's code under its MIT licence; changes
  there are speed changes that leave every output bit as it was, each listed in the README in that folder.
- **Windows ACO Mode has a record for every network SPIR-V.** The unchanged Windows runtime builds its
  matching pre-port Linux shaders with `windows/build/build_linux_aco.py`. Keep current Linux shaders
  (`linux/build/build_network.py rdna4`, optimized INT4 with `--int4`) covered too. ACO machine code is in
  `windows/data/aco/records` and `windows/data/aco/int4/records`, found by the SPIR-V's hash. A new or changed
  Linux network shader needs new records in both (a missing one turns ACO Mode off for the run), and a new
  pipeline whose name has no native Windows shader needs a template alias in `shell-aliases.txt`.
  Windows int4 mixed runs with ACO Mode only.
- **No D3D device or swap chain inside the game's process** on Windows (NR Control): OptiScaler's hooks
  take them for the game's. `nr_present_probe.exe` finds DXGI's methods in a process of its own.
- **Weight packing.** A new kernel that reads weights must be added to the host's weight packing
  (`nr_graph.cpp`); otherwise it runs on unpacked data and its results are meaningless.
- **Reproducible shaders.** `fetch_deps.sh` pins glslang 16.5.0; with it `build_network.py` produces the
  same SPIR-V bytes every time. RenoDX's shaders (`linux/shaders/renodx`, the RenoDX style) are built
  with the pinned Slang 2026.19 by `linux/build/build_renodx.sh`. After a change that should not alter
  the arithmetic (refactor, comments, scheduling of independent work), compare the `.spv` files before
  and after.

## RDNA3

- `linux/shaders/rdna3/` is the RDNA3 network (e4m3 values held as FP16, see README-RDNA3.md); `rdna4/` is not
  touched by it. Build scripts take `NR_GPU=rdna3`; the host is gated by `NR_ARCH_RDNA3`.
- Run `python3 linux/test/check_rdna3.py --model dlssnr.bin` before and after any change to it. A change that
  leaves the arithmetic alone must be EXACT; a change that does not is a new golden and needs a reason in the
  commit. Time kernels with the GPU idle.
- A raw 32-bit view of an arena (counters, sync words, scale tables) goes at alias binding 16 plus the arena's
  binding, never next to an e4m3 view: the e4m3 bindings read the FP16 twin buffer. The int4 data's views are such.
- Legacy int4 mixed on RDNA3 (`pipelines-int4-legacy.json`, `NR_INT4_LEGACY=1` in its test build): run `python3 linux/test/check_rdna3_int4.py
  --model CC_SILVER_AARDWOLD.bin` before and after any change to it, as above; its picture is not the default network's and has
  its own golden. Every int8 fragment in those kernels is multiplied twice with `NR_I4_MMA2` (the second marked
  saturating): the host rewrite turns the pair into the two halves of the fragment and refuses a pipeline whose
  WMMAs are not in marked pairs, so a WMMA added outside `NR_I4_MMA2` fails the build of that pipeline.
- The small model (`CC_SMALL_FP8.bin`, formerly `dlssnr-small.bin`, `[Network] Model`): run `python3 linux/test/check_rdna3_small.py --model
  CC_SMALL_FP8.bin` as above for a change that reaches it; a change to `nr_graph.cpp` or the planner also needs
  `check_rdna3.py` to stay EXACT.

## Performance work on RDNA4 (RADV / ACO)

Measured facts worth knowing before changing kernels:

- FP8 WMMA and VALU instructions do not overlap on RDNA4, not even across waves. Frame time is roughly
  WMMA time + VALU time + load time + idle time; saving VALU instructions saves time directly.
- ACO turns per-tile guards and uniform-looking branches it cannot prove uniform into phi copies and
  extra VGPRs; a "cheap" skip can cost occupancy. Count VGPRs and waves in the ISA
  (`RADV_DEBUG=shaders`) before and after.
- Wave arbitration is oldest-first: in persistent kernels, the second workgroup on a CU runs much slower
  than the first.
- Keep every LDS `coopMatLoad` stride and offset 16-byte aligned: the Windows compiler reads fragments
  with 8-byte-aligned paired loads and otherwise reads unwritten padding.
- Measure frame time over many frames and several runs at the same clocks (the log's per-frame GPU
  time); a single fastest run is not a result.

## Versions and releases

- The version comes from git tags only (`*/build/version.sh`): tag `v0.0.1` builds as `0.0.1`, later
  commits as `0.0.1-3-g1a2b3c4`. Package names and the DLLs' log lines carry it.
- Releasing: `git tag v0.0.2 && git push origin v0.0.2`. CI (.github/workflows/packages.yml) builds
  the Linux packages and the Windows package and publishes a GitHub release. For a Linux-only release,
  set the repository variable `WINDOWS_PACKAGE` to `off` first (`gh variable set WINDOWS_PACKAGE --body off`).
  A tag with `-win-` in it (`v0.0.4-win-preview`) is a Windows-only release: CI builds no Linux packages and
  does not mark the release latest.
  A tag with `-linux-` in it (`v0.0.6-linux-preview`) is a Linux-only release: CI builds no Windows package.

## Style

- Comments and user-facing text in English. Comments explain why (the measurement, the trap), not
  what the next line does.
- No internal version tags or references to files that are not in this repository.
- Package text (`*/package/README.txt`, installer messages) stays short and plain.

## This fork

- Canonical instructions are `AGENTS.md`; `CLAUDE.md` is a relative symlink to it. Preserve this arrangement
  when merging upstream changes, including any changes upstream makes to its own `CLAUDE.md`.
- Owner's writable fork: `https://github.com/saaaaaaaaaaaaaaaaaas/DLSSNR-RDNA3-0.0.8.3`, default branch `rdna3`.
- Main checkout in the current workspace: `/home/bozo/Projects/DLSSNR-RDNA3-0.0.8.3`.
  The 0.0.9 port worktree is `/home/bozo/Projects/DLSSNR-RDNA3-0.0.9`.
- `origin` is the owner's writable fork. Original upstream is
  `https://gitlab.com/mochizuki0323/DLSSNR-AMD.git`; the original RDNA3 port is
  `https://github.com/mauri870/DLSSNR-RDNA3.git`. Push only to `origin`.
  Older Fusion checkout/remote details in existing delivery reports are historical.
- This project's primary target is Linux / Proton on AMD RDNA3. Keep the upstream Linux and Windows
  separation and preserve RDNA4 behavior. Windows changes need an explicit reason.
- Do not change neighboring projects, system services, drivers, network settings or global agent configuration.
  Use project-local toolchains and dependencies. System changes belong to System Lab tasks.
- Work in a separate Git worktree outside the main checkout, with a dedicated branch. Check and preserve
  existing changes first. Build and test from that worktree; do not share its `build/`, `artifacts/` or
  `toolchain/` with another worktree. The pinned dependency setup is `bash fetch_deps.sh`; run it in the
  worktree before building, as described in `README.md` and `README-RDNA3.md`.
- Make focused commits. Push reviewable implementation branches to `origin`; integrate finished changes
  through Git, never by copying generated output over source files. Remove a worktree only after its changes
  are committed and preserved. Do not publish a release tag until its intended checks have passed.
- Bootstrap and assessment are recorded as System Lab tasks SYS-182 and SYS-181. The handoff prompt is
  `docs/PORTING-PROMPT.ru.md`. If PM is used, resolve the actual configured project and task; do not invent a key
  or enable Fusion's PM automation.

## Porting and verification

- Initial RDNA3 source: `a2b4edebcd236c8c6d189b340542ce0bf0c3d673`, based on upstream
  `v0.0.8-linux-preview` (`960038fd1cb892742548f408355e0f3b2fd30d6f`).
- Initial target: upstream `v0.0.8.3` (`931c50fe2f6096e8ac8bf60d3536ffc70db4a846`).
  The owner has requested the next port: fork source `88d8c9c`, upstream target
  `v0.0.9` (`9503f6b180df45305b38fbd21cd9f705164f2ab1`). See `docs/RDNA3-PORT-0.0.9.md`.
  Record exact source/target commits before work. Later upstream versions are a separate scope decision.
- Keep a written feature and acceptance matrix in project documentation before changing behavior.
  Update it with implementation and verification evidence. Separate common runtime fixes from new network kernels.
- Protect RDNA3's FP16 twin arenas, raw alias bindings, weight packing and gfx11 WMMA rewrites, including
  new descriptor retargeting paths. A merge without conflicts is not proof of numerical correctness.
- The new optimized network replaces upstream's old INT4 mixed implementation. Its calibration tables and
  pipeline definitions cannot be combined with the old RDNA3 kernels without adaptation and validation.
  Preserve old behavior deliberately or migrate it deliberately; never relabel one network as another.
- Keep manifest keys, host constants, shader variants, model filenames, installers and tests in agreement.
- Use the existing `check_rdna3.py`, `check_rdna3_small.py`, and, where applicable,
  `check_rdna3_int4.py` checks. Arithmetic-preserving changes must remain EXACT. If an upstream feature
  intentionally changes precision or the network, document that change and use an appropriate reference;
  do not overwrite a golden merely to make a failure disappear.
- Model weights and NVIDIA DLLs stay outside Git and distributed packages. Check package contents before
  sharing artifacts. Keep upstream license and attribution files.
- homeserver's Fusion worker reports no GPU. Do not claim GPU tests, frame timings or game compatibility
  from a successful build or software Vulkan execution. The owner reported Z1 Extreme, Dragon's Dogma 2,
  DX12/OptiScaler with XeSS MFG XMX (`realdody/dlssg-to-xefg-xmx`), Proton CachyOS Latest and Mesa 26.2.5: green output and slowdown after resolution/SR
  changes and with a second pass, plus FG menu flicker. Exact Proton/game builds and GPU qualification remain pending.
  The port and target checklists are in `docs/RDNA3-PORT-0.0.8.3.md` and `docs/RDNA3-TARGET-TESTING.md`.
  The follow-up cursor/FG/footer trial and artifact are recorded in `docs/NR-CONTROL-FIX.md`; live validation is pending.
- Missing target hardware does not block source work, shader compilation, packaging and CPU/static checks.
  Record GPU-dependent checks as unrun and provide exact commands and required inputs for the target machine.
- Report the implemented features, actual checks, artifact locations and remaining limitations precisely.
