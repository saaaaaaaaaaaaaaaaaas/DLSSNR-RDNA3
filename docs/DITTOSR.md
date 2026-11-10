# Ditto SR in NR Control

Implementation base: `188324b759e9d34d4bed7361946bf034d69db0b9`
(the RDNA3 0.0.8.3 port with the NR Control cursor/FG fixes).
DittoSR reference: release `v0.4.0`, commit
`d37519ce85a84a4cc3083cad17a83335bee0f6d4`.
Neural transport correction base: `47f325e9cb17b08eee075e77697031b4c9d89e66`.
The acceptance matrix was committed before the correction in `2973d57`.

## Feature and acceptance matrix

| Area | Contract | Evidence |
| --- | --- | --- |
| NR Control | Upsampling: Ditto SR; persist per layer as NRLookUpsample=4 | Existing PE/INI checks; target menu trial pending |
| Neural transport | NR neural after optional look shaping -> Ditto SR -> up_neural; retain up_proxy and the normal full-resolution resolve | Identity-network detail test fails on old path (contrast 0), passes on correction (0.498413 of original 0.5) |
| Composition | Existing HDR/SDR decoders, color/transfer controls, Preprocess and final commit receive the full-size proxy/neural images | Software Vulkan: nonidentity HDR half-strength blend error 0.000152588; zero-strength bypass exact |
| Temporal inputs | Actual game jitter scaled to NR pixels; normalized game motion and foreground depth from guide subrects | Constant assertions, D32/subrect cases and Vulkan validation; game motion quality pending |
| Output phase | Rephase Ditto's reconstructed up_neural to match up_proxy and the next SR's input jitter | Explicit phase shader and assertions; original NGX parameters unchanged |
| Multiple passes | Every active model pass owns independent Ditto temporal history | Two distinct neural images over seven frames: 14 network calls, output drift 0.000976562 |
| Resolution | 1280x720 at 0.33 -> 422x238 NR -> 1280x720 composed output -> OptiScaler SR -> 1920x1080 | Rounding and output-extent checks; target FSR 4 chain pending |
| Scope | Linux/Proton RenoDX layers, Before SR only; full scale uses ordinary NR | Software SDR/HDR 33%/50%/100%; unsupported conditions reported by layer |
| Lifecycle | Invalidate on cuts, resize, mode/settings changes, bypass; retire resources using existing GPU completion tracking | Software reset, switches, inactive pass and 72-frame ring reuse checks |
| Existing behavior | No learned network, weights, planner, golden or Windows changes; no RenoDX shader arithmetic changes | Protected-source diff and 29 original SPIR-V comparisons |
| Distribution | New trial ASI plus all five Ditto shaders, MIT/BSD notices, no NVIDIA/model files | Package audit and download receipt below; old prepare shader explicitly rejected |

## Use

This is an update for an existing RDNA3 0.0.8.3 port installation. Exit the game
and back up the ASI and shader folder outside the game directory. Copy the
archive's `OptiScaler` and `dlssnr-amd` folders into the game directory, merging
with the installed folders. Replace **both the ASI and all five Ditto shaders**:
the correction changes the prepare shader's push-constant layout. A new ASI
with an old prepare shader reports a mismatch instead of silently using it.
Keep the existing models, other NR shaders, OptiScaler and FG provider.

In NR Control, choose a RenoDX layer, **Placement: Before the upscaler**,
**Resolution scale: 0.33**, **Upsampling: Ditto SR**. Disable **Follow render
resolution**, which otherwise overrides the scale. Keep FSR 4 (or another SR)
selected in OptiScaler. At 100%, the ordinary NR path runs without Ditto.
Unsupported placement or unavailable guides/jitter is reported as a layer
error; the selected setting is retained.

## Composition and temporal contract

Each active model pass encodes the running reference into `proxy`, runs NR into
`neural`, and optionally shapes that neural image. The existing edge-aware
transport builds `up_proxy` at the layer's original extent. Ditto reconstructs
only the neural image into `up_neural`. The normal full-resolution codec resolve
then receives both images and the full-resolution running reference. HDR uses
its existing ratio resolve; SDR retains its legacy decoder and black restore.
Preprocess, pass strengths and the normal commit remain in the same path.
Ditto's intermediate image is never copied directly to the game output.

The existing transport still computes its ordinary edge-aware `up_neural`,
which Ditto overwrites before resolve. This retains the existing `up_proxy`
arithmetic and avoids changing RenoDX shaders, at the cost of redundant work.
No performance improvement is claimed for this integration.

