# NR Control pointer and FG status follow-up

Base: `3d4e56b` on the 0.0.8.3 port branch. The reported installation identifies itself
as `0.0.0-g2ab033e`, on Z1 Extreme / Dragon's Dogma 2 / DX12 / Proton CachyOS /
Mesa 26.2.5 / OptiScaler, with XeSS MFG XMX from realdody/dlssg-to-xefg-xmx.
Reported: pointer invisible despite working hit testing; FG not detected; footer
resolution flickers with FG enabled. The rest of the menu is stable.

## Changes and acceptance

| Area | Change | Available check | Target acceptance |
| --- | --- | --- | --- |
| Pointer | Draw ImGui's pointer whenever NR Control is open; hide the native client cursor on WM_SETCURSOR and ask the game's window to restore it on close | PE compilation; UI implementation review | Visible with FG off/on; no duplicate pointer; movement, clicks, close/reopen, game cursor and Alt-Tab recover |
| Resolution | Keep a complete tuple from the upscaler evaluate, independent of per-layer reports; expire after 2.5 seconds without new input | Concurrent CPU snapshot/partial-frame/resize tests | Stable footer with FG and multiple NR placements; actual resolution/SR changes update promptly |
| Footer layout | Keep the resolution at a fixed horizontal position while the NR timing text changes | PE compilation | No shifting when timing becomes unavailable on a generated frame |
| FG indicator | Structured On/Off/Unknown state; provider calls outrank ambiguous DXGI ratios; declared MFG multiplicity remains numeric | CPU provider/expiry/cadence tests | Selected 2x..6x matches the requested multiplier; Off only from explicit provider status, not missing presents |
| XeSS MFG XMX | Observe successful public NGX Evaluate calls of `dlssg_to_xefg_intel_is_better.dll`; use EnableInterp and MultiFrameCount, without changing arguments/results | ABI/PE compilation; source comparison with provider | Hook and state-change log lines appear; toggle FG and repeat every supported multiplier |
| FSR / ordinary XeSS FG | Observe successful ffxConfigure and xefgSwapChainGetLastPresentStatus calls | PE compilation; CPU fact arbitration | Provider enabled/disabled state matches OptiScaler |
| FG lifecycle | Poll for provider libraries once a second independently of HUD FIX/Present-layer activation; recognize dlssg_to_xefg callers as a generator | CPU module classification; PE compilation | Works with HUD FIX disabled and layers after SR; no double NR on generated frames |

No network kernel, weights, calibration, precision, graph planner or shader variant
is changed. The shared `control.bin` layout/version stays unchanged, so this is a
plugin update for the existing package. Shader/golden GPU checks are not claimed
from this UI fix.

## Provider reference and interpretation

