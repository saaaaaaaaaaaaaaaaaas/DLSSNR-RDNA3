#!/usr/bin/env bash
# CPU checks only; no model, Vulkan device or game is executed.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
out=build/rdna3-port-checks
mkdir -p "$out"
bash linux/build/check_paths.sh
bash linux/build/check_imgui.sh
g++ -std=c++17 -O2 -Itoolchain/Vulkan-Headers/include -Ilinux/src/core \
    linux/test/rdna3_descriptors.cpp -lvulkan -o "$out/rdna3_descriptors"
"$out/rdna3_descriptors"
g++ -std=c++17 -O2 -pthread -Ilinux/src/core \
    linux/test/control_presentation.cpp -o "$out/control_presentation"
"$out/control_presentation"
g++ -std=c++17 -O2 -Ilinux/src/core linux/test/mkplan.cpp \
    linux/src/core/nr_native_plan.cpp -o "$out/mkplan"
python3 linux/build/build_network.py rdna3 --ultra --out "$out/ultra"
python3 linux/test/check_rdna3_build.py "$out/ultra"
python3 linux/test/check_rdna3_qkv.py "$out/ultra" --planner "$out/mkplan"
python3 linux/package/make_ini.py linux/src/pe/nr_pe_config.cpp "$out/ini"
python3 - "$out/ini" <<'PY'
import configparser
import sys
from pathlib import Path
for name, method in [('optiscaler', 0), ('optiscaler-renodx', 0), ('addon', 1)]:
    cfg = configparser.ConfigParser(interpolation=None)
    cfg.read(Path(sys.argv[1]) / f'dlssnr-amd-{name}.ini')
    assert cfg.getint('RenoDX', 'NRFastMethod') == method, name
    assert cfg.getint('RenoDX', 'NRFastInterleavedPreset') == 1, name
    assert cfg.has_section('Reuse'), name
print('INI route defaults, Interleaved preset and temporal reuse: PASS')
PY
