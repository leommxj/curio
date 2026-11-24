#!/usr/bin/env python3
import argparse
import gzip
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('source', type=Path)
parser.add_argument('output', type=Path)
parser.add_argument('--modules', type=Path)
args = parser.parse_args()
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from modules import generate
bundle = generate(args.modules)['output']
assets = [('wifi.js', 'application/javascript; charset=utf-8'), ('vendor/js-yaml.min.js', 'application/javascript; charset=utf-8'), ('index.html', 'text/html; charset=utf-8'), ('style.css', 'text/css; charset=utf-8'), ('app.js', 'application/javascript; charset=utf-8'), ('setup.html', 'text/html; charset=utf-8'), ('setup.js', 'application/javascript; charset=utf-8')]
lines = ['#pragma once', '#include <stdint.h>', '#include <stddef.h>',
         'typedef struct { const char *path, *type; const uint8_t *data; size_t size; } curio_web_asset_t;']
for index, (name, _) in enumerate(assets):
    data = gzip.compress(((bundle if name in ('app.js', 'style.css') else args.source) / name).read_bytes(), compresslevel=9, mtime=0)
    lines.append(f'static const uint8_t curio_web_{index}[] = {{')
    lines.extend(','.join(str(byte) for byte in data[start:start + 24]) + ',' for start in range(0, len(data), 24))
    lines.append('};')
lines.append('static const curio_web_asset_t curio_web_assets[] = {')
for index, (name, mime) in enumerate(assets):
    lines.append(f'{{"/{name}", "{mime}", curio_web_{index}, sizeof(curio_web_{index})}},')
lines.append('};')
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text('\n'.join(lines) + '\n')
