# RDNA3 port of DLSSNR-AMD 0.0.8.3

## Scope and immutable inputs

- RDNA3 source: `a2b4edebcd236c8c6d189b340542ce0bf0c3d673`.
- Shared upstream base: `960038fd1cb892742548f408355e0f3b2fd30d6f` (`v0.0.8-linux-preview`).
- Fork preparation preserved: `2b091d75998bede9b6d86c683dc92d82790f26b6`.
- Target: `931c50fe2f6096e8ac8bf60d3536ffc70db4a846` (`v0.0.8.3`).
- Remote refs checked 2026-11-06: origin/rdna3 = preparation commit;
  upstream-rdna3/rdna3 = source commit; upstream target tag = target commit.
- Linux/Proton, x86_64, gfx11. No later upstream changes, release tags or upstream PRs.
- `CLAUDE.md` remains the relative symlink `AGENTS.md`; no Windows implementation port.

## Acceptance matrix (written before implementation)

Initial status for every row: planned, unverified. Build success does not establish GPU correctness.

| Upstream | Feature / source | Implementation acceptance | Verification acceptance |
| --- | --- | --- | --- |
| 0.0.8.1 d12c104 | Shared NR Control/Add-ons pages, Layers, hotkeys, languages | Bring Linux UI/config/installer changes; preserve RDNA3 model availability | Host/add-on builds; path/ImGui guards; live menus and hotkeys on target |
| 0.0.8.1 | UTF-8 internal paths and wide Windows calls under Wine | Bring path helpers and replace narrow path calls | check_paths.sh; install in Unicode/spaced directory; target load/save |
| 0.0.8.1 | FG Present hooks, recovery pause, HUD FIX, copied pre-upscale colour | Bring Linux hook/session fixes; preserve fork temporal reuse | Build; target FG off/on, HUD-less frames, NR once per game frame |
| 0.0.8.1 | Crash diagnostics, ini recovery, Memory Guard option | Bring Linux diagnostics and config changes | Build; target diagnostics and recovery |
| 0.0.8.2 050b8e5 | Arena lifetimes, persistent output reuse, lazy images, dedicated noise buffer | Retain FP16 twins and binding 16+arena raw aliases through retargeting; add matching shader/manifest flags | Compile baseline; audit descriptors and planner; baseline/small EXACT at History Precision 32 on GPU |
| 0.0.8.2 | History Precision 16/32 | Default 16; rebuild/reset history on change; 32 preserves previous history format | Build/config checks; target both formats and switching; document intentional 16-bit history change |
| 0.0.8.2 | FG proxies, Streamline compute blits, menu locking, HUD hand-over | Port Linux fixes including runtime_blit | Compile compute shader; target FG paths and resize with menu open |
| 0.0.8.2 | Dynamic resolution queued builds and bounded prefetch | Port queue and diagnostic changes | Build; target resolution churn and NR_TEST_PLAN_DELAY_MS |
| 0.0.8.2 | VRAM ledger, language extras, pinned dependency simplification | Port Linux build/runtime changes | Pinned fetch, builds and package inspection; target ledger |
| 0.0.8.3 931c50f | Memory Guard off by default, measured need only | Bring exact upstream policy | Config checks; target memory pressure/recovery |
| 0.0.8.3 | FG HUD-less NGX evaluation and per-present UI; overlay swapchain lock isolation | Bring Linux hooks and lifecycle fixes | Build; target game resize/menu and all available FG integrations |
| 0.0.8.3 | Session statistics, diagnostic package, installer logs; black restore/pedestal default 0 | Bring Linux config/UI/installer changes | Build/install/log route checks; target diagnostic zip |
| Existing | CC_SMALL_FP8 and CC_SILVER_AARDWOLD | Preserve gfx11 rounding, WMMA lane layout, weight packing, temporal reuse | Compile before new modes; old shaders byte comparison where arithmetic unchanged; existing GPU goldens EXACT |
| Existing | Legacy INT4 mixed | Keep its tables and tests isolated from new optimized calibration; deliberate migration | Old model hash/pipeline identity; check_rdna3_int4.py on same legacy network |
| 0.0.8.1–3 | CC_SMALL_FP8_EXPERIMENTAL | Implement ReLU/fixed attention/merged V-projection on gfx11; installer delta and manifest | Compile all variants, weight packing audit; compare against corresponding upstream network, no baseline golden replacement |
| 0.0.8.3 | CC_SILVER_AARDWOLD_OPTIMIZED Quality | Implement fitted ReLU, fixed attention, merged projection, constant C512 heads on gfx11 | Compile variants; prove constant-head detection; target matching network reference |
| 0.0.8.3 | Optimized Balanced | Adapt new int4 encoder expansion, attention QKV and ViT partial FFN calibration, Hadamard rotation | Compile and marked WMMA pair checks; model SHA; target matching Balanced reference |
| 0.0.8.3 | Optimized Performance | Also adapt fixed attention pre/encoder block routes | Compile routes; model/manifest agreement; target matching Performance reference |
| 0.0.8.3 | Optimized Ultra Performance | Also adapt encoder QKV, decoder expansions, extended ViT int4 and ultra tables | Compile routes; marked WMMA pairs; target matching Ultra reference |
| 0.0.8.3 | Named model files and migration | Runtime, arch scripts, tools, installer, ini and tests agree; legacy aliases migrated deliberately | Installer with no proprietary inputs; script/static checks; model hashes only on owner's machine |
| Delivery | RDNA3 Linux x86_64 package | Include routes, licenses, shader variants and docs; no NVIDIA DLL/model | Build package, list every member and scan models/DLL names; SHA256; install/rollback instructions |
| Regression | RDNA4 and Windows separation | Preserve upstream RDNA4 behavior; no Windows file changes without explicit need | Compile RDNA4 shader variants; source diff audit; ACO record caveat for any RDNA4 shader change |