Ditto receives encoded neural RGB, with its HDR flag off and exposure factors
at one. The codec already owns the game's HDR/exposure mapping. Applying that
mapping again inside Ditto would distort the encoded neural values.

Guide preparation honors depth/motion extents and subrect origins, chooses
foreground depth and matching motion, and expresses motion in normalized screen
units. Depth views use the depth aspect. RGBA32F motion storage avoids adding
a StorageImageExtendedFormats requirement. Each pass receives the actual game
jitter scaled from render pixels into NR pixels. Ditto's ordinary jittered-vector
cancellation removes jitter carried by motion vectors before stable history
reprojection; nonjittered vectors need no cancellation.

Ditto reconstructs onto a stable grid. The final prepare dispatch samples this
result at pixel minus the game jitter expressed in full-output pixels. That
puts `up_neural` on the current sample phase of `up_proxy` and the next SR.
The game's incoming NGX jitter and motion parameters remain unchanged.
Actual cascaded SR quality still requires a game trial.

Each model pass owns separate MDA, auxiliary and reconstruction histories,
including its previous jitter. Reset/cuts, resize, NR bypass, inactive passes,
preprocessing and reconstruction settings invalidate the relevant histories.
New history texels are initialized, and unselected resources retire under the
stage's existing GPU completion tracking. Guide views and descriptor pools use
that same recording lifetime. Constants use recorded uploads and barriers.

The corrected transport also exposed an existing host descriptor issue: when
look temporal filtering was off, its declared UINT history slots were bound to
a FLOAT placeholder. The host now binds typed RG32_UINT/R32_UINT placeholders.
This is a common runtime binding fix, separate from learned network arithmetic.
No RenoDX shader source was changed for it.

## Checks on homeserver

Dependencies were installed locally with `bash fetch_deps.sh` and
`bash linux/build/bootstrap_ubuntu_tools.sh`.

The native fixture runs real codec/Ditto shaders under llvmpipe with synthetic
identity and nonidentity networks, without model weights. Seven existing cases
cover SDR/HDR, 33%/50%/100%, odd extents, guide subrects, true D32 depth, multiple
passes, an inactive final pass, Preprocess, temporal look, history-ring reuse,
sRGB transfer, resets and live mode changes. Added composition cases check
full-reference detail, half/zero strength, and independent pass histories.
The corrected run has zero API or synchronization validation errors.

The detail regression was run against the old decoded-frame path before the
correction: max error 0.250732, contrast 0, assertion failure. The corrected path
has max error 0.00244141 and contrast 0.498413. These synthetic image checks
establish the composition wiring, not NR network quality or game performance.
Logs: `artifacts/test-transport-before.log`, `artifacts/test-transport-after.log`.

Additional checks cover real jitter constants, jittered-vector cancellation,
output phase and 422x238 rounding. A new binary paired with the previously
published prepare shader is rejected with the expected mismatch message.
`check_paths.sh`, `check_imgui.sh` and the NR Control presentation fixture pass.
All five Ditto shaders are validated with `spirv-val --target-env vulkan1.1`;
original RenoDX shaders and pinned Ditto vendor sources are compared separately.

Run the fixture with a Vulkan driver available:

```sh
source build/local-tools.env
bash linux/build/build_renodx.sh build/shaders-renodx
g++ -std=c++17 -O1 -Itoolchain/Vulkan-Headers/include -Ilinux/src/core \
    -Ilinux/src/pe linux/test/dittosr_stage.cpp linux/src/core/nr_renodx.cpp \
    -lvulkan -o build/dittosr-stage
NR_RENODX_ROBUST=1 build/dittosr-stage build/shaders-renodx
NR_GPU=rdna3 bash linux/build/build_asi.sh
bash linux/build/package_dittosr_update.sh
```

Software Vulkan uses locally extracted Mesa 25.2.8 and validation layers in
`toolchain/vulkan-test/`. `VK_DRIVER_FILES`, `LD_LIBRARY_PATH` and `VK_LAYER_PATH`
select those files. `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` and
`VK_LAYER_ENABLES=VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT`
enable API and synchronization validation. `NR_RENODX_ROBUST=1` disables the
existing pipeline-robustness override because the fixture's device does not
enable that optional feature. System drivers and services were not changed.

## Target acceptance still required

AMD GPU image quality, frame times and game/FSR 4 compatibility are **unrun**.
The reported target is Z1 Extreme / Dragon's Dogma 2 / DX12 / Proton CachyOS /
Mesa 26.2.5; see NR-CONTROL-FIX.md. Software Vulkan tests the host wiring and
API contracts. It cannot qualify two temporal upscalers in an actual game.

