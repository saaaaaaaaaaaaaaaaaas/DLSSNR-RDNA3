#!/usr/bin/env python3
"""Build the Linux SPIR-V matching this fork's deliberately unchanged Windows runtime.

Linux's port now uses a separate noise buffer and fitted optimized networks. The
older Windows host still uses noise in weights and legacy INT4 calibration;
substituting the new Linux SPIR-V would bind the wrong data, even with ACO records.
Keep the exact pre-port Linux sources in generated build output for this package.
"""
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = '960038fd1cb892742548f408355e0f3b2fd30d6f'


def main():
    parent = ROOT / 'build/windows'
    parent.mkdir(parents=True, exist_ok=True)
    snapshot = parent / 'linux-aco-source'
    if not (snapshot / 'source-commit').is_file() or (snapshot / 'source-commit').read_text().strip() != SOURCE:
        with tempfile.TemporaryDirectory(dir=parent) as tmp:
            tree = Path(tmp) / 'source'
            tree.mkdir()
            archive = subprocess.Popen(['git', 'archive', SOURCE, '--', 'linux/build/build_network.py',
                'linux/shaders/rdna4', 'linux/shaders/passes'], cwd=ROOT, stdout=subprocess.PIPE)
            try:
                with tarfile.open(fileobj=archive.stdout, mode='r|') as t:
                    t.extractall(tree, filter='data')
            finally:
                archive.stdout.close()
            if archive.wait():
                raise RuntimeError('cannot read pinned Linux ACO sources from Git')
            (tree / 'toolchain').symlink_to(os.path.relpath(ROOT / 'toolchain', snapshot), target_is_directory=True)
            (tree / 'source-commit').write_text(SOURCE + '\n')
            shutil.rmtree(snapshot, ignore_errors=True)
            tree.rename(snapshot)
    return subprocess.call([sys.executable, str(snapshot / 'linux/build/build_network.py'), 'rdna4'] + sys.argv[1:], cwd=ROOT)


if __name__ == '__main__':
    sys.exit(main())