## Numerical policy and target-only work

No GPU or model is available in the Fusion worker. Existing `check_rdna3.py`,
`check_rdna3_small.py`, and legacy `check_rdna3_int4.py` must be run before/after
on idle gfx11 hardware using the owner's extracted models and verification frames.
Arithmetic-preserving paths must remain EXACT. Keep existing goldens unchanged.
New fitted networks require their own reference and goldens; a default network's
golden is not a reference for a different network. History Precision 16 is an
intentional temporal precision change from upstream; use 32 for old-history comparison.

Implementation evidence, actual commands, artifact paths and remaining work will
be appended as the port progresses. GPU timing and game compatibility remain unverified.

## Stage 1 evidence

- Imported Linux 0.0.8.1 (`d12c104`) and 0.0.8.2 (`050b8e5`); Windows files untouched.
- Retained temporal reuse passes and configuration. Added `runtime_blit` alongside them.
- RDNA3 descriptors now copy every declared binding (including raw aliases), replace
  e4m3 bindings through the twin map, and replace raw aliases with byte buffers.
- Twins belong to `GraphAct` / `GraphArenas`, so shared resize plans and offload/reload
  retain both formats. Tail staging and arena zeroing update both buffers.
- Ported dedicated noise buffer and truthful `post_no_out1` / `noise_buffer` manifest keys.
- Pinned glslang 16.5.0 built the baseline (52 network, 8 temporal) and legacy INT4
  (65 network, 8 temporal) before shader edits. After memory changes, 61 SPIR-V files
  remain byte-identical; only the two pre-blocks and their three temporal variants
  changed because the noise binding moved. No network arithmetic was intentionally changed.
- `check_paths.sh`, `check_imgui.sh`, generated INIs and the CPU descriptor regression
  pass. Native runtime and MinGW runtime translation units compile with the RDNA3 defines.
- Full GPU checks were not run: this worker has no gfx11 GPU or model weights.

## Stage 2 implementation

- Imported the Linux target's complete 0.0.8.3 changes, including model filenames,
  fitted deltas, optimized calibration, diagnostics, Memory Guard and FG fixes.
- Added gfx11 experimental and optimized overlays with their actual shader paths:
  ReLU, fixed probability tables, merged V/output projection, certified constant
  C512 heads, H8-rotated Swin INT4 input and four-bank ViT INT4 scheduling.
