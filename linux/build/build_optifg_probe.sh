#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
mkdir -p artifacts/optifg-probe
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Wno-missing-field-initializers \
    linux/test/optifg_probe.cpp -o artifacts/optifg-probe/optifg_probe.exe \
    -ld3d11 -ldxgi -ld3dcompiler -static -static-libgcc -static-libstdc++ -Wl,--no-insert-timestamp
