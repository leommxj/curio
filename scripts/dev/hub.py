#!/usr/bin/env python3
"""Loopback web/API harness using the firmware's C state and layout core."""
import argparse
import hashlib
import ctypes
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
import re
from pathlib import Path
import secrets
import subprocess
import tempfile
import threading
import time
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from modules import generate

ROOT = Path(__file__).resolve().parents[2]
CORE = ROOT / 'firmware/components/curio_core'
LAYOUT = ROOT / 'firmware/components/curio_layout'
ALERTS = ROOT / 'firmware/components/curio_alerts'
CONFIG = ROOT / 'firmware/components/curio_config'
YAML = ROOT / '.deps/libyaml'
WEB = ROOT / 'firmware/main/web'


def build_library():
    destination = ROOT / 'build/libcurio_dev.so'
    modules = generate()
    sources = [*modules['portable'], ROOT / 'scripts/dev/bridge.c', CORE / 'curio_core.c', LAYOUT / 'curio_layout.c', ALERTS / 'curio_alerts.c', CONFIG / 'curio_config.c', *sorted((YAML / 'src').glob('*.c')), ROOT / '.deps/cJSON/cJSON.c']
    includes = [ROOT / 'firmware/modules/include', CORE / 'include', LAYOUT / 'include', ALERTS / 'include', CONFIG / 'include', YAML / 'include', YAML / 'src', ROOT / '.deps/cJSON']
    inputs = sources + [header for directory in includes for header in directory.glob('*.h')]
    if not destination.exists() or any(p.stat().st_mtime > destination.stat().st_mtime for p in inputs):
        destination.parent.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='curio-dev-', dir=destination.parent) as directory:
            library = Path(directory) / destination.name
            subprocess.run(['cc', '-std=c11', '-O2', '-fPIC', '-shared', '-Wall', '-Wextra', '-Werror', '-Wno-unused-value',
                            '-D_POSIX_C_SOURCE=200809L', '-DYAML_VERSION_STRING="0.2.5"', '-DYAML_VERSION_MAJOR=0', '-DYAML_VERSION_MINOR=2', '-DYAML_VERSION_PATCH=5',
                            *['-I' + str(p) for p in includes], *map(str, sources), '-lm', '-o', str(library)], check=True)
            library.replace(destination)
    return destination


class Core:
    def __init__(self):
        self.lib = ctypes.CDLL(str(build_library()))
        self.lib.curio_dev_create.restype = ctypes.c_void_p
        self.lib.curio_dev_destroy.argtypes = [ctypes.c_void_p]
        self.lib.curio_core_apply_json.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t, ctypes.c_uint64]
        self.lib.curio_core_apply_json.restype = ctypes.c_int
        self.lib.curio_core_free_json.argtypes = [ctypes.c_void_p]
        for name in ('curio_core_state_v2_json', 'curio_core_catalog_json'):
            function = getattr(self.lib, name)
            function.argtypes = [ctypes.c_void_p, ctypes.c_uint64]
            function.restype = ctypes.c_void_p
        for name in ('curio_dev_config_yaml', 'curio_dev_config_json'):
            function = getattr(self.lib, name)
            function.argtypes = [ctypes.c_char_p, ctypes.c_size_t]
            function.restype = ctypes.c_void_p
        self.lib.curio_dev_config_token.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_char_p, ctypes.c_char_p]
        self.lib.curio_dev_config_token.restype = ctypes.c_void_p
        self.lib.curio_dev_config_error.restype = ctypes.c_char_p
        self.handle = self.lib.curio_dev_create()
        if not self.handle:
            raise MemoryError('Could not create C core')
        self.lock = threading.Lock()

    def apply(self, body):
        with self.lock:
            return self.lib.curio_core_apply_json(self.handle, body, len(body), int(time.monotonic() * 1000))

    def string(self, address):
        if not address:
            raise ValueError('invalid configuration or out of memory')
        try:
            return ctypes.string_at(address)
        finally:
            self.lib.curio_core_free_json(address)

    def state(self, kind='state'):
        name = 'curio_core_catalog_json' if kind == 'catalog' else 'curio_core_state_v2_json'
        with self.lock:
            return self.string(getattr(self.lib, name)(self.handle, int(time.monotonic() * 1000)))

    def config_yaml(self, body):
        address = self.lib.curio_dev_config_yaml(body, len(body))
        if not address:
            raise ValueError(self.lib.curio_dev_config_error().decode() or 'out of memory')
        return self.string(address)

    def config_json(self, body):
        address = self.lib.curio_dev_config_json(body, len(body))
        if not address:
            raise ValueError(self.lib.curio_dev_config_error().decode() or 'out of memory')
        return json.loads(self.string(address))

    def config_token(self, body, role, token):
        return self.string(self.lib.curio_dev_config_token(body, len(body), role.encode(), token.encode()))

    def close(self):
        with self.lock:
            if self.handle:
                self.lib.curio_dev_destroy(self.handle)
                self.handle = None