- Fixed probability operands use the same upper-lane key-pair permutation as V.
  INT4 four-bank and constant-head paths retain gfx11's two record slots and
  `NR_I4_MMA2` marked pairs. No unmarked INT4 WMMA was introduced.
- All new overlays compiled with pinned glslang: experimental 53, Quality 56,
  Balanced 68, Performance 70, Ultra Performance 70 pipelines (plus 8 temporal each).
- The old INT4 network is replaced in installations, deliberately, as upstream does.
  Its old name and `--int4` migrate to optimized. Its tables and overlay are retained
  only for `check_rdna3_int4.py` with a test-only runtime define and
  `NR_INT4_LEGACY=1`; its golden is not used for optimized. Baseline goldens unchanged.
- Windows implementation files remain unchanged. Target hash-addressed ACO records
  and new template aliases are added (old records retained) solely because Linux
  RDNA4 SPIR-V changed in the upstream port and the repository requires matching
  Windows ACO records. These are upstream machine code, not NVIDIA files.

## Target machine and reported regressions

Owner supplied: Z1 Extreme (RDNA3 APU), Dragon's Dogma 2, DX12 through Proton,
Mesa 26.2.5, Proton CachyOS Latest, OptiScaler route (exact Proton/game builds still need recording in the test log).
Old build: green output and severe slowdown after changing network resolution,
display resolution or SR level; restart recovered the resize cases. A second pass
produced green output and slowdown even after restart. Menu flickered with FG.
These are priority acceptance scenarios; no fix is claimed proven on this machine.

## Reproducing the build

Ubuntu 24.04 x86_64 with a native C/C++ compiler, git, curl, Python 3 and the
Vulkan loader. No system packages were installed for this port. The optional
bootstrap extracts SHA-256-pinned Ubuntu packages into this worktree only:

```sh
bash linux/build/bootstrap_ubuntu_tools.sh
source build/local-tools.env
bash fetch_deps.sh
NR_ARCH=x86_64 bash linux/build/build_vulkan_loader.sh
NR_GPU=rdna3 NR_ARCH=x86_64 bash linux/build/build_package.sh
```

All downloads, tools and outputs are private to this worktree. If an exact
Ubuntu package version is no longer in the configured apt archive, use an archive
holding the listed version; do not silently substitute another checksum.
`SOURCE_DATE_EPOCH` defaults to the source commit's timestamp, PE link timestamps
are zeroed and the final tar/gzip metadata is normalized. `NR_SIGN=1` uses an
ephemeral certificate and is outside the reproducible-build comparison.

## Regression and distribution checks

- Rebuilt all six RDNA3 variants independently and compared every file, including
  temporal SPIR-V and markers: EXACT (72 / 67 / 72 / 98 / 100 / 102 files for
  default / experimental / Quality / Balanced / Performance / Ultra).
- Legacy INT4: 68 SPIR-V files remain identical to the pre-port build; the same
  five pre-block noise-binding variants change as in the default network. All
  old frame/layer goldens are unchanged. The legacy path is test-only; normal
  packages explicitly select the new calibration.
- CPU SPIR-V checks found no native FP8 types. Integer WMMA instructions occur
  in equal marked/unmarked counts: Balanced/Performance 750, Ultra 754; legacy
  502. This checks the SPIR-V pairing, not the driver's resulting ISA rewrite.
- All six Linux RDNA4 variants compile; their shader source tree is exactly the
  pinned upstream target's. Native planner emits 152 original / 76 small steps
  for 720p, 1080p, 1918x1078, 1440p and 4K.
- Native runtime, test runner and legacy test-only runtime compile. CPU descriptor
  test, config generation, path/ImGui guards and shell/Python syntax checks pass.
- Distribution audit now checks named model files, model-container magic and the
  known NVIDIA DLL hashes in tar/zip archives. CI uses it for both platforms;
  private builds with explicitly supplied models remain possible and unshareable.
