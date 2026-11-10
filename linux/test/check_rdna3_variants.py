#!/usr/bin/env python3
"""Exercise each installed RDNA3 network twice; compare only with an explicitly supplied approved reference.
Without --reference this writes candidates and exits 2: determinism alone is not numerical acceptance.
Needs the same Python dependencies as check_rdna3.py and an idle gfx11 GPU.
"""
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
from pathlib import Path

import check_rdna3 as base

MODES = {
    'small': ('CC_SMALL_FP8', 0, (1, 0, 0, 0, 0)),
    'silver': ('CC_SILVER_AARDWOLD', 0, (0, 0, 0, 0, 0)),
    'experimental': ('CC_SMALL_FP8_EXPERIMENTAL', 0, (1, 1, 0, 0, 0)),
    'quality': ('CC_SILVER_AARDWOLD_OPTIMIZED', 0, (0, 0, 1, 0, 0)),
    'balanced': ('CC_SILVER_AARDWOLD_OPTIMIZED', 1, (0, 0, 1, 1, 1)),
    'performance': ('CC_SILVER_AARDWOLD_OPTIMIZED', 2, (0, 0, 1, 2, 1)),
    'ultra': ('CC_SILVER_AARDWOLD_OPTIMIZED', 3, (0, 0, 1, 3, 1)),
}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--installed', type=Path, required=True, help='game or extracted package root with its own models')
    ap.add_argument('--runner', type=Path, help='already-built run_frame with NR_INT4=1')
    ap.add_argument('--out', type=Path, default=base.BUILD / 'variants')
    ap.add_argument('--reference', type=Path, help='approved gfx11 outputs from an independent verified implementation')
    ap.add_argument('--sizes', nargs='+', choices=list(base.FRAMES), default=list(base.FRAMES))
    ap.add_argument('--modes', nargs='+', choices=list(MODES), default=list(MODES))
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    runner = a.runner or base.BUILD / 'run_frame_variants'
    if not a.runner:
        base.BUILD.mkdir(parents=True, exist_ok=True)
        base.run(['g++', '-std=c++20', '-O1', '-w', '-DNR_INT4=1', '-Itoolchain/Vulkan-Headers/include',
                  '-Ilinux/src/core', '-Ilinux/test'] + base.host_defines() +
                 ['linux/test/run_frame.cpp', 'linux/src/core/nr_runtime.cpp', 'linux/src/core/nr_native_plan.cpp',
                  '-o', str(runner), '-lvulkan', '-lpthread'])
    runner = runner.resolve()
    report = {'source': base.run(['git', 'rev-parse', 'HEAD']).strip(), 'status': 'UNVERIFIED', 'cases': {}}
    report['models'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                        for p in (a.installed / 'dlssnr-amd').glob('CC_*.bin')}
    failed = False
    for mode in a.modes:
        model, level, expected = MODES[mode]
        for size in a.sizes:
            key = f'{mode}-{size}'
            w, h = base.FRAMES[size]
            inp, _ = base.frame_inputs(size)
            env = dict(os.environ, DLSSNR_MODEL=model, DLSSNR_INT4=str(level))
            outputs = []
            for run in range(2):
                output = a.out / f'{key}-{run}.rgba8'
                r = subprocess.run([str(runner), str(a.installed.resolve()), str(inp), str(w), str(h), str(output)],
                                   cwd=base.ROOT, env=env, capture_output=True, text=True)
                (a.out / f'{key}-{run}.log').write_text(r.stdout + r.stderr)
                selected = re.search(r'network_selected (\d+) experimental (\d+) optimized (\d+) layers (\d+) int4 (\d+)', r.stdout)
                if r.returncode or not selected or tuple(map(int, selected.groups())) != expected:
                    print(f'{key}: FAIL (execution or network fallback; see log)')
                    failed = True
                    break
                outputs.append(output.read_bytes())
            if len(outputs) != 2:
                continue
            deterministic = outputs[0] == outputs[1] and len(outputs[0]) == w * h * 4
            ref = a.reference / f'{key}-0.rgba8' if a.reference else None
            exact = bool(ref and ref.is_file() and outputs[0] == ref.read_bytes())
            ok = deterministic and (exact if a.reference else True)
            failed |= not ok
            report['cases'][key] = dict(input_sha256=hashlib.sha256(inp.read_bytes()).hexdigest(),
                sha256=hashlib.sha256(outputs[0]).hexdigest(), deterministic=deterministic, reference_exact=exact)
            print(f'{key}: ' + ('EXACT' if exact and deterministic else 'candidate, no reference' if ok else 'FAIL'))
    report['status'] = 'FAIL' if failed else 'EXACT' if a.reference else 'UNVERIFIED (no approved reference)'
    (a.out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    return 1 if failed else 0 if a.reference else 2


if __name__ == '__main__':
    sys.exit(main())
