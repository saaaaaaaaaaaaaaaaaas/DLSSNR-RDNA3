#!/usr/bin/env python3
"""Prove the Z1 build changes only its two selected pipelines and profile marker.

Usage: check_rdna3_profiles.py GENERIC_DIR Z1_DIR
Run on every network overlay too: derived INT4 kernels must remain unchanged.
Numerical equivalence still requires the GPU frame/layer checks.
"""
import sys
from pathlib import Path


def check(generic, tuned):
    original = {p.relative_to(generic): p.read_bytes() for p in generic.rglob('*') if p.is_file()}
    candidate = {p.relative_to(tuned): p.read_bytes() for p in tuned.rglob('*') if p.is_file()}
    marker = Path('gpu-profile.txt')
    assert candidate.pop(marker) == b'z1-extreme\n', 'missing/wrong GPU profile marker'
    assert original.keys() == candidate.keys(), 'profile changed pipeline/marker inventory'
    changed = {str(k) for k in original if original[k] != candidate[k]}
    assert changed == {'g_attn.spv', 'g_gemmproj.spv'}, changed
    print(f'{tuned.name}: only attn/gemmproj change; host constants, temporal passes and INT4 kernels identical')


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    check(Path(sys.argv[1]), Path(sys.argv[2]))
