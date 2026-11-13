#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
mkdir -p artifacts/optifg-probe
for api in dx11 dx10; do
    flags=(); name=optifg_probe
    [[ "$api" == dx10 ]] && { flags+=(-DOPTI_PROBE_DX10=1); name=optifg_probe_dx10; }
    x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Wno-cast-function-type -Wno-missing-field-initializers \
        "${flags[@]}" linux/test/optifg_probe.cpp -o "artifacts/optifg-probe/$name.exe" \
        -ld3d11 -ld3d10 -ldxgi -ld3dcompiler -static -static-libgcc -static-libstdc++ -Wl,--no-insert-timestamp
done
