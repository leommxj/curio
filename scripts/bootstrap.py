#!/usr/bin/env python3
"""Fetch the pinned source dependencies, without installing SDKs or touching devices."""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host-only', action='store_true', help='Fetch cJSON and libyaml for desktop C tests only')
    args = parser.parse_args()
    lock = json.loads((ROOT / 'dependencies.json').read_text())
    deps = ROOT / '.deps'
    deps.mkdir(exist_ok=True)
    for source in lock['sources']:
        if args.host_only and source['name'] not in ('cJSON', 'libyaml'):
            continue
        target = deps / source['name']
        marker = target / '.curio-revision'
        if marker.exists() and marker.read_text().strip() == source['commit']:
            print(source['name'] + ': already pinned')
            continue
        if target.exists():
            raise SystemExit(f'{target} exists without matching revision; move it aside before bootstrapping')
        print('Fetching ' + source['name'], flush=True)
        with tempfile.TemporaryDirectory(prefix='curio-dep-') as temp:
            archive = Path(temp) / 'source.tar.gz'
            subprocess.run(['curl', '-fsSL', '--retry', '2', '--connect-timeout', '15', '--max-time', '600',
                            source['url'], '-o', str(archive)], check=True)
            if hashlib.sha256(archive.read_bytes()).hexdigest() != source['sha256']:
                raise SystemExit('Archive checksum mismatch: ' + source['name'])
            extracted = Path(temp) / 'source'
            extracted.mkdir()
            with tarfile.open(archive) as bundle:
                for member in bundle.getmembers():
                    if '/' not in member.name:
                        continue
                    member.name = member.name.split('/', 1)[1]
                    if member.name:
                        bundle.extract(member, extracted, filter='data')
            (extracted / '.curio-revision').write_text(source['commit'] + '\n')
            shutil.move(str(extracted), target)
        print(source['name'] + ': ready')


if __name__ == '__main__':
    main()
