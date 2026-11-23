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

## Built and audited release

Built on 2026-11-23 from `73a0b9dd5934b5a13ea05f85469f6d63624f0038` (`v0.0.9-z1e.1`).
The complete package is rebuilt from this tagged source; runtime binaries carry
the release version rather than the previous preview stamp.

- Archive: `DLSS-NR-AMD-Vulcan-Linux-0.0.9-z1e.1-Z1E-Turbo-Temporal-RDNA-3.tar.gz`
- Bytes: 273,221,251.
- SHA-256: `d70d47855388ce3ff933b170bdbc6f22e3a797b8ffe7c24f6bff208a7993c9a4`.
- Audit: 1,164 files, model-free, all six Turbo markers, 422 SPIR-V validations.
- All 517 profile files match the Motion-qualified builds byte for byte.
- Runtime/shader/calibration source diff against `2be80e5`: empty.
- Three x86_64 runtime consumers checked; no static `vulkan-1.dll` imports.
- CPU checks cover descriptors, presentation, paired INT4 WMMAs, QKV routing and INI defaults.
- Guards reject RDNA4, i686 and Fast before building.

The archive, adjacent checksum and `release-manifest.json` are under
`artifacts/z1e-release/release/`. The manifest records every file's digest and
the exact source/tag/version. Local build log: `build/z1e-release/build.log`.
The included older qualification documents describe the baseline Motion tests;
the release manifest identifies this fresh build. No game/FG/HDR results are
inferred from a successful package build.

This release is published as a pre-release for the Z1 Extreme branch and does
not replace the general RDNA3 release as latest. No RDNA4 package is built.
