#!/usr/bin/env python3
"""Audit a model-free RDNA3 Z1 Turbo release and emit its checksum/manifest."""
import argparse
import configparser
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tarfile

import check_package

ROOT = Path(__file__).resolve().parents[2]
NETWORKS = (
    'dlssnr-amd/shaders', 'dlssnr-amd/experimental/shaders',
    'dlssnr-amd/optimized/shaders', 'int4/dlssnr-amd/int4/shaders',
    'int4/dlssnr-amd/int4-performance/shaders', 'int4/dlssnr-amd/int4-ultra/shaders',
)
CONSUMERS = ('reshade/dlssnr_amd.addon64', 'int4/reshade/dlssnr_amd.addon64',
             'optiscaler/asi/dlssnr-amd.asi')


def check_residual_shader(data, name):
    words = struct.unpack('<' + 'I' * (len(data) // 4), data)
    bindings, samples, i = set(), False, 5
    while i < len(words):
        n, op = words[i] >> 16, words[i] & 0xffff
        if not n or i + n > len(words):
            raise ValueError('malformed residual shader: ' + name)
        if op == 71 and n == 4 and words[i + 2] == 33:
            bindings.add(words[i + 3])
        if op == 72 and n == 5 and words[i + 3] == 35 and words[i + 4] == 56:
            samples = True
        i += n
    if bindings != set(range(19)) or not samples:
        raise ValueError('residual lighting ABI mismatch: ' + name)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--archive', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    hashes, sizes, markers, ini, binaries = {}, {}, {}, {}, {}
    with tarfile.open(args.archive) as archive:
        for entry in archive:
            if not entry.isfile():
                continue
            name = entry.name.removeprefix('./')
            if name in hashes:
                raise ValueError('duplicate archive member: ' + name)
            data = archive.extractfile(entry).read()
            check_package.inspect(name, data)
            hashes[name] = hashlib.sha256(data).hexdigest()
            sizes[name] = len(data)
            if name.endswith('/gpu-profile.txt'):
                markers[name] = data.decode().strip()
            if name.startswith('ini/') and name.endswith('.ini'):
                ini[name] = data.decode()
            if name in CONSUMERS:
                offset = struct.unpack_from('<I', data, 0x3c)[0]
                if data[offset:offset + 4] != b'PE\0\0' or struct.unpack_from('<H', data, offset + 4)[0] != 0x8664:
                    raise ValueError('release consumer is not x86_64 PE: ' + name)
                binaries[name] = hashes[name]
                if b'Residual shader ABI mismatch' not in data:
                    raise ValueError('runtime lacks the residual ABI guard: ' + name)
            if name.endswith('/runtime/runtime_seamless.spv'):
                check_residual_shader(data, name)
    expected = {directory + '/gpu-profile.txt' for directory in NETWORKS}
    if set(markers) != expected or set(markers.values()) != {'z1-extreme-turbo'}:
        raise ValueError('all six networks must select the Turbo profile')
    if any('/runtime/runtime_reuse_' in name and name.endswith('.spv') for name in hashes):
        raise ValueError('legacy reuse shaders in RDNA3 release')
    residual_names = {directory + '/runtime/runtime_seamless.spv' for directory in NETWORKS}
    if not residual_names.issubset(hashes) or len({hashes[name] for name in residual_names}) != 1:
        raise ValueError('all six networks require the same residual lighting shader')
    if len(binaries) != 3 or len(ini) != 3:
        raise ValueError('missing runtime consumers or installer INIs')
    for name, text in ini.items():
        config = configparser.ConfigParser(interpolation=None)
        config.read_string(text)
        if config.has_section('Reuse') or not config.getboolean('Temporal', 'Enabled') or config.getint('Temporal', 'Interval') != 2:
            raise ValueError('incorrect temporal defaults: ' + name)
    license_name = 'dlssnr-amd/shaders/runtime/Seamless-MIT-LICENSE.txt'
    license_hash = hashlib.sha256((ROOT / 'linux/shaders/passes/include/seamless/LICENSE.txt').read_bytes()).hexdigest()
    if hashes.get(license_name) != license_hash or 'Turbo-Temporal.txt' not in hashes:
        raise ValueError('missing reference license or profile notice')
    shader_names = []
    for directory in NETWORKS:
        for name in hashes:
            if name.startswith(directory + '/') and name.endswith('.spv'):
                relative = Path(name).relative_to(directory)
                if len(relative.parts) == 1 or relative.parts[0] in ('temporal', 'runtime'):
                    shader_names.append(name)
    # Only private audit files are extracted, never anything into a game folder.
    scratch = args.out / '.audit'
    scratch.mkdir(exist_ok=False)
    try:
        with tarfile.open(args.archive) as archive:
            wanted = set(shader_names) | set(CONSUMERS)
            for entry in archive:
                name = entry.name.removeprefix('./')
                if name in wanted:
                    target = scratch / name
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(archive.extractfile(entry).read())
        for name in shader_names:
            subprocess.run(['spirv-val', '--target-env', 'vulkan1.3', str(scratch / name)], check=True)
        for name in CONSUMERS:
            info = subprocess.check_output(['x86_64-w64-mingw32-objdump', '-p', str(scratch / name)]).decode()
            if any('vulkan-1.dll' in line.lower() for line in info.splitlines() if 'DLL Name:' in line):
                raise ValueError('static Vulkan import: ' + name)
    finally:
        shutil.rmtree(scratch)
    target = args.out / args.archive.name
    if target.resolve() != args.archive.resolve():
        shutil.copy2(args.archive, target)
    digest = hashlib.sha256(target.read_bytes()).hexdigest()
    target.with_suffix('.gz.sha256').write_text(f'{digest}  {target.name}\n')
    record = {
        'archive': target.name, 'sha256': digest, 'bytes': target.stat().st_size,
        'source': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT).decode().strip(),
        'version': subprocess.check_output(['bash', 'linux/build/version.sh'], cwd=ROOT).decode().strip(),
        'target': 'RDNA3 / ROG Ally Z1 Extreme / Linux-Proton x86_64',
        'profile': 'z1-extreme-turbo', 'model_free': True, 'files': len(hashes),
        'spirv_val_passed': len(shader_names), 'runtime_consumer_sha256': binaries,
        'residual_lighting_abi': {'descriptors': 19, 'push_bytes': 60,
                                  'shader_sha256': hashes[next(iter(residual_names))],
                                  'matching_network_folders': len(residual_names)},
        'file_sha256': hashes, 'file_bytes': sizes,
        'qualification': 'Existing Motion and full-NR validation; real game/HDR/FG x4 presentation unverified',
    }
    (args.out / 'release-manifest.json').write_text(json.dumps(record, indent=2) + '\n')
    print(f'PASS: {len(hashes)} files, {len(shader_names)} validated shaders, model-free Turbo x86_64')
    print(f'{digest}  {target.name}')


if __name__ == '__main__':
    main()