def valid_token(value):
    return isinstance(value, str) and 16 <= len(value) <= 128 and all(33 <= ord(c) <= 126 for c in value)


def config_etag(body):
    return '"' + hashlib.sha256(body).hexdigest() + '"'


def write_config(path, body):
    with os.fdopen(os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600), 'wb') as stream:
        os.fchmod(stream.fileno(), 0o600)
        stream.write(body)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=7340)
    parser.add_argument('--token-file', type=Path, required=True)
    parser.add_argument('--admin-token-file', type=Path)
    parser.add_argument('--config-dir', type=Path, default=ROOT / 'build/dev-state')
    args = parser.parse_args()
    core = Core()
    args.config_dir.mkdir(parents=True, exist_ok=True)
    config_path = args.config_dir / 'curio.yaml'
    if config_path.exists():
        config_yaml = core.config_yaml(config_path.read_bytes())
    else:
        collector = args.token_file.read_text().strip()
        admin = args.admin_token_file.read_text().strip() if args.admin_token_file else secrets.token_hex(32)
        if not valid_token(collector) or not valid_token(admin):
            parser.error('tokens must be 16–128 non-whitespace ASCII characters')
        initial = ('access:\n  admin_token: ' + json.dumps(admin) + '\n  collector_token: ' + json.dumps(collector) +
                   '\nscreen:\n  pages:\n    - id: main\n      title: Agents\n      widgets:\n        - id: agents\n          type: agents\n          at: [0, 0]\n          size: [4, 6]\n')
        config_yaml = core.config_yaml(initial.encode())
        write_config(config_path, config_yaml)
    config = core.config_json(config_yaml)
    config_lock = threading.RLock()

    def save_config(body):
        nonlocal config_yaml, config
        canonical = core.config_yaml(body)
        projected = core.config_json(canonical)
        temporary = config_path.with_suffix('.tmp')
        write_config(temporary, canonical)
        temporary.replace(config_path)
        config_yaml, config = canonical, projected

    class Handler(BaseHTTPRequestHandler):
        protocol_version = 'HTTP/1.1'

        def log_message(self, *_):
            pass

        def setup(self):
            super().setup()
            self.connection.settimeout(5)

        def respond(self, code, body, mime='application/json; charset=utf-8', etag=None):
            if isinstance(body, dict):
                body = json.dumps(body).encode()
            self.send_response(code)
            self.send_header('Content-Type', mime)
            self.send_header('Content-Length', str(len(body)))
            self.send_header('Cache-Control', 'no-store')
            if etag is not None: self.send_header('ETag', etag)
            self.end_headers()
            self.wfile.write(body)

        def authorized(self, *tokens):
            supplied = self.headers.get('Authorization', '').encode()
            if any(value and secrets.compare_digest(supplied, ('Bearer ' + value).encode()) for value in tokens):
                return True
            self.close_connection = True
            self.respond(401, {'error': 'unauthorized'})
            return False

        def body(self, limit):
            try:
                size = int(self.headers.get('Content-Length', '0'))
            except ValueError:
                size = 0
            if not 0 < size <= limit or self.headers.get('Transfer-Encoding'):
                self.close_connection = True
                self.respond(413 if size > limit else 400, {'error': 'invalid length'})
                return None
            body = self.rfile.read(size)
            if len(body) != size:
                self.close_connection = True
                self.respond(400, {'error': 'incomplete body'})
                return None
            return body

        def do_GET(self):
            files = {'/': ('index.html', 'text/html'), '/index.html': ('index.html', 'text/html'), '/style.css': ('style.css', 'text/css'), '/app.js': ('app.js', 'application/javascript'), '/vendor/js-yaml.min.js': ('vendor/js-yaml.min.js', 'application/javascript')}
            if self.path in files:
                name, mime = files[self.path]
                self.respond(200, ((generate()['output'] if name in ('app.js', 'style.css') else WEB) / name).read_bytes(), mime + '; charset=utf-8')
            elif self.path == '/healthz':
                self.respond(200, {'v': 2, 'status': 'ok', 'app': 'curio-dev'})
            elif self.path in ('/api/v2/state', '/api/v2/catalog'):
                with config_lock:
                    if self.authorized(config['access']['admin_token']): self.respond(200, core.state(self.path.rsplit('/', 1)[1]))
            elif self.path == '/api/v2/config':
                with config_lock:
                    if self.authorized(config['access']['admin_token']): self.config_response()
            else:
                self.respond(404, {'error': 'not found'})

        def config_response(self):
            self.respond(200, config_yaml, 'application/yaml; charset=utf-8', config_etag(config_yaml))

        def check_match(self):
            supplied = self.headers.get('If-Match')
            if supplied is None or not re.fullmatch(r'"[^"\r\n]+"', supplied):
                self.respond(428, {'error': 'If-Match required'})
                return False
            if supplied != config_etag(config_yaml):
                self.respond(409, {'error': 'configuration changed'})
                return False
            return True

        def mutate_token(self, role, value):
            if not valid_token(value):
                self.respond(400, {'error': 'token must be 16–128 non-whitespace ASCII characters'})
                return
            try:
                save_config(core.config_token(config_yaml, role, value))
            except (ValueError, OSError) as error:
                self.respond(400 if isinstance(error, ValueError) else 500, {'error': str(error)})
                return
            self.config_response()

        def do_POST(self):
            if self.path == '/api/v2/tokens/collector':
                with config_lock:
                    if not self.authorized(config['access']['admin_token']): return
                    self.mutate_token('collector_token', secrets.token_hex(32))
                return
            if self.path != '/api/v2/snapshot':
                self.close_connection = True
                self.respond(404, {'error': 'not found'})
                return
            with config_lock:
                if not self.authorized(config['access']['collector_token']): return
                body = self.body(16384)
                if body is None: return
                result = core.apply(body)
            self.respond([200, 400, 413, 503, 503][result], {'ok': True} if not result else {'error': ['ok', 'invalid snapshot', 'too large', 'capacity', 'out of memory'][result]})

        def do_PUT(self):
            if self.path not in ('/api/v2/config', '/api/v2/tokens/admin'):
                self.close_connection = True
                self.respond(404, {'error': 'not found'})
                return
            with config_lock:
                if not self.authorized(config['access']['admin_token']): return
                body = self.body(16384 if self.path == '/api/v2/config' else 512)
                if body is None: return
                if self.path == '/api/v2/config' and not self.check_match(): return
                if self.path == '/api/v2/tokens/admin':
                    try:
                        value = json.loads(body)
                        if not isinstance(value, dict) or set(value) != {'token'}: raise ValueError('invalid token request')
                    except (ValueError, UnicodeError):
                        self.respond(400, {'error': 'invalid token request'})
                        return
                    self.mutate_token('admin_token', value['token'])
                    return
                if self.headers.get_content_type() not in ('application/yaml', 'text/yaml'):
                    self.respond(415, {'error': 'Content-Type must be application/yaml'})
                    return
                try:
                    save_config(body)
                except (ValueError, OSError) as error:
                    self.respond(400 if isinstance(error, ValueError) else 500, {'error': str(error)})
                    return
                self.config_response()

    server = ThreadingHTTPServer(('127.0.0.1', args.port), Handler)
    print(f'Curio development Hub: http://127.0.0.1:{server.server_port}', flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        core.close()


if __name__ == '__main__':
    main()
