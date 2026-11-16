#!/usr/bin/env python3
"""Prove a Z1 build changes only its selected pipelines and profile marker.

Usage: check_rdna3_profiles.py GENERIC_DIR Z1_DIR
Run on every network overlay too: derived INT4 kernels must remain unchanged.
Numerical equivalence still requires the GPU frame/layer checks.
"""
import sys
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'linux/build'))
from build_network import PROFILES, profile_changes


def check(generic, tuned):
    original = {p.relative_to(generic): p.read_bytes() for p in generic.rglob('*') if p.is_file()}
    candidate = {p.relative_to(tuned): p.read_bytes() for p in tuned.rglob('*') if p.is_file()}
    marker = Path('gpu-profile.txt')
    profile = candidate.pop(marker).decode().strip()
    assert profile in PROFILES and profile != 'generic', 'missing/wrong GPU profile marker'
    assert original.keys() == candidate.keys(), 'profile changed pipeline/marker inventory'
    changed = {str(k) for k in original if original[k] != candidate[k]}
    selected = set(profile_changes('rdna3', profile))
    table = json.loads((ROOT / 'linux/shaders/rdna3/pipelines.json').read_text())
    expected = {f'g_{name}.spv' for name in selected}
    expected.update(f'temporal/{name}.spv' for name, v in table['variants'].items() if v['base'] in selected)
    assert changed == expected, {'unexpected': changed - expected, 'missing': expected - changed}
    print(f'{tuned.name}: {len(changed)} selected shader changes; host constants and derived INT4 kernels identical')


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    check(Path(sys.argv[1]), Path(sys.argv[2]))
