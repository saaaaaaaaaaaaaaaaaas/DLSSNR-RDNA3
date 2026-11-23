# RDNA3 Z1 Extreme release branch

Branch: `rdna3-z1-extreme`. Baseline: `2be80e53b8c566755a679b6b1968103e4f0ba9d7`.
Release target: `v0.0.9-z1e.1`, Linux/Proton x86_64, `z1-extreme-turbo` only.
The main/default branches and their build policies are not changed by this branch.

## Release acceptance matrix

| Area | Required evidence |
| --- | --- |
| Target | Dedicated entry point accepts RDNA3/x86_64/Turbo only; invalid target overrides stop before dependencies or compilation. |
| CI | This branch's workflow has a single RDNA3 Z1 job, no GPU matrix, RDNA4 job or native Windows build. |
| Build | Pinned dependency verification, local loader build and a complete fresh model-free package; version from the release tag. |
| Network | All six directories select Turbo, validate their SPIR-V and retain the recorded shaders/constants. No Fast or old runtime reuse shaders. |
| Package | No models/NVIDIA DLLs, original notices plus reference MIT license, enabled N=2 defaults, x86_64 runtime consumers and dynamic Vulkan imports. |
| Publication | Batch source commits into one push; attach only the audited RDNA3 Z1 archive, checksum and manifest. Keep real-game/HDR/FG limits in the release notes. |

Runtime, network and calibration sources are unchanged from the baseline; the
existing [Motion qualification](RDNA3-SEAMLESS.md) and
[full validation record](RDNA3-SEAMLESS-VALIDATION.json) remain applicable.
Historical Turbo golden differences are not replaced or relabelled as passing.
This release does not add game/FG x4/HDR qualification.

Build the tagged source with:

```sh
git checkout v0.0.9-z1e.1
bash linux/build/build_z1e_release.sh
```

The release entry point forces RDNA3, x86_64 and Turbo. Its result is under
`artifacts/z1e-release/release/`, including the archive, SHA-256 and manifest.
Models must be extracted from the owner's own DLL during installation.
