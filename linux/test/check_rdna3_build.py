#!/usr/bin/env python3
"""CPU-only checks of compiled gfx11 variants. Does not establish GPU correctness.
Usage: check_rdna3_build.py <network-directory> [...]
"""
import collections
import struct
import sys
from pathlib import Path


def check(path):
    words = struct.unpack('<%dI' % (path.stat().st_size // 4), path.read_bytes())
    assert words[0] == 0x07230203, path
    floats, matrices, ints = {}, {}, {}
    ops = []
    i = 5
    while i < len(words):
        n, op = words[i] >> 16, words[i] & 65535
        assert n and i + n <= len(words), path
        a = words[i + 1:i + n]
        if op == 22: floats[a[0]] = a[1]     # OpTypeFloat
        if op == 21: ints[a[0]] = a[1]       # OpTypeInt
        if op == 4456: matrices[a[0]] = a[1] # OpTypeCooperativeMatrixKHR component
        if op == 4459: ops.append(a)         # result type, id, A, B, C, optional flags
        i += n
    assert all(v in (16, 32, 64) for v in floats.values()), f'{path}: native FP8 type on gfx11'
    integer = [a for a in ops if matrices.get(a[0]) in ints]
    if integer:
        assert Path(str(path) + '.iu4').is_file(), f'{path}: INT4 pipeline has no rewrite marker'
        marked = sum(bool(a[5] & 16) if len(a) > 5 else False for a in integer)
        assert marked * 2 == len(integer), f'{path}: {marked} marked of {len(integer)} integer WMMAs'
    return len(integer)


def main():
    for root in map(Path, sys.argv[1:]):
        assert root.is_dir(), root
        counts = collections.Counter()
        for path in root.rglob('*.spv'):
            counts['shaders'] += 1
            counts['integer_wmma'] += check(path)
        assert counts['shaders'], root
        markers = dict(line.split() for line in (root / 'shader-constants.txt').read_text().splitlines()[1:])
        assert markers['kswap'] == markers['noise_buffer'] == markers['post_no_out1'] == '1', root
        for path in root.glob('*.spv.iu4'):
            assert Path(str(path)[:-4]).is_file(), path
        print(f'{root}: PASS, {counts["shaders"]} shaders, {counts["integer_wmma"]} paired integer WMMAs')


if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    main()
