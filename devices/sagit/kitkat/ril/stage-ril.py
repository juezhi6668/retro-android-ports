#!/usr/bin/env python3
import argparse
import hashlib
from pathlib import Path
import shutil

parser = argparse.ArgumentParser(description='Stage the verified sagit/chiron KitKat RIL outside the device tree')
parser.add_argument('source', type=Path, help='flat directory containing the tested rild and libraries')
parser.add_argument('aosp', type=Path, help='KitKat source checkout')
args = parser.parse_args()
manifest = Path(__file__).with_name('blobs.sha256')
entries = [line.split() for line in manifest.read_text().splitlines() if line.strip()]
if not (args.aosp / 'build/core/main.mk').is_file():
    parser.error('not an AOSP checkout')
for expected, name in entries:
    source = args.source / Path(name).name
    if not source.is_file() or hashlib.sha256(source.read_bytes()).hexdigest() != expected:
        parser.error('missing or mismatched blob: ' + name)
dest = args.aosp / 'vendor/xiaomi/sagit-ril'
if dest.exists():
    for expected, name in entries:
        target = dest / name
        if target.exists() and hashlib.sha256(target.read_bytes()).hexdigest() != expected:
            parser.error('refusing to overwrite different staged blob: ' + str(target))
for expected, name in entries:
    target = dest / name
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(str(args.source / target.name), str(target))
    target.chmod(0o755 if target.name == 'rild' else 0o644)
print('Staged {} verified files in {}'.format(len(entries), dest))
