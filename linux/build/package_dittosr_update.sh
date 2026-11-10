#!/usr/bin/env bash
# Package an already-built NR Control for an existing Linux/Proton RDNA3 installation.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
source linux/build/reproducible.sh
asi=${1:-artifacts/optiscaler/asi}
out=${2:-artifacts/dittosr-update}
[[ -f "$asi/dlssnr-amd.asi" ]] || { echo 'Build NR Control with build_asi.sh first.' >&2; exit 1; }
version=$(bash linux/build/version.sh)
commit=$(git rev-parse --short HEAD)
python3 - "$asi/dlssnr-amd.asi" "$version" "$commit" <<'PY'
from pathlib import Path
import sys
data = Path(sys.argv[1]).read_bytes()
if not any(stamp.encode() + b'\0' in data for stamp in sys.argv[2:]):
    sys.exit('The ASI does not identify this source revision. Rebuild it before packaging.')
PY
mkdir -p -- "$out"
stage=$(mktemp -d "$out/package.XXXXXX")
trap 'rm -rf -- "$stage"' EXIT
mkdir -p -- "$stage/OptiScaler/plugins" "$stage/dlssnr-amd/shaders/renodx" "$stage/licenses"
cp -- "$asi/dlssnr-amd.asi" "$stage/OptiScaler/plugins/"
for pass in prepare convert motion upscale sharpen; do
    cp -- "$asi/shaders/renodx/renodx_ditto_$pass.spv" "$stage/dlssnr-amd/shaders/renodx/"
done
cp -- "$asi/shaders/renodx/DittoSR-LICENSE.txt" "$asi/shaders/renodx/Snapdragon-GSR-LICENSE.txt" \
    "$stage/dlssnr-amd/shaders/renodx/"
cp -- LICENSE "$stage/licenses/DLSSNR-AMD-LICENSE.txt"
cp -- toolchain/imgui-nr/LICENSE.txt "$stage/licenses/DearImGui-LICENSE.txt"
cp -- toolchain/fonts/inter-4.1/LICENSE.txt "$stage/licenses/Inter-LICENSE.txt"
cp -- artifacts/ref/DLSS5-Feeder/external/minhook/LICENSE.txt "$stage/licenses/MinHook-LICENSE.txt"
cp -- linux/shaders/renodx/LICENSE "$stage/licenses/RenoDX-LICENSE.txt"
cp -- linux/shaders/passes/include/fsr1/LICENSE.txt "$stage/licenses/FidelityFX-FSR1-LICENSE.txt"
cp -- linux/package/DITTOSR-UPDATE.txt "$stage/README.txt"
archive="$out/NR-Control-DittoSR-$version-x86_64-rdna3.tar.gz"
tar --sort=name --mtime="@$SOURCE_DATE_EPOCH" --owner=0 --group=0 --numeric-owner -C "$stage" -cf - . | gzip -n > "$archive"
python3 linux/build/check_package.py "$archive"
(cd -- "$out" && sha256sum -- "$(basename -- "$archive")" > "$(basename -- "$archive").sha256")
echo "$archive"
