#!/usr/bin/env python3
"""Reject NVIDIA models/DLLs in a distributable tar.gz or zip; calibration tables and source-built forwarders are allowed."""
import hashlib
import json
import re
import sys
import tarfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2] if Path(__file__).parent.name == 'build' and Path(__file__).parent.parent.name == 'linux' else Path.cwd()
PROPRIETARY = {json.loads(p.read_text())['dll_sha256'] for p in (ROOT / 'linux/package/model-tools').glob('descriptor*.json')}
CALIBRATION = re.compile(r'(?:^|/)int4(?:-(?:performance|ultra))?/(?:swin|vit)/[^/]+\.bin$')
MODEL = re.compile(r'(?i)(?:CC_.*|dlssnr(?:-small|-int4|-small-experimental|-optimized)?)\.bin$')


def inspect(name, data):
    leaf = Path(name).name
    if MODEL.fullmatch(leaf) or data[:8] in (b'NRMODEL1', b'NRI4WGT1'):
        raise ValueError(f'model found: {name}')
    if leaf.lower().endswith('.bin') and not CALIBRATION.search(name.replace('\\', '/')):
        raise ValueError(f'unexpected binary tensor file: {name}')
    if leaf.lower().endswith('.dll') and hashlib.sha256(data).hexdigest() in PROPRIETARY:
        raise ValueError(f'proprietary NVIDIA DLL found: {name}')


def audit(path):
    count = 0
    if zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as z:
            for f in z.infolist():
                if not f.is_dir(): inspect(f.filename, z.read(f)); count += 1
    else:
        with tarfile.open(path, 'r:*') as t:
            for f in t:
                if f.isfile(): inspect(f.name, t.extractfile(f).read()); count += 1
    print(f'{path}: PASS ({count} files; no model containers, unexpected tensor files or known NVIDIA DLLs)')


if __name__ == '__main__':
    for path in map(Path, sys.argv[1:]): audit(path)
