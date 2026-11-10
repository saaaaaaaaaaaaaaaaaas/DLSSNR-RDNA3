#!/usr/bin/env bash
# Optional Ubuntu 24.04 x86_64 build prerequisites, extracted locally without installing packages.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
root=$PWD
mkdir -p toolchain/ubuntu-debs toolchain/ubuntu build
while IFS=$'\t' read -r package version sum filename; do
    [[ $package == \#* || -z $package ]] && continue
    if [[ ! -f toolchain/ubuntu-debs/$filename ]]; then
        (cd toolchain/ubuntu-debs && apt-get download "$package=$version")
    fi
    echo "$sum  toolchain/ubuntu-debs/$filename" | sha256sum -c --quiet -
    dpkg-deb -x "toolchain/ubuntu-debs/$filename" toolchain/ubuntu
done < linux/build/ubuntu-24.04-tools.tsv
for compiler in gcc g++; do
    ln -sfn "x86_64-w64-mingw32-$compiler-posix" "toolchain/ubuntu/usr/bin/x86_64-w64-mingw32-$compiler"
done
# Ubuntu's 7z launcher uses /usr/lib; resolve the extracted executable locally.
rm -f -- toolchain/ubuntu/usr/bin/7z
cat > toolchain/ubuntu/usr/bin/7z <<'LAUNCHER'
#!/bin/sh
exec "$(dirname -- "$0")/../lib/7zip/7z" "$@"
LAUNCHER
chmod +x toolchain/ubuntu/usr/bin/7z
# The dev package's unversioned symlink needs the host's existing Vulkan loader, never a GPU driver download.
loader=$(readlink -f /usr/lib/x86_64-linux-gnu/libvulkan.so.1)
[[ -f $loader ]] || { echo 'The native tests need the host Vulkan loader.' >&2; exit 1; }
ln -sfn "$loader" toolchain/ubuntu/usr/lib/x86_64-linux-gnu/libvulkan.so.1
cat > build/local-tools.env <<'ENV'
NR_LOCAL_TOOLS_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
export PATH="$NR_LOCAL_TOOLS_ROOT/toolchain/ubuntu/usr/bin:$PATH"
export LD_LIBRARY_PATH="$NR_LOCAL_TOOLS_ROOT/toolchain/ubuntu/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export NR_MINGW=x86_64-w64-mingw32-g++-posix
export NR_MINGW_CC=x86_64-w64-mingw32-gcc-posix
export CPLUS_INCLUDE_PATH="$NR_LOCAL_TOOLS_ROOT/toolchain/Vulkan-Headers/include${CPLUS_INCLUDE_PATH:+:$CPLUS_INCLUDE_PATH}"
export LIBRARY_PATH="$NR_LOCAL_TOOLS_ROOT/toolchain/ubuntu/usr/lib/x86_64-linux-gnu${LIBRARY_PATH:+:$LIBRARY_PATH}"
ENV
echo 'Local tools ready. Source build/local-tools.env before building.'
