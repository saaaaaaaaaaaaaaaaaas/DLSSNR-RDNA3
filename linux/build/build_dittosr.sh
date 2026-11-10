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
for pass in convert motion upscale sharpen; do
    cp -- "linux/shaders/dittosr/$pass.hlsl" "$tmp/$pass.slang"
    "$slangc" "$tmp/$pass.slang" -I linux/shaders/dittosr -DDITTO_VK=1 -DDITTO_FP16=0 -DANTI_SHIMMER=0 \
        -target spirv -profile glsl_460 -entry main -stage compute -O2 \
        -fvk-b-shift 0 all -fvk-t-shift 10 all -fvk-u-shift 20 all -fvk-s-shift 30 all \
        -o "$out/renodx_ditto_$pass.spv" > "$tmp/log" 2>&1 || { cat "$tmp/log" >&2; exit 1; }
done
"$glslang" -V --target-env vulkan1.1 linux/shaders/dittosr/prepare.comp \
    -o "$out/renodx_ditto_prepare.spv" > "$tmp/log" || { cat "$tmp/log" >&2; exit 1; }
cp -- linux/shaders/dittosr/LICENSE "$out/DittoSR-LICENSE.txt"
cp -- linux/shaders/dittosr/licenses/LICENSE-snapdragon-gsr.txt "$out/Snapdragon-GSR-LICENSE.txt"
echo "$out: Ditto SR shaders and notices"