- Windows package selection is pinned to its pre-port Linux ACO source snapshot
  (`960038f`), extracted from Git into generated build output. Its unchanged host
  still expects noise in weights and legacy INT4, so feeding it the newly ported
  Linux SPIR-V would be incorrect. This narrow packaging fix preserves the old
  Windows network; it does not port Windows runtime features or enable the new
  networks there. The normal Linux build continues using the new sources.

## Final acceptance status

| Area | Implemented | Actual qualification | Remaining target work |
| --- | --- | --- | --- |
| UI, hotkeys, localization, Unicode, Layers | Yes, complete Linux target sources | Host/add-on builds; INIs and guards | Live UI/hotkeys/font/Unicode install |
| FG, queued resize/dynamic resolution, menu locking, compute blits | Yes | Host/shader builds | DD2 FG provider(s), menu/resize, once-per-game-frame |
| History Precision, Memory Guard, VRAM reuse/lazy allocations | Yes; twins and raw aliases adapted | CPU descriptor regression; compilation | EXACT baseline frames, temporal transitions, UMA memory pressure |
| Small and Silver | Preserved | Baseline shader byte comparison; no golden changes | Existing EXACT GPU checks |
| Experimental | Implemented gfx11 computations | All 53 + 8 temporal pipelines compile/reproduce | Model hash, independent numerical reference and held-out clips |
| Optimized Quality | Implemented gfx11 computations | All 56 + 8 temporal pipelines compile/reproduce | Same, with constant-head equivalence |
| Optimized Balanced | Implemented gfx11 computations and new calibration path | 68 + 8 compile/reproduce; paired integer WMMA | Model hash, actual ISA rewrite, independent numerical reference |
| Optimized Performance | Implemented fixed-attention routes | 70 + 8 compile/reproduce; paired integer WMMA | Same plus pre/encoder route numerical checks |
| Optimized Ultra Performance | Implemented QKV/decoder/ViT INT4 paths and ultra tables | 70 + 8 compile/reproduce; paired integer WMMA | Same with ultra model/table coverage |
| Legacy INT4 | Retained only in regression harness; installations migrate deliberately | 65 + 8 compile, old tables/goldens isolated | Existing EXACT legacy GPU check |
| Model migration and installation | Named models; verified original/small reuse; new derived weights rebuilt | Script/config and package checks | Full extraction/installation with owner's 310.9.16 DLL |
| RDNA4 | Target Linux shader sources retained | All six variants compile; ACO hashes covered | GPU regression checks |
| Windows | Runtime unchanged; old ACO source selection preserved | Pinned default/legacy INT4 compile; all hashes covered | Full Windows package/game validation outside this Linux port |

No requested Linux mode is absent from the implementation. All modes remain
GPU-unverified. In particular there is no claim that the Z1 Extreme's reported
green output, second-pass slowdown or FG menu flicker is fixed until the target
acceptance matrix passes. Full NVIDIA extraction, derived-model SHA checks,
GPU goldens, actual gfx11 machine-code rewrite, quality, VRAM, timing and game
compatibility were not testable without the owner's DLL/models and target GPU.
All new-mode numerical references/goldens remain unapproved; existing tests were
not relaxed. The worker has no `/dev/dri` device node. No software Vulkan run is
being counted as a GPU test.

## Delivered artifact and actual build record

- Build source: `2ab033e5eacada86eee26736e74c6d530632370f`
  (`0.0.8.3-65-g2ab033e`). Check out this exact commit to reproduce the archive;
  later delivery documentation and ignore rules leave runtime, shaders and build recipe unchanged.
- Branch: `port/rdna3-0.0.8.3` in the owner's writable origin; no PR or release tag.
- Worktree retained: `/home/gedomada/projects/dlssnr-rdna3-port-0.0.8.3`.
- **Trial package:** `linux/package/DLSSNR-AMD-Vulkan-Linux-0.0.8.3-65-g2ab033e-x86_64-rdna3.tar.gz`.
- Size: 259,161,157 bytes; 1,135 files; staging directory about 454 MiB.
- SHA-256: `3691b2db4c6f0b6f21ed4ad80a0daee6a38eca0ea190c0941da3edacd30b8408`.
- Adjacent `.tar.gz.sha256` file allows `sha256sum -c` in `linux/package/`.
- The archive includes the project MIT license, dependency notices and
  `RDNA3-TARGET-TESTING.md` with OptiScaler installation, rollback and target checks.