Inspected `realdody/dlssg-to-xefg-xmx` at
`7d9c0db6476613cfc40b0c96e7a25a708e66fd84`:
[README](https://github.com/realdody/dlssg-to-xefg-xmx/blob/7d9c0db6476613cfc40b0c96e7a25a708e66fd84/README.md),
[NGX export](https://github.com/realdody/dlssg-to-xefg-xmx/blob/7d9c0db6476613cfc40b0c96e7a25a708e66fd84/src/Exports.cpp),
[frame generator](https://github.com/realdody/dlssg-to-xefg-xmx/blob/7d9c0db6476613cfc40b0c96e7a25a708e66fd84/src/FrameGenerator.cpp).
It calls private XeFG execution functions, so ordinary XeSS proxy swap-chain
status alone cannot detect this route. Its successful NGX evaluate exposes the
requested EnableInterp state and 1..5 generated frames (2x..6x). This is a
**request/configuration observation**, not a physical display-rate measurement or
proof that every requested interpolation succeeded. The menu/status log says so.

The ordinary XeSS observer uses the documented three-field
[xefg_swapchain_present_status_t](https://github.com/intel/xess/blob/main/inc/xess_fg/xefg_swapchain.h).
OptiScaler source at `e7e3872d410f704479d94d318d68d163b6b58947` calls that API after
its XeSS proxy Present. Its FSR output calls ffxConfigure each frame. HUD-less
capture still filters handovers by their existing game-caller rule; telemetry
also accepts OptiScaler's own provider calls.

## Build and CPU checks

Use this worktree's dependencies only:

```sh
bash linux/build/bootstrap_ubuntu_tools.sh
source build/local-tools.env
bash fetch_deps.sh
g++ -std=c++17 -O2 -pthread -Ilinux/src/core linux/test/control_presentation.cpp -o build/control-presentation
build/control-presentation
bash linux/build/check_paths.sh
bash linux/build/check_imgui.sh
NR_GPU=rdna3 bash linux/build/build_asi.sh
```

The source fixture tests coherent extent snapshots with concurrent writers/readers,
invalid/generated-frame input, real resize, provider Off/On, expiry, low-frame-rate
ambiguity, declared MFG, multiple contexts and generator module classification.
Actual checks completed on homeserver:

- Native fixture passes; AddressSanitizer + UndefinedBehaviorSanitizer run passes.
- `check_paths.sh` and `check_imgui.sh` pass.
- Full `NR_GPU=rdna3 bash linux/build/build_asi.sh` passes with exit 0 from clean
  source `2e2cce42aec5dee7348466e29fe9f1fd98f686fd` and this worktree's pinned tools.
- The PE is AMD64, has a zero timestamp and exports InitializeASI. It imports no
  `vulkan-1.dll` or dynamic `libwinpthread-1.dll`. Its embedded source ID is `2e2cce4`.
- All 26 rebuilt wilsjo2 processing SPIR-V files are EXACT against the previous
  `0.0.8.3-65-g2ab033e` package. Network shader/data/planner/runtime and old goldens
  are unchanged in Git; no GPU numerical or game test was run.
- The update audit passes: exactly the ASI, README, project MIT license and five
  dependency notices (Inter, Dear ImGui, MinHook, RenoDX, FidelityFX FSR1).
  There are no models, NVIDIA DLLs or third-party FG binaries in this artifact.
- Repeating normalized tar/gzip gives an identical archive. A second full plugin
  rebuild was not run.

## Delivered update

Branch: `fix/nr-control-cursor-fg`, pushed to the owner's origin. No upstream PR.
At the owner's subsequent request to publish the download on GitHub, the update
was published as a [trial prerelease](https://github.com/hedonemaeda/DLSSNR-RDNA3/releases/tag/trial-nr-control-2e2cce4),
with tag `trial-nr-control-2e2cce4` pointing to the exact build source. It is not
marked latest; GPU qualification is still pending.
Worktree: `/home/gedomada/projects/dlssnr-rdna3-fix-control`.

- Public [update archive](https://github.com/hedonemaeda/DLSSNR-RDNA3/releases/download/trial-nr-control-2e2cce4/NR-Control-0.0.8.3-68-g2e2cce4-x86_64-rdna3.tar.gz)
  and [SHA-256 file](https://github.com/hedonemaeda/DLSSNR-RDNA3/releases/download/trial-nr-control-2e2cce4/NR-Control-0.0.8.3-68-g2e2cce4-x86_64-rdna3.tar.gz.sha256).
- Archive: `artifacts/control-hotfix/NR-Control-0.0.8.3-68-g2e2cce4-x86_64-rdna3.tar.gz`.
- Archive SHA-256: `3c093e2bd94e2f698d5f1aad27ef70051c4f0d886920203509f10817b92f32b6`;
  an adjacent `.sha256` file allows verification with `sha256sum -c`.
- Plugin: 8,212,594 bytes; SHA-256
  `ce9fb15224ffddea6ca9f457a109f93807c9c9c1ede3c7bd54a60b655863d90e`.
- The direct ASI builder stamps its source ID, so NR Control shows **2e2cce4**.
  Later documentation commits do not change this executable.
- Logs: `build/control-fix-logs/`. A snapshot with the exact packaging script is
  `artifacts/control-hotfix/build-logs-2e2cce4.tar.gz` (checksum beside it).

## Installation, rollback and target trial

Exit the game. Back up `OptiScaler/plugins/dlssnr-amd.asi` outside the plugins folder.
Replace it with the hotfix's `dlssnr-amd.asi`. Keep the existing models, shaders,
OptiScaler/XeSS provider and settings. Rollback: exit the game and restore that ASI.
This update is for the previous Linux x86_64 RDNA3 0.0.8.3 port package.

1. Check the new build identifier in NR Control. Move the pointer across controls,
   click and scroll; close/reopen, Alt-Tab, test FG off and on.
2. Enable XeMFG XMX and test every available multiplier. Allow up to one second
   for the provider hook to attach and 2.5 seconds for stale facts to expire on Off.
3. Run NR after SR and then at Present, with HUD FIX off/on. The footer's resolution
   must remain stable during generated frames; changing SR/display resolution must
   update the complete render/output pair. Repeat with a second NR layer/pass.
4. In `dlssnr-amd.log`, expect `XeSS MFG XMX NGX provider status followed` and
   `XeSS MFG XMX successful NGX evaluate, interpolation on, requested multiplier ...`.
   Compare with the provider's `dlssg_to_xefg_intel_is_better.log` and OptiScaler.log.
5. If the problem remains, retain these logs, settings and a short video covering
   FG toggle and cursor movement; exact OptiScaler/provider/Proton builds matter.

There is no target GPU on homeserver. Live visibility, footer behavior, game
compatibility, FG transitions and visual correctness remain unrun here. Providers
renamed to other basenames, unloaded/reloaded at runtime, or with incompatible
NGX behavior need separate validation; no loaded library alone is counted as FG On.
