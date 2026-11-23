#!/usr/bin/env bash
# DittoSR's Vulkan FP32 passes, with the same register shifts as its upstream Vulkan build.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
out=$(realpath -m -- "${1:?usage: build_dittosr.sh <shader directory>}")
slangc=${NR_SLANGC:-toolchain/slang-2026.19/bin/slangc}
glslang=${NR_GLSLANG:-toolchain/glslang/bin/glslang}
mkdir -p -- "$out"
tmp=$(mktemp -d "${TMPDIR:-/tmp}/dittosr.XXXXXX")
trap 'rm -rf -- "$tmp"' EXIT
src=linux/shaders/dittosr
passes=(convert motion upscale sharpen aoprep aomipall aomain aofilter aotemporal aocacao aolut aocurve aocompose)
anti=1
if [[ "${NR_GPU:-rdna4}" != rdna3 ]]; then
    src=linux/shaders/dittosr-legacy
    passes=(convert motion upscale sharpen)
    anti=0
fi
for pass in "${passes[@]}"; do
    cp -- "$src/$pass.hlsl" "$tmp/$pass.slang"
    "$slangc" "$tmp/$pass.slang" -I "$src" -DDITTO_VK=1 -DDITTO_FP16=0 -DANTI_SHIMMER="$anti" -DAO_WG=16 -DCACAO_FUSED=1 \
        -target spirv -profile glsl_460 -entry main -stage compute -O2 \
        -fvk-b-shift 0 all -fvk-t-shift 10 all -fvk-u-shift 20 all -fvk-s-shift 30 all \
        -o "$out/renodx_ditto_$pass.spv" > "$tmp/log" 2>&1 || { cat "$tmp/log" >&2; exit 1; }
done
"$glslang" -V --target-env vulkan1.1 "$src/prepare.comp" \
    -o "$out/renodx_ditto_prepare.spv" > "$tmp/log" || { cat "$tmp/log" >&2; exit 1; }
if [[ "${NR_GPU:-rdna4}" == rdna3 ]]; then
    "$glslang" -V --target-env vulkan1.1 linux/shaders/dittosr/residual.comp \
        -o "$out/renodx_ditto_residual.spv" > "$tmp/log" || { cat "$tmp/log" >&2; exit 1; }
fi
cp -- linux/shaders/dittosr/LICENSE "$out/DittoSR-LICENSE.txt"
cp -- linux/shaders/dittosr/licenses/LICENSE-snapdragon-gsr.txt "$out/Snapdragon-GSR-LICENSE.txt"
cp -- linux/shaders/dittosr/licenses/LICENSE-cortao.txt "$out/CORTAO-LICENSE.txt"
cp -- linux/shaders/dittosr/licenses/LICENSE-fidelityfx-sdk.txt "$out/FidelityFX-CACAO-LICENSE.txt"
echo "$out: Ditto shaders and notices"
