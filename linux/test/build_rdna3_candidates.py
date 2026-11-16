#!/usr/bin/env python3
"""Compile benchmark-only RDNA3 candidates over a known shader tree.

Recipes: {"candidate": {"pipeline": ["NAME=value", ...]}}. Temporal variants
remain the baseline's; use build_network.py profiles for an installed package.
Each candidate gets its own directory and any failed compile is recorded.
"""
import argparse
import json
import re
import shutil
import subprocess
from pathlib import Path

from bench_rdna3 import ROOT


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--recipes', type=Path, required=True)
    ap.add_argument('--base', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    recipes = json.loads(args.recipes.read_text())
    table = json.loads((ROOT / 'linux/shaders/rdna3/pipelines.json').read_text())['pipelines']
    profile = json.loads((ROOT / 'linux/shaders/rdna3/profiles/z1-extreme.json').read_text())['change']
    supported = '\n'.join(p.read_text() for p in (ROOT / 'linux/shaders/rdna3').rglob('*') if p.suffix in ('.comp', '.glsl'))
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=False)
    cases = {'baseline': {'spv_dir': str(args.base.resolve())}}
    results = {}
    for name, changes in recipes.items():
        if not re.fullmatch('[A-Za-z0-9_-]+', name) or name == 'baseline':
            raise ValueError(f'invalid candidate name: {name}')
        target = args.out / name
        shutil.copytree(args.base, target)
        result = {'changes': changes}
        results[name] = result
        try:
            for kernel, extra in changes.items():
                for define in extra:
                    if not re.search(r'\b' + re.escape(define.split('=')[0]) + r'\b', supported):
                        raise ValueError('unknown shader define: ' + define)
                entry = table[kernel]
                defines = entry['defines']
                for additions in [profile.get(kernel, []), extra]:
                    keys = {d.split('=')[0] for d in additions}
                    defines = [d for d in defines if d.split('=')[0] not in keys] + additions
                cmd = [str(ROOT / 'toolchain/glslang/bin/glslang'), '-V', '--target-env', 'vulkan1.3',
                       '-I' + str(ROOT / 'linux/shaders/rdna3/include')]
                cmd += ['-D' + d for d in defines] + [str(ROOT / 'linux/shaders/rdna3' / entry['source']), '-o', str(target / f'g_{kernel}.spv')]
                r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
                if r.returncode:
                    raise RuntimeError(r.stdout + r.stderr)
                r = subprocess.run(['spirv-val', '--target-env', 'vulkan1.3', str(target / f'g_{kernel}.spv')],
                                   capture_output=True, text=True)
                if r.returncode:
                    raise RuntimeError(r.stdout + r.stderr)
            cases[name] = {'spv_dir': str(target)}
            result['status'] = 'compiled'
            print(name + ': compiled', flush=True)
        except (RuntimeError, KeyError, ValueError) as e:
            result.update(status='rejected', error=str(e))
            shutil.rmtree(target)
            print(name + ': rejected; see compile.json', flush=True)
    (args.out / 'cases.json').write_text(json.dumps(cases, indent=2) + '\n')
    (args.out / 'compile.json').write_text(json.dumps(results, indent=2) + '\n')
    return 0 if len(cases) > 1 else 1


if __name__ == '__main__':
    raise SystemExit(main())
