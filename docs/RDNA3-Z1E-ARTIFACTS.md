# Local Z1 Extreme Fast and Turbo trials

Prepared and audited on 2026-11-16. Shader implementation: `1a1a481`.
Packaged qualification documents: `1a2bdee`. Target: Linux/Proton on Z1 Extreme.

Both profiles change the picture. Fast is the conservative option; Turbo changes
more rounding sites. The measured reduction in isolated Small/Silver network
time is 2.08–3.34% for Fast and 3.18–5.41% for Turbo, relative to the already
optimized exact Z1 profile. These are not game FPS gains. See the
[measurements and image differences](RDNA3-Z1E-AGGRESSIVE.md) and
[validation record](RDNA3-Z1E-FAST-VALIDATION.json).

## Archives

Files are local under `linux/package/`; each has an adjacent `.tar.gz.sha256`.

| Profile | Filename | Bytes |
| --- | --- | ---: |
| Fast | `DLSS-NR-AMD-Vulcan-Linux-0.0.9-Z1E-Optimized-Fast-RDNA-3.tar.gz` | 271,503,969 |
| Turbo | `DLSS-NR-AMD-Vulcan-Linux-0.0.9-Z1E-Optimized-Turbo-RDNA-3.tar.gz` | 271,475,626 |

Fast SHA-256: `a330ccd95e23f00b5ce2331302974a119754e33dc0e5f95996c0e1faae8fa837`.

Turbo SHA-256: `e30a7ab36bb080d7fc077dc601bb70f170e5c233be055d98ac66a7ec4a17041f`.

The normal installer selects the compiled profile; no launch variable is needed.
Use your own model/DLL with it. `Z1-PROFILE.txt` identifies the selected trial.
The [original exact archive](RDNA3-Z1-ARTIFACT.md) is preserved byte for byte for
rollback; its SHA-256 remains
`9d13e81a8d226e6aa770f332210bce018e43bbcc216dc9d51fcb8f469431e662`.

## Provenance and audit

The audited exact archive is extracted into new staging directories. Each trial
overlays all **520 validated profile files** across base, experimental, optimized,
INT4, Performance and Ultra, including temporal passes and profile markers.
They match the newly compiled trees byte for byte. The new trial documentation
and profile notice are added; the other **645 original files** remain identical.
The archive entries are also compared against their staging files.

Host components are reused from the exact archive, source `72a157f`. All C++ and
calibration sources and host compilation defines match that source, so no host
rebuild is needed for this shader-only change. The internal host version remains
`0.0.9-z1-preview-g72a157f`. The outer filenames and six `gpu-profile.txt` markers
identify the selected new profile. A full build from source is supported by the
normal package builder as documented in the measurements.

Each archive passes `linux/build/check_package.py`: **1,168 files**, no model
containers, unexpected tensors or known NVIDIA DLLs. Both new checksum files
and the preserved exact archive's checksum pass `sha256sum -c`. Private models,
test installations and raw GPU outputs are excluded. Existing licenses and
attributions remain.

Local records are `build/aggressive/package.log`, `package-audit-fast.json` and
`package-audit-turbo.json`. They include immutable base checksum, source epoch,
profile file counts and the hashes of every reused file. The qualification
documents in each archive match `1a2bdee`; this checksum note is added to Git
after packaging to avoid an archive containing its own checksum.

No game installation or system settings were changed. No RDNA4 compilation,
release tag, release creation or archive upload was performed. Source delivery
to the requested `main` branch uses `[skip ci]` to suppress the existing
multi-GPU packaging/upload workflow. Real-game, moving-scene, resize and FG/HUD
qualification remains pending. Strict historical golden mismatches are recorded
unchanged, and fitted/INT4 modes still lack independent numerical references.
