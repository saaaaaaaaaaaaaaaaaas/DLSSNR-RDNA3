#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
mkdir -p build/optifg-test
g++ -std=c++17 -Wall -Wextra -Werror linux/test/optifg_contract.cpp -o build/optifg-test/contract
build/optifg-test/contract
printf '#include "opti_reshade_bridge.h"\nint main(void) { return 0; }\n' > build/optifg-test/abi.c
gcc -std=c11 -Wall -Wextra -Werror -Ilinux/optifg/include -c build/optifg-test/abi.c -o build/optifg-test/abi.o
x86_64-w64-mingw32-g++ -std=c++17 -Wall -Wextra -Werror -c linux/test/optifg_contract.cpp -o build/optifg-test/contract-win64.o
python3 -m py_compile linux/optifg/prepare_optiscaler.py
