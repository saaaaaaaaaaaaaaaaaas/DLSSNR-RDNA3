#!/usr/bin/env python3
"""Import custom FP8 small-model weights into this project's model containers.

    import_small_model.py INPUT --reference-small ORIGINAL --output NEW_DIRECTORY
    import_small_model.py INPUT --small-only --output NEW_DIRECTORY

INPUT is a raw WEIGHTS_CC_SMALL_FP8 resource, an existing normal NRMODEL1 pack,
or a directory containing resource variants. Experimental output is a hybrid:
custom entries plus the stock network's fitted replacement entries. It is not
a newly calibrated experimental version of the custom model.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True

from inspect_nr import FormatError, parse_weights
import unpack_postblock
import unpack_preblock
import unpack_splitswin
import unpack_swin_family
import unpack_vit


HERE = Path(__file__).resolve().parent
SMALL = "CC_SMALL_FP8.bin"
EXPERIMENTAL = "CC_SMALL_FP8_EXPERIMENTAL.bin"
STOCK_SHA256 = "7e966e2639dbec6596aa0bac3b8761137d2c40ca8cb4aca30df7ca10043c6e33"
MAX_INPUT_BYTES = 512 * 1024 * 1024


def digest(data):
    return hashlib.sha256(data).hexdigest()


def schema():
    """Derive sizes from the same region maps that the runtime's unpackers use."""
    graph = json.loads((HERE / "descriptor-small.json").read_text())
    raw, packed, matrices, floats = {}, {}, {}, {}

    def add(folder, base, name, size, kind):
        key = f"{folder}/{base}.{name}.bin"
        packed[key] = size
        if kind == "matrix":
            matrices[key] = [(0, size)]
        elif kind in ("fp16", "f32"):
            floats[key] = "<e" if kind == "fp16" else "<f"

    for block in graph["blocks"]:
        for layer in block["layers"]:
            base = f"block{block['index']}.layer{layer['index']}.layer"
            family = layer["type"]
            source_matrices = []
            if family.startswith("CCTinlayoutFused"):
                if "PreBlock" in family:
                    folder, regions = "unpacked-preblock", unpack_preblock.REGIONS
                elif "PostBlock" in family:
                    folder, regions = "unpacked-postblock", unpack_postblock.REGIONS
                else:
                    ci, co = layer["input_channels"], layer["output_channels"]
                    c = min(ci, co)
                    folder = "unpacked"
                    regions = unpack_swin_family.regions(c, c // 32, ci, co)
                raw[base] = regions[-1][2]
                for name, start, end, *_, kind in regions:
                    add(folder, base, name, end - start, kind)
                    if kind == "matrix":
                        source_matrices.append((start, end - start))
            elif family in unpack_splitswin.LAYOUT:
                end = 0
                for name, kind, offset, a, b in unpack_splitswin.LAYOUT[family]:
                    size = a * b if kind == "matrix" else a * (2 if kind == "fp16" else 1)
                    end = max(end, offset + size)
                    add("unpacked-splitswin", base, name, size, kind)
                    if kind == "matrix":
                        source_matrices.append((offset, size))
                raw[base] = end
            elif family in unpack_vit.LAYOUT:
                n, k, head, tail, split = unpack_vit.LAYOUT[family]
                # FFN expand has the original unused 16-byte trailer.
                raw[base] = head + n * k + 2 * tail + (16 if family == "CCVit1DFfnExpand" else 0)
                source_matrices.append((head, n * k))
                if head:
                    add("unpacked-vit", base, "header", head, "raw")
                for name in (["q", "k", "v"] if split == 3 else ["weight"]):
                    add("unpacked-vit", base, name, n * k // split, "matrix")
                if tail:
                    add("unpacked-vit", base, "skip_weight", 2 * tail, "fp16")
            elif family == "CCVit1DAttention":
                raw[base] = 2
            else:
                raise FormatError(f"unsupported descriptor family: {family}")
            key = f"inventory/weights/{base}.bin"
            packed[key] = raw[base]
            matrices[key] = source_matrices
    raw["block30.layer0.blend_scale"] = 2
    names = [line.strip() for line in (HERE / "model-files-small.txt").read_text().splitlines()
             if line.strip() and not line.startswith("#")]
    return raw, {n: packed[n] for n in names}, {n: matrices[n] for n in names if n in matrices}, {
        n: fmt for n, fmt in floats.items() if n in names}


def read_input(path):
    if not path.is_file():
        raise FormatError(f"not a regular input file: {path}")
    if path.stat().st_size > MAX_INPUT_BYTES:
        raise FormatError(f"input exceeds {MAX_INPUT_BYTES // 2**20} MiB: {path}")
    return path.read_bytes()


def validate_resource(data, expected):
    records = parse_weights(data)
    actual = {r["name"]: r for r in records}
    if actual.keys() != expected.keys():
        missing, extra = sorted(expected.keys() - actual.keys()), sorted(actual.keys() - expected.keys())
        raise FormatError(f"not the supported small-model resource (missing {missing[:3]}, extra {extra[:3]})")
    for name, size in expected.items():
        r = actual[name]
        if r["payload_size"] != size:
            raise FormatError(f"{name}: {r['payload_size']} bytes, expected FP8 layout's {size}; "
                              "FP16 and INT8 replacement-DLL layouts are not supported; use the FP8 variant")
        if (r["type_id"], r["layout_id"], r["device_id"], r["storage_shape"]) != (0, 0, 1, [size // 2]):
            raise FormatError(f"{name}: unsupported resource metadata")
    return records


def parse_pack(data, expected):
    if len(data) < 16 or data[:8] != b"NRMODEL1":
        raise FormatError("not an NRMODEL1 model pack")
    count, reserved = struct.unpack_from("<II", data, 8)
    if reserved or count != len(expected):
        raise FormatError(f"not the normal small-model pack ({count} entries, expected {len(expected)})")
    index, at = {}, 16
    for _ in range(count):
        if at + 4 > len(data):
            raise FormatError("truncated model index")
        n, = struct.unpack_from("<I", data, at)
        at += 4
        if not 0 < n <= 4096 or at + n + 16 > len(data):
            raise FormatError("invalid model entry name length")
        try:
            name = data[at:at + n].decode("utf-8")
        except UnicodeDecodeError as exc:
            raise FormatError("invalid model entry name") from exc
        at += n
        off, size = struct.unpack_from("<QQ", data, at)
        at += 16
        if name in index or name not in expected or size != expected[name]:
            raise FormatError(f"unexpected, duplicate or incorrectly sized model entry: {name}")
        if off % 256 or off + size > len(data):
            raise FormatError(f"invalid model data range: {name}")
        index[name] = off, size
    end = at
    for off, size in sorted(index.values()):
        if off < end:
            raise FormatError("overlapping model entries or index")
        end = off + size
    if end != len(data):
        raise FormatError("trailing model data")
    return index


def validate_values(data, index, matrices, floats):
    for name, ranges in matrices.items():
        off, _ = index[name]
        for start, size in ranges:
            values = data[off + start:off + start + size]
            if b"\x7f" in values or b"\xff" in values:
                raise FormatError(f"non-finite FP8 weight in {name}")
    for name, fmt in floats.items():
        off, size = index[name]
        if any(not math.isfinite(v) for (v,) in struct.iter_unpack(fmt, data[off:off + size])):
            raise FormatError(f"non-finite weight in {name}")


def select_input(path, raw_sizes, pack_sizes):
    """Use layout checks, never the filename's CRC, to choose a folder variant."""
    candidates = sorted(path.glob("*.bin")) if path.is_dir() else [path]
    supported, ignored = [], []
    for candidate in candidates:
        try:
            data = read_input(candidate)
            if data[:8] == b"NRMODEL1":
                parse_pack(data, pack_sizes)
                kind = "NRMODEL1"
            else:
                validate_resource(data, raw_sizes)
                kind = "WEIGHTS_CC_SMALL_FP8_FP8"
            supported.append((candidate, digest(data), kind))
        except (OSError, FormatError, struct.error) as exc:
            if not path.is_dir():
                raise FormatError(f"{candidate}: {exc}") from exc
            ignored.append({"path": str(candidate), "reason": str(exc)})
    if not supported:
        details = "; ".join(f"{Path(i['path']).name}: {i['reason']}" for i in ignored)
        raise FormatError(f"no supported FP8 small model in {path}" + (f" ({details})" if details else ""))
    if len({sha for _, sha, _ in supported}) != 1:
        raise FormatError("multiple different supported models; select a file explicitly: "
                          + ", ".join(str(p) for p, _, _ in supported))
    source, sha, kind = supported[0]
    return source, sha, kind, ignored


def unpack_resource(source, destination, stage, raw_sizes):
    data = read_input(source)
    records = validate_resource(data, raw_sizes)
    with tempfile.TemporaryDirectory(prefix="unpack-", dir=stage) as tmp:
        work = Path(tmp)
        (work / "inventory/weights").mkdir(parents=True)
        (work / "graph").mkdir()
        (work / "inventory/weights_ht.bin").write_bytes(data)
        (work / "inventory/manifest.json").write_text(json.dumps({"weights": records}))
        for r in records:
            # Names have already been checked against the descriptor's exact set.
            (work / "inventory/weights" / (r["name"] + ".bin")).write_bytes(
                data[r["payload_offset"]:r["payload_offset"] + r["payload_size"]])
        shutil.copyfile(HERE / "descriptor-small.json", work / "graph/descriptor.json")
        commands = [
            ["unpack_swin_family.py", "--artifacts", work, "--output", work / "unpacked", "--max-c", "256"],
            ["unpack_splitswin.py", "--artifacts", work, "--output", work / "unpacked-splitswin"],
            ["unpack_vit.py", "--artifacts", work, "--output", work / "unpacked-vit"],
            ["unpack_preblock.py", work / "inventory/weights/block0.layer0.layer.bin", work / "unpacked-preblock"],
            ["unpack_postblock.py", work / "inventory/weights/block30.layer0.layer.bin", work / "unpacked-postblock"],
            ["pack_model.py", "--root", work, "--list", HERE / "model-files-small.txt", "--out", destination],
        ]
        (stage / "logs").mkdir()
        for script, *args in commands:
            print(f"Running {script} ...", flush=True)
            result = subprocess.run([sys.executable, str(HERE / script), *map(str, args)],
                                    cwd=work, env=dict(os.environ, PYTHONDONTWRITEBYTECODE="1"),
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            (stage / "logs" / (script + ".log")).write_text(result.stdout)
            if result.returncode:
                raise FormatError(f"{script} failed:\n{result.stdout[-4000:]}")


def delta_entries(data, expected):
    if len(data) < 12 or data[:8] != b"NRXDIFF1":
        raise FormatError("invalid experimental delta")
    count, = struct.unpack_from("<I", data, 8)
    if not 0 < count <= len(expected):
        raise FormatError("invalid experimental delta entry count")
    at, result = 12, {}
    for _ in range(count):
        if at + 4 > len(data):
            raise FormatError("truncated experimental delta")
        n, = struct.unpack_from("<I", data, at)
        at += 4
        if not 0 < n <= 4096 or at + n + 8 > len(data):
            raise FormatError("invalid experimental delta entry")
        try:
            name = data[at:at + n].decode("utf-8")
        except UnicodeDecodeError as exc:
            raise FormatError("invalid experimental delta name") from exc
        at += n
        size, = struct.unpack_from("<Q", data, at)
        at += 8
        if name in result or name not in expected or size != expected[name] or at + size > len(data):
            raise FormatError(f"invalid experimental replacement: {name}")
        result[name] = data[at:at + size]
        at += size
    if at != len(data):
        raise FormatError("trailing experimental delta data")
    return result


def experimental_assets():
    # Source checkout and unpacked Linux package layouts, respectively.
    for folder in (HERE.parents[1] / "data/experimental", HERE.parent / "experimental"):
        delta = folder / "dlssnr-small-experimental.delta"
        if delta.is_file():
            sums = folder / "dlssnr-small-experimental.sha256"
            if not sums.is_file():
                sums = folder / (EXPERIMENTAL + ".sha256")
            return delta, sums
    raise FormatError("experimental delta not found next to the project/package")


def make_hybrid(custom, reference, expected, delta, fitted_sha256):
    if digest(reference) != STOCK_SHA256:
        raise FormatError("--reference-small must be the unchanged stock CC_SMALL_FP8.bin from DLL 310.9.16")
    custom_index, reference_index = parse_pack(custom, expected), parse_pack(reference, expected)
    replacements = delta_entries(delta, expected)
    fitted, hybrid = bytearray(reference), bytearray(custom)
    for name, difference in replacements.items():
        ref_off, size = reference_index[name]
        # Recover actual fitted bytes from their verified base; a bytewise XOR
        # against custom weights is not a numeric or calibrated weight change.
        replacement = (int.from_bytes(reference[ref_off:ref_off + size], "little")
                       ^ int.from_bytes(difference, "little")).to_bytes(size, "little")
        fitted[ref_off:ref_off + size] = replacement
        off, _ = custom_index[name]
        hybrid[off:off + size] = replacement
    if digest(fitted) != fitted_sha256:
        raise FormatError("experimental assets do not reproduce the stock experimental checksum")
    return bytes(hybrid), sorted(replacements)


def convert(args):
    raw_sizes, pack_sizes, matrices, floats = schema()
    output = args.output.expanduser().absolute()
    if output.exists() or output.is_symlink():
        raise FormatError(f"output already exists; choose a new directory: {output}")
    source, source_sha, kind, ignored = select_input(args.input.expanduser().resolve(), raw_sizes, pack_sizes)
    print(f"Selected {source} ({kind}).", flush=True)
    reference, delta, fitted_sha = None, None, None
    if not args.small_only:
        delta_path, sums_path = experimental_assets()
        delta = read_input(delta_path)
        fitted_sha = sums_path.read_text().split()[0]
        delta_entries(delta, pack_sizes)
        if args.reference_small:
            ref_path = args.reference_small.expanduser().resolve()
            if ref_path.is_dir():
                ref_path /= SMALL
            reference = read_input(ref_path)
            if digest(reference) != STOCK_SHA256:
                raise FormatError("--reference-small must be the unchanged stock CC_SMALL_FP8.bin from DLL 310.9.16")
        elif kind == "NRMODEL1" and source_sha == STOCK_SHA256:
            reference = read_input(source)
        else:
            raise FormatError("both outputs require --reference-small ORIGINAL_CC_SMALL_FP8.bin; "
                              "use --small-only to import only the normal model")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".model-import-", dir=output.parent) as tmp:
        stage = Path(tmp)
        normal = stage / SMALL
        if kind == "NRMODEL1":
            shutil.copyfile(source, normal)
        else:
            unpack_resource(source, normal, stage, raw_sizes)
        data = normal.read_bytes()
        index = parse_pack(data, pack_sizes)
        validate_values(data, index, matrices, floats)
        if digest(read_input(source)) != source_sha:
            raise FormatError("input changed during conversion; retry with an unchanged file")
        report = {"schema_version": 1, "source": str(source), "source_sha256": source_sha,
                  "input_format": kind, "ignored_variants": ignored, "gpu_validation": "not run",
                  "outputs": {SMALL: {"sha256": digest(data), "bytes": len(data), "entries": len(index),
                                      "mode": "custom", "stock_sha256": STOCK_SHA256}}}
        if not args.small_only:
            hybrid, replaced = make_hybrid(data, reference, pack_sizes, delta, fitted_sha)
            validate_values(hybrid, index, matrices, floats)
            (stage / EXPERIMENTAL).write_bytes(hybrid)
            report["outputs"][EXPERIMENTAL] = {
                "sha256": digest(hybrid), "bytes": len(hybrid), "entries": len(index), "mode": "hybrid",
                "reference_sha256": digest(reference), "stock_fitted_sha256": fitted_sha,
                "stock_fitted_entries": replaced, "custom_entries_preserved": len(index) - len(replaced),
                "note": "Stock fitted replacements plus custom remaining entries; not recalibrated for the custom model."}
            print(f"Experimental hybrid: {len(replaced)} stock fitted entries, "
                  f"{len(index) - len(replaced)} custom entries; image quality is unverified.")
        (stage / "import-report.json").write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n")
        if output.exists() or output.is_symlink():
            raise FormatError(f"output appeared during conversion: {output}")
        stage.rename(output)
    for name, details in report["outputs"].items():
        print(f"{output / name}\n  SHA-256 {details['sha256']}")
    print(f"Report: {output / 'import-report.json'}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("input", type=Path, help="FP8 resource, normal model pack, or folder of .bin variants")
    parser.add_argument("--output", "-o", required=True, type=Path, help="new output directory (never overwritten)")
    parser.add_argument("--reference-small", type=Path, help="unchanged original CC_SMALL_FP8.bin, or its directory")
    parser.add_argument("--small-only", action="store_true", help="omit the experimental hybrid and its reference requirement")
    args = parser.parse_args()
    try:
        convert(args)
    except (OSError, ValueError, struct.error) as exc:
        parser.exit(1, f"Error: {exc}\n")


if __name__ == "__main__":
    main()
