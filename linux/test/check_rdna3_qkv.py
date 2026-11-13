#!/usr/bin/env python3
"""CPU regression for Ultra's plain C=256 encoder/decoder routing.

Use the compiled shader markers, native frame plans and shipped calibration
tables. This checks table availability, not GPU arithmetic or image quality.
"""
import argparse
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('shaders', type=Path)
    ap.add_argument('--planner', type=Path, required=True, help='compiled linux/test/mkplan.cpp')
    a = ap.parse_args()
    qkv = set((a.shaders / 'int4-qkv.txt').read_text().split())
    routes = []
    for line in (a.shaders / 'block-routes.txt').read_text().splitlines():
        left, right = line.split(':')
        suffix, *kernels = left.split()
        routes.append((suffix, set(kernels), set(map(int, right.split()))))
    # Locate tables without assuming their directory depth.
    tables = {p.name for p in (ROOT / 'linux/data/int4-ultra/rdna4').rglob('*.y.aq.bin')}
    for w, h in [(1280, 720), (1920, 1080), (1918, 1078), (2560, 1440), (3840, 2160)]:
        plan = subprocess.check_output([str(a.planner.resolve()), str(w), str(h)], text=True)
        encoder = decoder = 0
        for line in plan.splitlines():
            fields = line.split()
            if fields[18] != 'cc_tinlayout_fused_swin_8h_256_8_fp8':
                continue
            block, layer = map(int, fields[:2])
            kernel = 'fswin256'
            for suffix, kernels, blocks in routes:
                if block in blocks and kernel in kernels:
                    kernel += suffix
            assert (a.shaders / f'g_{kernel}.spv').is_file(), kernel
            expected = f'b{block}l{layer}.y.aq.bin' in tables
            assert (kernel in qkv) == expected, (
                f'{w}x{h}: b{block}l{layer} uses {kernel}, '
                f'INT4 QKV={kernel in qkv}, tables={expected}')
            encoder += expected
            decoder += not expected
        assert encoder and decoder, 'the plan must exercise both sides of the shared plain kernel'
        print(f'{w}x{h}: PASS ({encoder} encoder INT4 QKV, {decoder} decoder FP16 QKV layers)')


if __name__ == '__main__':
    main()
