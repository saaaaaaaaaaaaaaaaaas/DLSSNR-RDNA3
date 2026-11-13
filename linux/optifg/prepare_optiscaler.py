#!/usr/bin/env python3
"""Apply the reviewed bridge patch and overlay to the pinned OptiScaler source."""
from pathlib import Path
import argparse
import shutil
import subprocess

PIN = "e7e3872d410f704479d94d318d68d163b6b58947"
ROOT = Path(__file__).resolve().parent

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="private OptiScaler checkout at the documented pin")
    args = parser.parse_args()
    source = args.source.resolve()
    def git(*options, **kwargs):
        return subprocess.run(["git", "-C", str(source), *options], check=True, **kwargs)
    commit = git("rev-parse", "HEAD", capture_output=True, text=True).stdout.strip()
    if commit != PIN:
        raise SystemExit(f"Expected OptiScaler {PIN}, found {commit}; use a separate checkout")
    patch = ROOT / "optiscaler.patch"
    check = subprocess.run(["git", "-C", str(source), "apply", "--check", str(patch)], capture_output=True)
    if check.returncode == 0:
        git("apply", str(patch))
    else:
        # Repeatable for rebuilds, but never silently accept a partly applied patch.
        git("apply", "--reverse", "--check", str(patch))
    target = source / "OptiScaler/inputs/FG/reshade"
    target.mkdir(parents=True, exist_ok=True)
    for folder in (ROOT / "include", ROOT / "receiver"):
        for entry in sorted(folder.iterdir()):
            if entry.is_file():
                shutil.copyfile(entry, target / entry.name)
    print(f"Prepared OptiScaler {PIN} with ReShade bridge ABI 1: {source}")

if __name__ == "__main__":
    main()
