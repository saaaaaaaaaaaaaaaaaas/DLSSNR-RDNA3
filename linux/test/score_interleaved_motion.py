#!/usr/bin/env python3
"""Compare paired actual-model Interleaved Motion runs, with Lighting off.

The root contains final-3-full-p{1,2}-n1, final-3-before-p{1,2}-n2 and
latest-3-after-p{1,2}-n2, plus the input scene and exposed-pixel masks.
"""
import argparse
import json
import re
from pathlib import Path

import numpy as np


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--root', type=Path, required=True)
    ap.add_argument('--scene', type=Path, required=True)
    args = ap.parse_args()
    w, h = json.loads((args.scene/'scene.json').read_text())['extent']
    result = {'lighting': 'off', 'reference': 'Full NR, Interleaved 3 passes, same preset', 'presets': {}}
    for preset in (1, 2):
        ref = args.root/f'final-3-full-p{preset}-n1'
        cases = {}
        for name, prefix in [('before','final-3-before'), ('after','latest-3-after')]:
            folder = args.root/f'{prefix}-p{preset}-n2'
            text = (folder/'run.log').read_text()
            assert 'Validation Error' not in text and 'SYNC-HAZARD' not in text
            rows = re.findall(r'^\s*(\d+)\s+(yes|no)\s+(yes|no)\s+(\d+)\s+([0-9.]+)$', text, re.M)
            assert len(rows) == 24
            if name == 'after':
                # No motion-triggered full passes; alternate until the cut at frame 13.
                assert all((int(rows[i][3]) > 0) == (i % 2 == 0) for i in range(1, 13))
                assert all(int(row[3]) <= 71 for row in rows[1:])
            rgb, blue = [], []
            for i in range(4,24):
                def load(d):
                    return np.fromfile(d/f'out-{i:02}.rgba8',np.uint8).reshape(h,w,4)
                a,b = load(folder),load(ref)
                assert np.array_equal(a[...,3],b[...,3])
                error = a[...,:3].astype(float)-b[...,:3].astype(float)
                rgb.append(float(np.sqrt(np.square(error).mean())))
                mask = np.fromfile(args.scene/f'exposed-{i:02}.u8',np.uint8).reshape(h,w).astype(bool)
                if mask.any():
                    chroma = error[...,2]-.5*(error[...,0]+error[...,1])
                    blue.append(float(np.maximum(chroma[mask],0).mean()))
            cases[name] = {'rgb_rmse_codes': float(np.mean(rgb)), 'exposed_positive_blue_codes': float(np.mean(blue))}
        assert cases['after']['rgb_rmse_codes'] < cases['before']['rgb_rmse_codes']
        result['presets'][str(preset)] = cases
    print(json.dumps(result,indent=2))


if __name__ == '__main__':
    main()