1. Start with FG off, 1280x720 SR input and 1920x1080 display. Select the settings
   above. The log must contain `Ditto SR: neural 422x238 -> up_neural 1280x720; resolve with up_proxy`.
2. Compare Classic/Ditto at 0.33, 0.5 and 1; vary color/transfer strengths. Check
   still detail, pans, foliage, hair, moving silhouettes, particles, cuts and
   dark/bright menus. Check SDR/HDR when available. At 1 Ditto should not run.
3. Change scale, mode, layer/pass count and window size; disable/re-enable NR.
   Check recovery and history resets. Enable FG and exercise its multipliers.
4. Try After SR/Present, check the placement error, then restore Before SR.
   Record logs, screenshots and repeated per-frame GPU timings at fixed clocks.

Learned kernels, planner, weights and goldens are unchanged. The existing target
RDNA3 checks remain available with locally extracted models; do not update their
goldens for this feature:

```sh
python3 linux/test/check_rdna3.py --model /path/to/CC_SILVER_AARDWOLD.bin
python3 linux/test/check_rdna3_small.py --model /path/to/CC_SMALL_FP8.bin
python3 linux/test/check_rdna3_int4.py --model /path/to/CC_SILVER_AARDWOLD.bin
```

## Corrected build and public download

[Download 0.0.8.3-78-ge486f8a](https://github.com/hedonemaeda/DLSSNR-RDNA3/releases/download/trial-nr-control-dittosr-transport-e486f8a/NR-Control-DittoSR-0.0.8.3-78-ge486f8a-x86_64-rdna3.tar.gz)
([SHA-256 file](https://github.com/hedonemaeda/DLSSNR-RDNA3/releases/download/trial-nr-control-dittosr-transport-e486f8a/NR-Control-DittoSR-0.0.8.3-78-ge486f8a-x86_64-rdna3.tar.gz.sha256)).
[Trial release](https://github.com/hedonemaeda/DLSSNR-RDNA3/releases/tag/trial-nr-control-dittosr-transport-e486f8a).

- Build source: `e486f8a37726d66c99472e21b956ad52644f39df`, branch `feat/nr-control-dittosr`, pushed to origin.
- Full `NR_GPU=rdna3` ASI build completed with exit 0 from the clean source commit.
- Archive: `NR-Control-DittoSR-0.0.8.3-78-ge486f8a-x86_64-rdna3.tar.gz`, 2,975,503 bytes; SHA-256 `04c2e8fc05985e9a81b965e5d4c123d7f73e5f4a020cf2eeb2bb374e472f89db`.
- ASI: 8,230,273 bytes; SHA-256 `6406e8bea9f2c4b431ae3bfb70c6d39b30fe8e2474752fa789efec318dfaf897`.
- PE: AMD64, zero timestamp, InitializeASI exported; no Vulkan-loader or dynamic pthread import.
- Package audit: 15 files, exactly five Ditto shaders and required notices, no models/NVIDIA DLLs. Packaged shaders match the validated fixture binaries.
- Independent normalized package builds produce identical bytes. Both public assets were downloaded without authentication and match the local files.
- Published as a new trial prerelease, with the tag pointing to the exact build source. Latest-release state is unchanged; the old trial now links to this correction.
- Local archive: `/home/gedomada/projects/dlssnr-rdna3-dittosr/artifacts/dittosr-transport-update/NR-Control-DittoSR-0.0.8.3-78-ge486f8a-x86_64-rdna3.tar.gz`. This path belongs to the separate homeserver worktree; use the public download from other devices.
- Build/package logs: `artifacts/build-asi-transport.log`, `artifacts/package-transport.log`; receipt: `artifacts/dittosr-transport-delivery.json`.
- The main checkout remains unchanged. AMD GPU/game acceptance is pending as described above.

## Historical first trial

The original decoded-frame implementation is superseded by the neural-transport
correction. Its source was `30a4d3ed250a6877988f0311ea3a7ea7561a0711`, version
`0.0.8.3-74-g30a4d3e`, published as
[`trial-nr-control-dittosr-30a4d3e`](https://github.com/hedonemaeda/DLSSNR-RDNA3/releases/tag/trial-nr-control-dittosr-30a4d3e).
The historical archive SHA-256 is
`0216046e3b79be3ed2320f0fe409108454546bc4c6ece1c1d0b4e69d4634dddb`.
Its seven original software cases did not detect the composition regression;
the new high-frequency reference case does. Historical assets are retained,
with their original source and hashes, rather than replaced in place.
