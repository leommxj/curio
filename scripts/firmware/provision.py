#!/usr/bin/env python3
"""Write a Hub configuration over the pinned Iris Gateway and restart the device."""

import argparse
import os
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[2]
UTILS_ROOT = Path(os.environ.get('MOSAICO_UTILS_ROOT', ROOT / '.deps/esp-mosaico-utils')).expanduser().resolve()
TOOL_ROOT = UTILS_ROOT / 'mosaico-tools'
SERVICE_ID = '0x4355'


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, default=ROOT / 'firmware/local/curio.yaml')
    parser.add_argument('--device-id')
    parser.add_argument('--gateway-profile')
    args = parser.parse_args(argv)
    try:
        payload = args.config.read_bytes()
    except OSError as exc:
        parser.error(f'cannot read configuration: {exc}')
    if not payload or len(payload) > 16384:
        parser.error('initial configuration must be 1–16384 bytes')
    try:
        payload.decode('utf-8')
    except UnicodeDecodeError:
        parser.error('configuration must be UTF-8 YAML')
    if b'\0' in payload:
        parser.error('configuration cannot contain NUL bytes')
    if not (TOOL_ROOT / 'tools/mosaico_cli/cli.py').is_file():
        parser.error('pinned Mosaico tools are missing; run make deps first')

    sys.path.insert(0, str(TOOL_ROOT / 'tools'))
    from mosaico_cli.cli import main as mosaico_main

    selection = []
    if args.device_id:
        selection += ['--device-id', args.device_id]
    if args.gateway_profile:
        selection += ['--gateway-profile', args.gateway_profile]
    for offset in range(0, len(payload), 1020):
        chunk = offset.to_bytes(4, 'little') + payload[offset:offset + 1020]
        result = mosaico_main(['iris', 'rpc', SERVICE_ID, '1', '--payload-hex', chunk.hex(),
                               '--deadline-ms', '10000', *selection], tool_root=TOOL_ROOT)
        if result:
            return result
    result = mosaico_main(['iris', 'rpc', SERVICE_ID, '2', '--deadline-ms', '10000', *selection],
                          tool_root=TOOL_ROOT)
    if result:
        return result
    return mosaico_main(['iris', 'rpc', SERVICE_ID, '3', '--deadline-ms', '3000', *selection],
                        tool_root=TOOL_ROOT)


if __name__ == '__main__':
    raise SystemExit(main())
