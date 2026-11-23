#!/usr/bin/env bash
# Release entry point for this branch: Linux/Proton, Z1 Extreme Turbo, x86_64.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
[[ ${NR_GPU:-rdna3} == rdna3 ]] || { echo 'Z1 Extreme releases require NR_GPU=rdna3' >&2; exit 2; }
[[ ${NR_ARCH:-x86_64} == x86_64 ]] || { echo 'Z1 Extreme releases require NR_ARCH=x86_64' >&2; exit 2; }
[[ ${NR_SHADER_PROFILE:-z1-extreme-turbo} == z1-extreme-turbo ]] || { echo 'Z1 Extreme releases require the Turbo profile' >&2; exit 2; }
export NR_GPU=rdna3 NR_ARCH=x86_64 NR_SHADER_PROFILE=z1-extreme-turbo
unset NR_MODEL NR_MODEL_SMALL NR_INT4 NR_PROGRAMS_ONLY NR_LAYER32
if [[ ${1:-} == --check-target ]]; then
    echo 'RDNA3 / x86_64 / z1-extreme-turbo / model-free'
    exit 0
fi
out=$(realpath -m -- "${1:-artifacts/z1e-release}")
case "$out" in "$(pwd)"/*) ;; *) echo 'release output must be inside this worktree' >&2; exit 2;; esac
version=$(bash linux/build/version.sh)
[[ "$version" != *-dirty ]] || { echo 'commit release inputs before building' >&2; exit 2; }
mkdir -p -- "$out/release"
echo "Building $version: RDNA3 / x86_64 / Z1 Extreme Turbo"
bash fetch_deps.sh
bash linux/build/build_vulkan_loader.sh
bash linux/test/check_rdna3_port.sh
bash linux/build/build_package.sh "$out/package-build"
archive="linux/package/DLSS-NR-AMD-Vulcan-Linux-$version-Z1E-Turbo-Temporal-RDNA-3.tar.gz"
python3 linux/build/audit_z1e_release.py --archive "$archive" --out "$out/release"
echo "Release ready: $out/release"
