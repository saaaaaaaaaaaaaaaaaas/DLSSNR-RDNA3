#!/usr/bin/env python3
"""Bundle matching CI binaries and their source/build instructions, without models or third-party runtimes."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import zipfile

from prepare_optiscaler import PIN

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--consumer", required=True, type=Path, help="consumer CI artifact directory")
    parser.add_argument("--producer", required=True, type=Path, help="producer CI artifact directory")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    subprocess.run(["git", "diff", "--exit-code", "HEAD", "--", "linux", "fetch_deps.sh",
                    ".github/workflows/optifg.yml", "docs/RESHADE-OPTIFG.md"], cwd=ROOT,
                   check=True, stdout=subprocess.DEVNULL)
    provenance = f"OptiScaler {PIN} + DLSSNR bridge {commit}"
    for folder in (args.consumer, args.producer):
        if (folder / "source.txt").read_text(encoding="utf-8-sig").strip() != provenance:
            raise SystemExit(f"{folder}: binaries must be from CI for {commit}")
    inputs = {
        "bin/OptiScaler.dll": args.consumer / "OptiScaler.dll",
        "bin/dlssnr_amd.addon64": args.producer / "dlssnr_amd.addon64",
        "bin/optifg_probe.exe": args.producer / "optifg_probe.exe",
        "bin/optifg_probe_dx10.exe": args.producer / "optifg_probe_dx10.exe",
        "licenses/COPYING.OptiScaler": args.consumer / "COPYING.OptiScaler",
        "licenses/LICENSE.DLSSNR": ROOT / "LICENSE",
        "README.txt": ROOT / "linux/optifg/DEVELOPMENT.txt",
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="optifg-package-") as tmp:
        source = Path(tmp) / "source.tar.gz"
        # These tracked paths contain the producer, receiver overlay and build
        # recipes; generated dependencies, captures and model weights stay out.
        subprocess.run(["git", "archive", "--format=tar.gz", "--output", str(source), "HEAD",
                        "linux", "fetch_deps.sh", "LICENSE", "README.md", "README-RDNA3.md",
                        ".github/workflows/optifg.yml", "docs/RESHADE-OPTIFG.md"], cwd=ROOT, check=True)
        inputs["source/DLSSNR-RDNA3-source.tar.gz"] = source
        manifest = {"bridge_commit": commit, "optiscaler_commit": PIN, "files": {}}
        temporary = args.output.with_name(args.output.name + ".tmp")
        with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED) as out:
            for name, path in inputs.items():
                data = path.read_bytes()
                out.writestr(name, data)
                manifest["files"][name] = hashlib.sha256(data).hexdigest()
            out.writestr("manifest.json", json.dumps(manifest, indent=2) + "\n")
        temporary.replace(args.output)
    digest = hashlib.sha256(args.output.read_bytes()).hexdigest()
    args.output.with_name(args.output.name + ".sha256").write_text(f"{digest}  {args.output.name}\n")
    print(f"{args.output}: {len(inputs)} files, bridge {commit}")


if __name__ == "__main__":
    main()
