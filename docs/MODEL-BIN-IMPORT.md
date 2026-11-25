# Importing custom small-model weights

The importer converts a custom `WEIGHTS_CC_SMALL_FP8` resource into the
`NRMODEL1` container used by this project. It runs on the CPU with Python 3
and the existing model unpackers; it does not change the runtime or shaders.

## Acceptance matrix

| Feature | Required behavior | Verification |
| --- | --- | --- |
| Raw FP8 resource | Validate the complete 310.9.16 small-model layout before unpacking | Private FP8 resource and malformed-input checks |
| Existing model pack | Accept the same entry names and sizes as the stock small model | Stock model reproduced exactly |
| A folder of variants | Select the supported FP8 layout by its contents; reject ambiguous sets | Private FP8/FP16/INT8 set; synthetic ambiguity check |
| FP16 and INT8 resources | Reject with an explanation; never reinterpret or silently quantize them | Both private alternative layouts rejected |
| Normal output | Preserve the existing unpackers' output bytes | Exact comparison with an independently prepared custom pack |
| Experimental output | Never XOR a stock-model delta onto custom weights | Stock output checksum and all 255 entries checked |
| File handling | Stage outputs together; refuse an existing destination; leave source files alone | Failure and overwrite checks |
| Distribution | Commit code and metadata only; no model weights | Generated weights stay under ignored artifacts/ |

The stock experimental network changes activations and attention, and its
replacement weights were fitted to the stock model. A full experimental
variant of a trained custom model requires new calibration. Reusing the
stock fitted entries produces a hybrid, which must be identified as such.

## Usage

Run from the repository root, replacing `INPUT`, `ORIGINAL`, and
`NEW_DIRECTORY` with paths. INPUT can be a raw FP8 `.bin`, a directory of
`.bin` variants, or an existing **normal** `CC_SMALL_FP8.bin` pack:

```sh
python3 linux/package/model-tools/import_small_model.py INPUT \
    --reference-small ORIGINAL --output NEW_DIRECTORY
```

`ORIGINAL` must be an unchanged stock `CC_SMALL_FP8.bin` extracted from
DLL 310.9.16 (SHA-256 `7e966e2639dbec6596aa0bac3b8761137d2c40ca8cb4aca30df7ca10043c6e33`),
or the directory containing that file. Keep it separately if the game's
installed model has already been replaced. The stock pack can also be used
as INPUT without `--reference-small` to reproduce both stock outputs.

For only the normal model, no original reference is needed:

```sh
python3 linux/package/model-tools/import_small_model.py INPUT \
    --small-only --output NEW_DIRECTORY
```

Both forms accept absolute paths and work from any current directory when
the script path is absolute. No GPU, Python packages, compiler or shader
build is required. The tool is included in Linux packages' `model-tools/`.
In an unpacked package the command starts with
`python3 model-tools/import_small_model.py` instead.

The output directory must not exist. The importer stages the results,
checks entry names, sizes, bounds and finite values, then publishes the
directory with the model file(s), an `import-report.json`, and unpacker
logs for raw inputs. It does not install them into a game. Back up existing
models before copying the generated files to the game's `dlssnr-amd/`.
Use `[Network] Model=CC_SMALL_FP8` or `CC_SMALL_FP8_EXPERIMENTAL` with the
project's corresponding shaders already installed.

Directory selection uses the file contents, not a filename's CRC or the
word "FP8". Different supported models in one directory are ambiguous and
require an explicit input file. FP16/INT8 resources made for modified NVIDIA
DLLs have a different layout and are rejected. Different architectures and
other model revisions require another importer.

## Experimental hybrid

The supplied stock experimental delta describes **64 replacement entries**.
The tool first recovers their fitted bytes from the unchanged original and
verifies the complete stock experimental checksum. It then replaces those
64 entries in the custom model, preserving the other **191 entries** exactly.
The report lists every replacement and records both output hashes.

This is a hybrid approximation, not a recalibration of the custom model.
Compatibility of the container does not establish image quality, the
strength of an effect, numerical equivalence to a custom NVIDIA DLL, or
game performance. A full custom experimental model requires new fitting
for the changed activations and fixed-attention layers. Never apply the
stock bytewise XOR delta directly to custom weights.

## Verification

```sh
python3 linux/test/test_import_small_model.py -v
```

The ten CPU tests cover malformed resources and packs, layout selection,
ambiguous directories, non-finite weights, invalid deltas, reference and
fitted checksums, entry provenance, and refusal to overwrite output.
They use synthetic fixtures and include no model weights.

Local checks with private input files additionally reproduced the stock
normal and experimental model hashes, compared the imported custom normal
pack byte for byte with an independent conversion, and verified each hybrid
entry against its custom or fitted reference. GPU inference and visual
validation remain unrun. No runtime, shader, golden, or original extraction
checksum was changed; custom weights need their own visual reference.
