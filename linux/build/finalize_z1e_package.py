#!/usr/bin/env python3
"""Finish an assembled Turbo package without rebuilding its runtime consumers."""
import argparse
from pathlib import Path
import shutil

from audit_z1e_release import NETWORKS, ROOT, check_residual_shader


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('package', type=Path)
    args = ap.parse_args()
    package = args.package.resolve()
    if not package.is_relative_to(ROOT):
        ap.error('package must be inside this worktree')
    for directory in NETWORKS:
        if (package / directory / 'gpu-profile.txt').read_text().strip() != 'z1-extreme-turbo':
            ap.error('all six networks must already use the Turbo profile')
    source = package / NETWORKS[0] / 'runtime'
    check_residual_shader((source / 'runtime_seamless.spv').read_bytes(), str(source))
    # The runtime normally resolves adapters from the base network. Also ship
    # identical adapters with every variant, so partial shader updates cannot
    # leave a stale residual ABI in a variant folder.
    for directory in NETWORKS[1:]:
        shutil.copytree(source, package / directory / 'runtime', dirs_exist_ok=True)
    for name in ('RESIDUAL-STABILITY.md', 'RESIDUAL-STABILITY-VALIDATION.json'):
        shutil.copy2(ROOT / 'docs' / name, package / name)
    print('Finalized Turbo package: six matching adapter folders and stability documentation')


if __name__ == '__main__':
    main()