- **Full recipe passed with exit 0**, from a clean worktree at the build source:
  `source build/local-tools.env`, then
  `NR_GPU=rdna3 NR_ARCH=x86_64 bash linux/build/build_package.sh`.
  This rebuilt all host/add-on/plugin components, six shader families, native
  INT4 tools/layer and the final archive. Its pinned loader prerequisite had been
  built separately with `NR_ARCH=x86_64 bash linux/build/build_vulkan_loader.sh`.
- Earlier packaging attempts at `397bb23` needed a corrected local 7z launcher
  and manual continuation. The final full recipe above needs no continuation;
  interrupted logs remain as an audit trail.
- All 1,135 archived payloads match staging by SHA-256. Tar ownership is 0:0,
  mtimes equal the source commit's epoch, siblings are sorted, gzip mtime is 0.
  Repeating tar/gzip from staging yields the **identical complete archive**.
- Against the earlier `397bb23` package, 1,132 payloads are identical. The only
  changed payload is `dlssnr-amd.asi` with its new Git version stamp; the project
  license and trial instructions are added. All shaders, other PE binaries,
  native tools and INT4 layer remain byte-identical across those builds.
  The independently rebuilt DXGI probe is also EXACT with a zero PE timestamp.
- CPU checks on the **packaged** shader directories pass: default 122 SPIR-V
  (including processing shaders), experimental 61, Quality 64, Balanced 76,
  Performance 78, Ultra 78. Paired integer WMMA counts are 750/750/754 for the
  latter three. No native FP8 types or missing rewrite markers were found.
- Package audit passes; named models, model-container magic, unexpected `.bin`
  tensors and known NVIDIA DLL hashes are rejected. The actual DLL member list
  contains OptiScaler, AMD/Intel helpers, ReShade and Vulkan loader binaries only.
  Synthetic negative cases exercise model renaming/magic and binary-tensor checks;
  intended upstream calibration-table paths remain allowed.
- Host environment: Ubuntu 24.04.5, native GCC 13.3.0, glibc 2.39; cross compiler,
  CMake, 7z and auxiliary packages pinned in `linux/build/ubuntu-24.04-tools.tsv`.
  Shader and reference dependencies are pinned by `fetch_deps.sh` (glslang 16.5.0,
  Slang 2026.19). No system package, GPU driver or service was installed.
- GPU execution, extraction/model SHA and game tests were **not run**. Independent
  shader builds, unchanged host payload comparisons and repeated archive creation
  were verified; two complete builds of the exact final commit in fresh worktrees
  were not performed. No claim of cross-host bit reproducibility is made.

Build logs remain in `build/port-logs/`. A snapshot is retained at
`artifacts/port-delivery/build-logs-2ab033e.tar.gz`; its SHA-256 is beside it.
Key evidence:

| Log | Evidence |
| --- | --- |
| `package-clean-recipe.log` | Complete final build and package audit |
| `delivery-build-environment.txt` | Exact host/tool versions and input hashes |
| `delivery-inspection.txt`, `delivery-payload-sha256.json` | Metadata, architecture, included modes/notices, payload hashes |
| `delivery-payload-comparison.txt`, `pe-reproducibility.txt` | Unchanged payloads and independent probe comparison |
| `shader-reproducibility.txt`, `archive-reproducibility-final.txt` | Independent shader rebuilds and repeated archive bytes |
| `static-packaged-shaders.txt`, `aco-coverage-final.txt` | Packaged gfx11 variants and RDNA4/Windows record coverage |
| `installer-rejection.txt`, `package-audit-rejection.txt` | Rejected malformed model and distribution guard cases |

Install, rollback, target commands, inputs, numerical references and requested logs:
[RDNA3-TARGET-TESTING.md](RDNA3-TARGET-TESTING.md). The original Z1 Extreme regressions
remain unverified until that target matrix passes.
