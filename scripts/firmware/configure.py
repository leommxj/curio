#!/usr/bin/env python3
"""Generate a private Hub configuration and Collector credentials."""

import argparse
import getpass
import ipaddress
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]


def private_write(path, value):
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, 'w', encoding='utf-8') as stream:
        stream.write(value)


def scalar(value):
    return json.dumps(value, ensure_ascii=False)


def pem_field(name, value):
    return f'  {name}: |\n' + ''.join(f'    {line}\n' for line in value.splitlines())


def config_yaml(ssid, password, hostname, certificate, private_key, admin_token, collector_token):
    return (
        'version: 1\n'
        'network:\n'
        f'  ssid: {scalar(ssid)}\n'
        f'  password: {scalar(password)}\n'
        f'  hostname: {scalar(hostname)}\n'
        'tls:\n'
        + pem_field('certificate', certificate)
        + pem_field('private_key', private_key)
        + 'access:\n'
        f'  admin_token: {scalar(admin_token)}\n'
        f'  collector_token: {scalar(collector_token)}\n'
        'screen:\n'
        '  pages:\n'
        '    - id: main\n'
        '      title: Agents\n'
        '      widgets:\n'
        '        - id: agents\n'
        '          type: agents\n'
        '          at: [0, 0]\n'
        '          size: [4, 6]\n'
    )


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ssid', required=True)
    parser.add_argument('--hostname', default='curio')
    parser.add_argument('--ip', action='append', default=[], help='Additional device IP SAN; repeatable')
    parser.add_argument('--password-env', help='Read Wi-Fi password from this environment variable')
    parser.add_argument('--open-wifi', action='store_true')
    parser.add_argument('--output', type=Path, default=ROOT / 'firmware/local')
    args = parser.parse_args(argv)
    if not re.fullmatch(r'[a-zA-Z0-9](?:[a-zA-Z0-9-]{0,30}[a-zA-Z0-9])?', args.hostname):
        parser.error('hostname must be one DNS label, at most 32 ASCII characters')
    if not 1 <= len(args.ssid.encode('utf-8')) <= 32 or '\0' in args.ssid:
        parser.error('SSID must be 1–32 UTF-8 bytes without NUL')
    if args.open_wifi and args.password_env:
        parser.error('--open-wifi and --password-env are mutually exclusive')
    if args.open_wifi:
        password = ''
    elif args.password_env:
        if args.password_env not in os.environ:
            parser.error('password environment variable is not set')
        password = os.environ[args.password_env]
    else:
        password = getpass.getpass('Wi-Fi password: ')
    if not args.open_wifi and not (8 <= len(password.encode('utf-8')) <= 63 and '\0' not in password):
        parser.error('WPA password must be 8–63 UTF-8 bytes; use --open-wifi for an open network')
    try:
        addresses = [str(ipaddress.ip_address(value)) for value in args.ip]
    except ValueError as exc:
        parser.error(str(exc))
    if not shutil.which('openssl'):
        parser.error('OpenSSL is required')
    target = args.output.resolve()
    if target.exists():
        parser.error(f'{target} already exists; move it aside to explicitly replace credentials')
    target.parent.mkdir(parents=True, exist_ok=True)
    os.umask(0o077)
    with tempfile.TemporaryDirectory(prefix='.curio-config-', dir=target.parent) as temp:
        work = Path(temp)
        dns = args.hostname + '.local'
        sans = ['DNS:' + dns, 'DNS:' + args.hostname] + ['IP:' + address for address in addresses]
        subprocess.run([
            'openssl', 'req', '-x509', '-newkey', 'ec', '-pkeyopt', 'ec_paramgen_curve:P-256',
            '-nodes', '-sha256', '-days', '3650', '-subj', '/CN=' + dns,
            '-addext', 'subjectAltName=' + ','.join(sans),
            '-addext', 'basicConstraints=critical,CA:TRUE',
            '-addext', 'keyUsage=critical,digitalSignature,keyCertSign',
            '-addext', 'extendedKeyUsage=serverAuth',
            '-keyout', str(work / 'server-key.pem'), '-out', str(work / 'server-cert.pem'),
        ], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        certificate = (work / 'server-cert.pem').read_text(encoding='utf-8')
        private_key = (work / 'server-key.pem').read_text(encoding='utf-8')
        collector_token = secrets.token_hex(32)
        admin_token = secrets.token_hex(32)
        config = config_yaml(args.ssid, password, args.hostname, certificate, private_key,
                             admin_token, collector_token)
        if len(config.encode('utf-8')) > 16384:
            parser.error('generated configuration exceeds the 16384-byte device limit')
        private_write(work / 'curio.yaml', config)
        private_write(work / 'token.txt', collector_token + '\n')
        private_write(work / 'admin-token.txt', admin_token + '\n')
        os.rename(work, target)
    print(f'Hub configuration written to {target / "curio.yaml"}')
    print(f'Collector endpoint: https://{dns}')


if __name__ == '__main__':
    main()
