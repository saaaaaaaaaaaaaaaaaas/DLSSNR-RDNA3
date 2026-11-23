#!/usr/bin/env python3
"""Compare Lighting-off Motion outputs against unchanged full NR.

Folders: motion-full, motion-before-full, motion-n{2,3,4}, motion-before-n{2,3,4}.
Luma error uses 16x16 averages to separate lighting drift from individual edges.
These are model-fixture measurements, not independent ground truth or game FPS.
"""
import argparse
import json
from pathlib import Path
import re

import numpy as np


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--root', type=Path, required=True)
    ap.add_argument('--scene', type=Path, required=True)
    args = ap.parse_args()
    scene = json.loads((args.scene / 'scene.json').read_text())
    w, h = scene['extent']
    frames = scene['frames']
    luma = np.array([.2126, .7152, .0722])

    def load(folder, i):
        return np.fromfile(folder / f'out-{i:02}.rgba8', np.uint8).reshape(h, w, 4)

    def low(rgb):
        y = rgb @ luma
        return y[:h//16*16, :w//16*16].reshape(h//16, 16, w//16, 16).mean(axis=(1, 3))

    def rms(values):
        return float(np.sqrt(np.square(values).mean()))

    full = [load(args.root / 'motion-full', i) for i in range(frames)]
    assert all(np.array_equal(load(args.root / 'motion-before-full', i), full[i]) for i in range(frames))
    report = {'scene': scene['scene'], 'extent': [w, h], 'lighting': 'off',
              'full_nr_exact_frames': frames, 'reference': 'Full NR Turbo, N=1', 'cases': {}}
    for version in ('before', 'after'):
        for n in (2, 3, 4):
            folder = args.root / (f'motion-before-n{n}' if version == 'before' else f'motion-n{n}')
            text = (folder / 'run.log').read_text()
            rows = re.findall(r'^\s*(\d+)\s+(yes|no)\s+(yes|no)\s+(\d+)\s+([0-9.]+)$', text, re.M)
            assert len(rows) == frames
            assert all((int(d) > 0) == (int(i) % n == 0) and g == 'no' for i, _, g, d, _ in rows)
            assert f'cadence full {frames//n} reconstructed {frames-frames//n} unexpected 0' in text
            metrics = []
            for i in range(frames):
                out = load(folder, i)
                assert np.array_equal(out[..., 3], full[i][..., 3])
                rgb, ref = out[..., :3].astype(float), full[i][..., :3].astype(float)
                error = rgb - ref
                exposed = np.fromfile(args.scene / f'exposed-{i:02}.u8', np.uint8).reshape(h, w).astype(bool)
                blue = error[..., 2] - .5 * (error[..., 0] + error[..., 1])
                # One code value avoids exaggerated log errors on black blocks.
                gain_error = np.log2((low(rgb) + 1) / (low(ref) + 1))
                metrics.append({'frame': i, 'phase': i % n, 'full': i % n == 0,
                                'rgb_rmse_codes': rms(error), 'low_luma_rmse_stops': rms(gain_error),
                                'low_luma_mean_error_stops': float(gain_error.mean()),
                                'exposed_blue_error_codes': float(np.maximum(blue[exposed], 0).mean()) if exposed.any() else None})
            warm = metrics[4:]
            report['cases'][f'{version}-n{n}'] = {
                'low_luma_rmse_stops': float(np.mean([x['low_luma_rmse_stops'] for x in warm])),
                'phase_mean_error_stops': {str(p): float(np.mean([x['low_luma_mean_error_stops'] for x in warm if x['phase'] == p])) for p in range(n)},
                'motion_blue_error_codes': float(np.mean([x['exposed_blue_error_codes'] for x in metrics[4:13]])),
                'frames': metrics}
    (args.root / 'motion-residual-quality.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: {a: b for a, b in v.items() if a != 'frames'} for k, v in report['cases'].items()}, indent=2))


if __name__ == '__main__':
    main()
