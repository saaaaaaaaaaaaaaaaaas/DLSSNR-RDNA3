#!/usr/bin/env python3
"""Score benchmark outputs outside the timed runs and export a visual comparison.

NVIDIA comparisons use the supplied original-model single-frame outputs. They
measure approximation error, not independent acceptance of a new network.
"""
import argparse
import json
import math
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

import check_rdna3 as base


def display_rgb(path, width, height):
    rgba = np.fromfile(path, np.float32).reshape(height, width, 4)
    if not np.isfinite(rgba).all():
        raise ValueError(f'non-finite output: {path}')
    return np.rint(np.clip(rgba[..., :3], 0, 1) * 255).astype(np.uint8)


def finite(value):
    return value if math.isfinite(value) else None


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('directory', type=Path)
    ap.add_argument('--reference', type=Path, help='matching NVIDIA RGB PNG; default selected by model-kind and extent')
    args = ap.parse_args()
    report = json.loads((args.directory / 'report.json').read_text())
    width, height = report['extent']
    selected = {}
    for row in report['runs']:
        selected.setdefault(row['case'], row)
    reference = args.reference or base.ROOT / 'docs/ngx-verification' / ('310.9.16' if report['model_kind'] == 'small' else '310.8.0') / 'single-frame-outputs' / f'{width}x{height}_nvidia.png'
    nvidia = np.array(Image.open(reference).convert('RGB')) if reference.is_file() else None
    if nvidia is not None and nvidia.shape != (height, width, 3):
        raise ValueError('reference extent differs from the benchmark')
    first = next(iter(selected.values()))
    exact = display_rgb(args.directory / f'{first["round"]}-{first["case"]}.f32', width, height)
    scores = {'reference': str(reference) if nvidia is not None else None, 'cases': {}}
    panels = []
    for name, row in selected.items():
        rgb = display_rgb(args.directory / f'{row["round"]}-{name}.f32', width, height)
        same = np.array_equal(rgb, exact)
        result = {'psnr_baseline_db': finite(base.psnr(rgb, exact)), 'ssim_baseline': 1.0 if same else base.ssim(rgb, exact)}
        if nvidia is not None:
            result.update(psnr_nvidia_db=finite(base.psnr(rgb, nvidia)), ssim_nvidia=base.ssim(rgb, nvidia))
        scores['cases'][name] = result
        difference = np.minimum(np.abs(rgb.astype(np.int16) - exact) * 8, 255).astype(np.uint8)
        panels.append((name, rgb, difference))
        print(name, json.dumps(result), flush=True)
    thumb_w = min(width, 960)
    thumb_h = round(height * thumb_w / width)
    sheet = Image.new('RGB', (thumb_w * 2, (thumb_h + 24) * len(panels)), '#151515')
    draw = ImageDraw.Draw(sheet)
    for i, (name, rgb, difference) in enumerate(panels):
        y = i * (thumb_h + 24)
        draw.text((8, y + 5), name, fill='white')
        draw.text((thumb_w + 8, y + 5), 'absolute difference vs baseline x8', fill='white')
        sheet.paste(Image.fromarray(rgb).resize((thumb_w, thumb_h), Image.Resampling.LANCZOS), (0, y + 24))
        sheet.paste(Image.fromarray(difference).resize((thumb_w, thumb_h), Image.Resampling.LANCZOS), (thumb_w, y + 24))
    sheet.save(args.directory / 'comparison.png')
    (args.directory / 'quality.json').write_text(json.dumps(scores, indent=2) + '\n')


if __name__ == '__main__':
    main()
