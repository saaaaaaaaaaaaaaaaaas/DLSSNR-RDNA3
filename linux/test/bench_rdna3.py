#!/usr/bin/env python3
"""Compare RDNA3 scheduling/shader candidates on an identical input.

Build nr_graph and mkplan with check_rdna3.py (or check_rdna3_small.py) first.
Cases are a JSON object: {"baseline": {}, "candidate": {"env":
{"NR_PERSIST_WG_128": "24"}, "spv_dir": "build/candidate/network"}}.
Optional per-case graph/spv_dir paths are relative to the repository root.
Each round reverses case order to reduce clock/temperature ordering bias.
Output equality is checked on RGBA32F bytes, separately from timing. This is
not an independent numerical reference, a game benchmark or a power-mode switch.
--allow-different permits approximate candidates while still rejecting invalid
or nondeterministic output. RGB errors use peak 1; null PSNR means an exact match.
"""
import argparse
import datetime
import hashlib
import json
import math
import os
import re
import statistics
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
EXTENTS = {"720p": (1280, 720), "800p": (1280, 800), "900p": (1600, 900),
           "1080p": (1920, 1080), "1440p": (2560, 1440), "4k": (3840, 2160)}


def positive(value):
    n = int(value)
    if n < 1:
        raise argparse.ArgumentTypeError("must be positive")
    return n


def extent(value):
    if value in EXTENTS:
        return EXTENTS[value]
    m = re.fullmatch(r"([1-9][0-9]*)x([1-9][0-9]*)", value)
    if not m:
        raise argparse.ArgumentTypeError("use 720p/800p/900p/1080p/1440p/4k or WIDTHxHEIGHT")
    return tuple(map(int, m.groups()))


def root_path(value):
    p = Path(value)
    return p if p.is_absolute() else ROOT / p


def shader_digest(directory):
    digest = hashlib.sha256()
    for p in sorted(directory.rglob('*')):
        if p.is_file():
            digest.update(p.relative_to(directory).as_posix().encode() + b'\0')
            digest.update(hashlib.sha256(p.read_bytes()).digest())
    return digest.hexdigest()


def sensors():
    result = {}
    for card in Path('/sys/class/drm').glob('card[0-9]*'):
        for pattern in ('device/gpu_busy_percent', 'device/pp_dpm_sclk',
                        'device/power_dpm_force_performance_level',
                        'device/hwmon/hwmon*/power*_average', 'device/hwmon/hwmon*/temp1_input'):
            for p in card.glob(pattern):
                try:
                    result[str(p)] = p.read_text().strip()
                except OSError:
                    pass
    return result


def parse_timing(output):
    if re.search(r'persist error:|chain error:|output not trustworthy', output):
        raise RuntimeError('GPU dependency wait failed; timing is invalid')
    times = re.findall(r'\d+ dispatches, one submit: ([0-9.]+) ms', output)
    if len(times) != 1 or not math.isfinite(float(times[0])) or float(times[0]) <= 0:
        raise RuntimeError('missing or invalid GPU frame time')
    layers = {}
    for m in re.finditer(r'^(\S+)/(\S+)\s+\d+\s+(\d+)\s+([0-9.]+)\s+([0-9.]+)\s+[0-9.]+%', output, re.M):
        # A kernel can serve several families: keep their total, not the last row.
        row = layers.setdefault(m[2], {'dispatches': 0, 'ms': 0.0})
        row['dispatches'] += int(m[3])
        row['ms'] += float(m[5])
    if not layers:
        raise RuntimeError('empty per-layer timings')
    return float(times[0]), layers


def quality_metrics(data, reference):
    a = np.frombuffer(data, np.float32).reshape(-1, 4)[:, :3]
    b = np.frombuffer(reference, np.float32).reshape(-1, 4)[:, :3]
    delta = a - b
    mse = float(np.mean(np.square(delta, dtype=np.float64)))
    display_a = np.rint(np.clip(a, 0, 1) * 255).astype(np.uint8)
    display_b = np.rint(np.clip(b, 0, 1) * 255).astype(np.uint8)
    display_mse = float(np.mean(np.square(display_a.astype(np.float32) - display_b, dtype=np.float64)))
    return {'rgb_rmse': math.sqrt(mse), 'rgb_max_abs': float(np.max(np.abs(delta))),
            'rgb_psnr_db': None if mse == 0 else -10 * math.log10(mse),
            'display_psnr_db': None if display_mse == 0 else 10 * math.log10(255 ** 2 / display_mse)}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--model', type=Path, required=True)
    ap.add_argument('--model-kind', choices=['silver', 'small'], default='silver')
    ap.add_argument('--resolution', type=extent, default=EXTENTS['1080p'])
    ap.add_argument('--input', type=Path, default=ROOT / 'docs/ngx-verification/310.8.0/single-frame-inputs/1920x1080.png')
    ap.add_argument('--cases', type=Path, required=True, help='JSON object; first case is the equality reference')
    ap.add_argument('--graph', type=Path, help='default: build/test/nr_graph[_small]')
    ap.add_argument('--spv-dir', type=Path, default=ROOT / 'build/linux/rdna3/network')
    ap.add_argument('--rounds', type=positive, default=3)
    ap.add_argument('--warmup', type=positive, default=8)
    ap.add_argument('--repeats', type=positive, default=20)
    ap.add_argument('--timeout', type=positive, default=180, help='per GPU process, seconds')
    ap.add_argument('--allow-different', action='store_true', help='measure image-changing candidates; deterministic output still required')
    ap.add_argument('--min-psnr', type=float, default=0, help='optional minimum RGB PSNR vs the first case (peak 1)')
    ap.add_argument('--out', type=Path, required=True, help='new directory for logs, input, outputs and report.json')
    args = ap.parse_args()
    args.model = args.model.resolve()
    if not args.model.is_file():
        ap.error(f'model missing: {args.model}')
    cases = json.loads(args.cases.read_text())
    if not isinstance(cases, dict) or not cases:
        ap.error('cases must be a nonempty JSON object')
    for name, case in cases.items():
        if not re.fullmatch(r'[A-Za-z0-9_-]+', name) or not isinstance(case, dict):
            ap.error('case names must contain only letters, digits, underscores or hyphens')
        if set(case) - {'env', 'spv_dir', 'graph'} or not isinstance(case.get('env', {}), dict):
            ap.error(f'invalid case: {name}')
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=False)
    graph = args.graph or ROOT / ('build/test/nr_graph_small' if args.model_kind == 'small' else 'build/test/nr_graph')
    planner = ROOT / ('build/test/mkplan_small' if args.model_kind == 'small' else 'build/test/mkplan')
    w, h = args.resolution
    plan = args.out / 'plan.txt'
    plan.write_text(subprocess.run([str(planner), str(w), str(h)] + (['small'] if args.model_kind == 'small' else []),
                                   cwd=ROOT, capture_output=True, text=True, check=True, timeout=10).stdout)
    image = Image.open(args.input).convert('RGBA').resize((w, h), Image.Resampling.LANCZOS)
    input_file = args.out / 'input.f32'
    (np.asarray(image).astype(np.float32) / 255).tofile(input_file)
    report = {'source': subprocess.run(['git', 'rev-parse', 'HEAD'], cwd=ROOT, capture_output=True, text=True, check=True).stdout.strip(),
              'started_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'source_diff_sha256': hashlib.sha256(subprocess.run(['git', 'diff', 'HEAD'], cwd=ROOT, capture_output=True, check=True).stdout).hexdigest(),
              'model': args.model.name, 'model_sha256': hashlib.sha256(args.model.read_bytes()).hexdigest(),
              'model_kind': args.model_kind, 'extent': [w, h], 'input_sha256': hashlib.sha256(input_file.read_bytes()).hexdigest(),
              'warmup': args.warmup, 'repeats': args.repeats, 'rounds': args.rounds, 'cases': cases,
              'allow_different': args.allow_different, 'min_psnr_db': args.min_psnr,
              'environment': {k: v for k, v in os.environ.items() if k.startswith(('NR_', 'RADV_', 'VK_'))}, 'runs': []}
    report['binaries'] = {
        name: {'graph_sha256': hashlib.sha256(root_path(case.get('graph', str(graph))).read_bytes()).hexdigest(),
               'shader_tree_sha256': shader_digest(root_path(case.get('spv_dir', str(args.spv_dir))))}
        for name, case in cases.items()}
    reference = None
    reference_data = None
    case_hashes = {}
    try:
        for round_index in range(args.rounds):
            names = list(cases)
            if round_index % 2:
                names.reverse()
            for name in names:
                case = cases[name]
                prefix = args.out / f'{round_index}-{name}'
                output_file = prefix.with_suffix('.f32')
                cmd = [str(root_path(case.get('graph', str(graph)))), '--plan', str(plan), '--model-pack', str(args.model),
                       '--spv-dir', str(root_path(case.get('spv_dir', str(args.spv_dir)))), '--host-boundary', '--reuse',
                       '--source-width', str(w), '--source-height', str(h), '--in-image', str(input_file),
                       '--warmup', str(args.warmup), '--repeats', str(args.repeats), '--per-layer', '--out-image', str(output_file)]
                env = os.environ.copy()
                for key, value in case.get('env', {}).items():
                    if value is None:
                        env.pop(key, None)
                    else:
                        env[key] = str(value)
                row = {'round': round_index, 'case': name, 'command': cmd, 'sensors_before': sensors()}
                # Stream directly to disk so failed/timed-out runs retain their diagnostics.
                with prefix.with_suffix('.log').open('w') as log:
                    r = subprocess.run(cmd, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=args.timeout)
                row['sensors_after'] = sensors()
                if r.returncode:
                    raise RuntimeError(f'{name}: GPU process exited {r.returncode}; see {prefix}.log')
                output = prefix.with_suffix('.log').read_text()
                row['ms'], row['layers'] = parse_timing(output)
                clock = re.search(r'one submit:.*@ ([0-9.]+) MHz', output)
                if clock and float(clock[1]) > 0:
                    row['gpu_mhz'] = float(clock[1])
                    row['gpu_mcycles'] = row['ms'] * row['gpu_mhz'] / 1000
                telemetry = re.search(r'telemetry:.*gfx ([0-9.]+)\.\.([0-9.]+) MHz, ([0-9.]+) W, ([0-9.]+) C', output)
                if telemetry:
                    row['telemetry'] = dict(zip(['min_mhz', 'max_mhz', 'power_w', 'edge_c'], map(float, telemetry.groups())))
                data = output_file.read_bytes()
                if len(data) != w * h * 16 or not np.isfinite(np.frombuffer(data, np.float32)).all():
                    raise RuntimeError(f'{name}: missing, truncated or non-finite RGBA32F output')
                row['sha256'] = hashlib.sha256(data).hexdigest()
                if reference is None:
                    reference = row['sha256']
                    reference_data = data
                row['exact'] = row['sha256'] == reference
                row['quality'] = quality_metrics(data, reference_data)
                case_hashes.setdefault(name, row['sha256'])
                row['deterministic'] = row['sha256'] == case_hashes[name]
                report['runs'].append(row)
                (args.out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
                score = row['quality']['rgb_psnr_db']
                picture = 'EXACT' if row['exact'] else 'CHANGED (RGB exact)' if score is None else f'CHANGED ({score:.2f} dB)'
                print(f'{round_index + 1}/{args.rounds} {name}: {row["ms"]:.3f} ms  {picture}', flush=True)
        report['summary'] = {}
        for name in cases:
            rows = [r for r in report['runs'] if r['case'] == name]
            times = [r['ms'] for r in rows]
            report['summary'][name] = {'median_ms': statistics.median(times), 'min_ms': min(times),
                                       'max_ms': max(times), 'exact': all(r['exact'] for r in rows),
                                       'deterministic': all(r['deterministic'] for r in rows),
                                       'quality': rows[0]['quality']}
        print(json.dumps(report['summary'], indent=2))
        ok = all(r['deterministic'] and (args.allow_different or r['exact']) and
                 (r['quality']['rgb_psnr_db'] is None or r['quality']['rgb_psnr_db'] >= args.min_psnr)
                 for r in report['runs'])
        return 0 if ok else 1
    except (RuntimeError, subprocess.TimeoutExpired, OSError) as e:
        report['error'] = str(e)
        print(f'ERROR: {e}', file=sys.stderr)
        return 1
    finally:
        (args.out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    sys.exit(main())
