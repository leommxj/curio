#!/usr/bin/env python3
"""Host-only API/collector integration against the firmware C core."""
import json
import os
from pathlib import Path
import select
import re
import subprocess
import sys
import tempfile
import time
import unittest
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
COLLECTOR_TOKEN = 'integration-collector-token'
ADMIN_TOKEN = 'integration-admin-token'


class CollectorHubIntegration(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build_dir = tempfile.TemporaryDirectory(prefix='curio-integration-build-')
        cls.binary = Path(cls.build_dir.name) / 'collector'
        go = os.environ.get('GO') or 'go'
        subprocess.run([go, 'build', '-buildvcs=false', '-o', str(cls.binary), '.'], cwd=ROOT / 'collector', check=True)

    @classmethod
    def tearDownClass(cls):
        cls.build_dir.cleanup()

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='curio-integration-')
        self.root = Path(self.directory.name)
        (self.root / 'collector-token').write_text(COLLECTOR_TOKEN)
        (self.root / 'admin-token').write_text(ADMIN_TOKEN)
        self.admin_token = ADMIN_TOKEN
        self.collector_token = COLLECTOR_TOKEN
        self.collectors = []
        self.start_hub()

    def tearDown(self):
        for process in self.collectors:
            self.stop(process)
        self.stop(self.hub)
        self.directory.cleanup()

    @staticmethod
    def stop(process):
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
        if process.stdout:
            process.stdout.close()
        if process.stderr:
            process.stderr.close()

    def start_hub(self):
        self.hub = subprocess.Popen([
            sys.executable, str(ROOT / 'scripts/dev/hub.py'), '--port', '0',
            '--token-file', str(self.root / 'collector-token'),
            '--admin-token-file', str(self.root / 'admin-token'),
            '--config-dir', str(self.root / 'state'),
        ], cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        self.addCleanup(self.stop, self.hub)
        ready, _, _ = select.select([self.hub.stdout], [], [], 180)
        self.assertTrue(ready, 'development Hub startup timed out')
        line = self.hub.stdout.readline().strip()
        self.assertTrue(line.startswith('Curio development Hub:'), line)
        self.endpoint = line.split(': ', 1)[1]

    def request(self, method, path, body=None, token=None, headers=None):
        if body is not None and not isinstance(body, bytes):
            body = json.dumps(body).encode()
        request = urllib.request.Request(self.endpoint + path, data=body, method=method,
                                         headers={'Authorization': 'Bearer ' + (token or self.admin_token),
                                                  'Content-Type': 'application/json', **(headers or {})})
        try:
            response = urllib.request.urlopen(request, timeout=5)
        except urllib.error.HTTPError as error:
            response = error
        with response:
            raw = response.read()
            value = raw.decode() if 'yaml' in response.headers.get('Content-Type', '') else json.loads(raw)
            self.last_headers = response.headers
            return response.status, value

    def config(self):
        code, body = self.request('GET', '/api/v2/config')
        self.assertEqual(code, 200, body)
        self.assertIsInstance(body, str)
        self.assertIn('yaml', self.last_headers['Content-Type'])
        return body, self.last_headers['ETag']

    def put_config(self, body, etag):
        return self.request('PUT', '/api/v2/config', body.encode(),
                            headers={'Content-Type': 'application/yaml', 'If-Match': etag})

    def board_yaml(self, extra=''):
        return (f'access:\n  admin_token: {self.admin_token}\n'
                f'  collector_token: {self.collector_token}\n'
                'screen:\n  pages:\n    - id: home\n      widgets:\n        - type: agents\n          at: [0, 0]\n          size: [4, 6]\n' + extra)

    def state(self):
        code, state = self.request('GET', '/api/v2/state')
        self.assertEqual(code, 200, state)
        return state

    def post(self, host, sources):
        return self.request('POST', '/api/v2/snapshot',
                            {'v': 2, 'host_id': host, 'host_name': host, 'sources': sources}, self.collector_token)

    @staticmethod
    def metrics(**values):
        return {'kind': 'metrics', 'status': 'ok', 'sample_age_ms': 0,
                'metrics': {key: {'unit': 'percent', 'value': value} for key, value in values.items()}}

    def test_status_sources_and_independent_freshness(self):
        sources = {
            'weather': dict(self.metrics(temperature=23), sample_age_ms=20000, max_age_ms=120000),
            'cpu': dict(self.metrics(usage_pct=10), sample_age_ms=20000, max_age_ms=10000),
            'service': {'kind': 'status', 'status': 'ok', 'sample_age_ms': 0, 'max_age_ms': 60000,
                        'state': {'text': 'Failed', 'level': 'error', 'detail': 'HTTP 503'}},
        }
        self.assertEqual(self.post('general', sources)[0], 200)
        h = self.state()['hosts'][0]
        self.assertEqual(h['health'], 'fresh')
        self.assertEqual(h['sources']['weather']['health'], 'fresh')
        self.assertEqual(h['sources']['cpu']['health'], 'stale')
        service = h['sources']['service']
        self.assertEqual(service['status'], 'ok')
        self.assertEqual(service['health'], 'fresh')
        self.assertEqual(service['state']['level'], 'error')
        self.assertEqual(service['max_age_ms'], 60000)
        code, catalog = self.request('GET', '/api/v2/catalog')
        self.assertEqual(code, 200)
        self.assertNotIn('state', catalog['hosts'][0]['sources']['service'])
        _, etag = self.config()
        yaml = (f'access: {{admin_token: {self.admin_token}, collector_token: {self.collector_token}}}\n'
                'screen:\n  pages:\n    - id: service\n      widgets:\n'
                '        - {type: status, host: general, source: service, at: [0, 0], size: [2, 2]}\n')
        code, saved = self.put_config(yaml, etag)
        self.assertEqual(code, 200, saved)
        self.assertIn('type: status', saved)
        sources['service'] = {'kind': 'status', 'status': 'error', 'sample_age_ms': 0, 'max_age_ms': 60000}
        self.assertEqual(self.post('general', sources)[0], 200)
        service = self.state()['hosts'][0]['sources']['service']
        self.assertEqual(service['health'], 'unavailable')
        self.assertEqual(service['state']['text'], 'Failed')
        self.assertEqual(service['state']['detail'], 'HTTP 503')
        sources['service']['state'] = {'text': 'Online'}
        self.assertEqual(self.post('general', sources)[0], 400)
        self.assertEqual(self.state()['hosts'][0]['sources']['service']['state']['text'], 'Failed')

    def test_token_permissions_and_capacity(self):
        for path in ('state', 'catalog', 'config'):
            self.assertEqual(self.request('GET', '/api/v2/' + path, token=self.collector_token)[0], 401)
        body, etag = self.config()
        self.assertEqual(self.request('PUT', '/api/v2/config', body.encode(), self.collector_token,
                                     {'Content-Type': 'application/yaml', 'If-Match': etag})[0], 401)
        self.assertEqual(self.request('POST', '/api/v2/snapshot', {}, self.admin_token)[0], 401)
        for number in range(4):
            self.assertEqual(self.post('host-' + str(number), {})[0], 200)
        self.assertEqual(self.post('fifth-host', {})[0], 503)
        self.assertEqual(len(self.state()['hosts']), 4)

    def test_yaml_roundtrip_validation_etag_and_restart(self):
        original, etag = self.config()
        self.assertIn(ADMIN_TOKEN, original)
        self.assertIn(COLLECTOR_TOKEN, original)
        body = self.board_yaml('device: {brightness: 42, rotation: 270, auto_rotate: true}\n'
                               'alerts: {volume: 0, rules: [{type: agent_done, duration: 0, sound: true}]}\n')
        code, saved = self.put_config(body, etag)
        self.assertEqual(code, 200, saved)
        self.assertIsInstance(saved, str)
        saved_etag = self.last_headers['ETag']
        self.assertNotEqual(etag, saved_etag)
        for field in ('brightness: 42', 'rotation: 270', 'auto_rotate: true', 'duration: 0'):
            self.assertIn(field, saved)
        for default in ('auto_page:', 'hold:', 'revision:', 'include_idle:'):
            self.assertNotIn(default, saved)
        self.assertEqual(self.put_config(body, etag)[0], 409)
        self.assertEqual(self.config(), (saved, saved_etag))
        for suffix in ('unknown: true\n', 'device: {brightness: 1, brightness: 2}\n',
                       'device: {auto_rotate: yes}\n', '---\nother: document\n'):
            self.assertEqual(self.put_config(self.board_yaml(suffix), saved_etag)[0], 400, suffix)
            self.assertEqual(self.config(), (saved, saved_etag))
        self.assertEqual(self.put_config(body + '#' + 'x' * 65536, saved_etag)[0], 413)
        self.assertEqual(self.config(), (saved, saved_etag))
        self.assertEqual(self.request('PUT', '/api/v2/config', body.encode(),
                                     headers={'Content-Type': 'application/yaml'})[0], 428)
        self.stop(self.hub)
        files = list((self.root / 'state').iterdir())
        self.assertEqual([path.suffix for path in files], ['.yaml'])
        self.assertEqual(files[0].read_text(), saved)
        self.start_hub()
        restored, restored_etag = self.config()
        self.assertEqual(restored, saved)
        self.assertEqual(restored_etag, saved_etag)
        self.assertEqual(self.state()['hosts'], [])
        code, normalized = self.put_config(restored, restored_etag)
        self.assertEqual(code, 200, normalized)
        self.assertEqual(normalized, restored)

    def test_token_rotation_persists_and_new_collector_posts(self):
        code, saved = self.request('POST', '/api/v2/tokens/collector')
        self.assertEqual(code, 200, saved)
        match = re.search(r'collector_token: [\"\']?([a-f0-9]{64})', saved)
        self.assertIsNotNone(match, saved)
        new_collector = match.group(1)
        self.assertEqual(self.request('POST', '/api/v2/snapshot', {}, COLLECTOR_TOKEN)[0], 401)
        self.collector_token = new_collector
        self.assertEqual(self.post('new-collector', {})[0], 200)
        new_admin = 'rotated-admin-token-123456'
        code, saved = self.request('PUT', '/api/v2/tokens/admin', {'token': new_admin})
        self.assertEqual(code, 200, saved)
        self.admin_token = new_admin
        self.assertEqual(self.request('GET', '/api/v2/config', token=ADMIN_TOKEN)[0], 401)
        self.assertIn(new_admin, self.config()[0])
        self.stop(self.hub)
        self.start_hub()
        self.assertIn(new_admin, self.config()[0])
        self.assertEqual(self.request('GET', '/api/v2/state', token=ADMIN_TOKEN)[0], 401)
        self.assertEqual(self.post('after-restart', {})[0], 200)
        self.assertEqual(self.request('POST', '/api/v2/snapshot', {}, COLLECTOR_TOKEN)[0], 401)

    def test_field_source_revocation_and_body_limit(self):
        self.assertEqual(self.post('revocation', {'cpu': self.metrics(usage_pct=10, old=20), 'memory': self.metrics(usage_pct=30)})[0], 200)
        self.assertEqual(self.post('revocation', {'cpu': self.metrics(usage_pct=12)})[0], 200)
        host = self.state()['hosts'][0]
        self.assertEqual(set(host['sources']), {'cpu'})
        self.assertEqual(set(host['sources']['cpu']['metrics']), {'usage_pct'})
        before = self.state()
        oversized = json.dumps({'v': 2, 'host_id': 'revocation', 'host_name': 'Revocation', 'sources': {}}).encode() + b' ' * 16385
        self.assertEqual(self.request('POST', '/api/v2/snapshot', oversized, COLLECTOR_TOKEN)[0], 413)
        self.assertEqual(set(self.state()['hosts'][0]['sources']), set(before['hosts'][0]['sources']))
        self.assertEqual(self.post('revocation', {})[0], 200)
        self.assertEqual(self.state()['hosts'][0]['sources'], {})

    def test_failed_source_preserves_value_age_and_declared_fields(self):
        self.assertEqual(self.post('cached', {'cpu': self.metrics(usage_pct=23, removed=1)})[0], 200)
        time.sleep(.04)
        failure = {'kind': 'metrics', 'status': 'error', 'sample_age_ms': 0,
                   'metrics': {'usage_pct': {'unit': 'percent'}}}
        self.assertEqual(self.post('cached', {'cpu': failure})[0], 200)
        source = self.state()['hosts'][0]['sources']['cpu']
        self.assertEqual(source['status'], 'error')
        self.assertEqual(set(source['metrics']), {'usage_pct'})
        self.assertEqual(source['metrics']['usage_pct']['value'], 23)
        age = source['metrics']['usage_pct']['age_ms']
        self.assertGreaterEqual(age, 30)
        time.sleep(.03)
        self.assertEqual(self.post('cached', {'cpu': failure})[0], 200)
        later = self.state()['hosts'][0]['sources']['cpu']['metrics']['usage_pct']
        self.assertGreaterEqual(later['age_ms'], age + 20)

    def test_source_metric_capacity_rejection_keeps_live_state(self):
        sources = {'source-' + str(i): self.metrics(**{'metric-' + str(j): i * 4 + j for j in range(4)})
                   for i in range(8)}
        code, result = self.post('capacity', sources)
        self.assertEqual(code, 200, result)
        expected = {key: value['metrics'] for key, value in sources.items()}

        def assert_retained():
            hosts = self.state()['hosts']
            self.assertEqual(len(hosts), 1)
            self.assertEqual(hosts[0]['host_id'], 'capacity')
            actual = {source_id: {key: {'unit': metric['unit'], 'value': metric['value']}
                                  for key, metric in source['metrics'].items()}
                      for source_id, source in hosts[0]['sources'].items()}
            self.assertEqual(actual, expected)
            self.assertEqual(self.request('GET', '/api/v2/catalog')[0], 200)
            self.assertIsNone(self.hub.poll(), 'Hub exited after capacity rejection')

        assert_retained()
        too_many_sources = dict(sources, **{'source-8': self.metrics()})
        code, result = self.post('capacity', too_many_sources)
        self.assertIn(code, (400, 413, 503), result)
        assert_retained()
        too_many_metrics = json.loads(json.dumps(sources))
        too_many_metrics['source-0']['metrics']['extra'] = {'unit': 'percent', 'value': 99}
        code, result = self.post('capacity', too_many_metrics)
        self.assertIn(code, (400, 413, 503), result)
        assert_retained()
        # A valid follow-up on the same process proves the receiver remains writable.
        self.assertEqual(self.post('capacity', sources)[0], 200)
        assert_retained()

    def test_config_init_check_and_restart_preserve_identity_and_update_fields(self):
        if not sys_platform_linux():
            self.skipTest('Linux native sampling evidence')
        path = self.root / 'config directory' / 'collector.yaml'

        def run(*args):
            result = subprocess.run([str(self.binary), *args, '--config', str(path)],
                                    cwd=ROOT, capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            return result

        run('init')
        self.assertIn('sources:', path.read_text())
        config = (f'endpoint: {self.endpoint}\nallow_http_loopback: true\n'
                  'token_file: ../collector-token\nsources:\n  system.memory:\n'
                  '    module: memory\n    fields: [usage_pct]\n')
        path.write_text(config)
        identity = path.parent / 'host-id'
        run('check')
        self.assertFalse(identity.exists(), 'check created an identity')
        self.assertEqual(self.state()['hosts'], [], 'check posted data')
        run('--once')
        host_id = identity.read_text().strip()
        first = self.state()['hosts']
        self.assertEqual(len(first), 1)
        self.assertEqual(first[0]['host_id'], host_id)
        self.assertEqual(set(first[0]['sources']['system.memory']['metrics']), {'usage_pct'})
        config = config.replace('[usage_pct]', '[available_bytes]')
        path.write_text(config)
        run('--once')
        second = self.state()['hosts']
        self.assertEqual(len(second), 1)
        self.assertEqual(second[0]['host_id'], host_id)
        self.assertEqual(identity.read_text().strip(), host_id)
        self.assertEqual(set(second[0]['sources']['system.memory']['metrics']), {'available_bytes'})
        duplicate = subprocess.run([str(self.binary), 'init', '--config', str(path)],
                                   capture_output=True, text=True, timeout=5)
        self.assertNotEqual(duplicate.returncode, 0)
        self.assertEqual(path.read_text(), config)

    def test_real_linux_collection_with_failed_herdr(self):
        if not sys_platform_linux():
            self.skipTest('Linux native sampling evidence')
        fixture_cli = self.root / 'herdr-fixture'
        native = {'result': {'type': 'agent_list', 'agents': [
            {'terminal_id': 'fixture-agent', 'agent': 'pi', 'agent_status': 'working',
             'display_agent': 'Integration title', 'cwd': '/fixture/project'}]}}
        fixture_cli.write_text('#!/usr/bin/env python3\nprint(' + repr(json.dumps(native)) + ')\n')
        fixture_cli.chmod(0o700)
        config = (f'host_id: real-linux\nhost_name: Linux integration\nendpoint: {self.endpoint}\n'
                  f'token_file: {self.root / "collector-token"}\n'
                  'allow_http_loopback: true\npublish_interval: 60ms\ntimeout: 1s\nsources:\n'
                  '  system.cpu: {module: cpu, interval: 40ms, fields: [usage_pct, logical_count]}\n'
                  '  system.memory: {module: memory, interval: 40ms}\n'
                  f'  disk: {{module: disk, path: {self.root}, interval: 40ms, max_age: 1m}}\n'
                  '  network: {module: network, interface: lo, interval: 40ms}\n'
                  f'  herdr: {{module: herdr, interval: 40ms, timeout: 30ms, socket: {self.root / "missing-herdr.sock"}}}\n'
                  f'  fixture.herdr: {{module: herdr, cli: true, binary: {fixture_cli}, allow_titles: true, interval: 40ms}}\n')
        path = self.root / 'collector.yaml'
        path.write_text(config)
        process = subprocess.Popen([str(self.binary), '--config', str(path)], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
        self.collectors.append(process)
        deadline = time.monotonic() + 8
        observed = None
        while time.monotonic() < deadline:
            self.assertIsNone(process.poll(), 'collector stopped unexpectedly')
            hosts = self.state()['hosts']
            if hosts:
                observed = hosts[0]
                sources = observed['sources']
                if all(sources[key]['status'] == 'ok' for key in ('system.cpu', 'system.memory', 'disk', 'network', 'fixture.herdr')):
                    break
            time.sleep(.04)
        else:
            self.fail('native collection never became ready: ' + repr(observed))
        sources = observed['sources']
        self.assertEqual(sources['herdr']['status'], 'error')
        agent = sources['fixture.herdr']['agents'][0]
        self.assertEqual(agent['brand'], 'pi')
        self.assertEqual(agent['name'], 'Integration title')
        self.assertEqual(agent['project'], 'project')
        cpu = sources['system.cpu']['metrics']
        memory = sources['system.memory']['metrics']
        self.assertTrue(0 <= cpu['usage_pct']['value'] <= 100)
        self.assertGreater(cpu['logical_count']['value'], 0)
        self.assertEqual(memory['used_bytes']['value'], memory['total_bytes']['value'] - memory['available_bytes']['value'])
        self.assertTrue(0 <= memory['usage_pct']['value'] <= 100)
        disk = sources['disk']['metrics']
        network = sources['network']['metrics']
        self.assertEqual(sources['disk']['max_age_ms'], 60000)
        self.assertTrue(0 <= disk['usage_pct']['value'] <= 100)
        self.assertGreater(disk['total_bytes']['value'], 0)
        self.assertGreaterEqual(disk['free_bytes']['value'], 0)
        self.assertGreater(disk['total_bytes']['value'], disk['free_bytes']['value'])
        self.assertEqual(set(network), {'rx_bytes_per_second', 'tx_bytes_per_second'})
        for metric in network.values():
            self.assertEqual(metric['unit'], 'bytes_per_second')
            self.assertGreaterEqual(metric['value'], 0)
        self.assertEqual(self.request('GET', '/api/v2/catalog')[0], 200)
        print('Linux sample:', json.dumps({'cpu_pct': cpu['usage_pct']['value'],
                                          'logical_count': cpu['logical_count']['value'],
                                          'memory_total_bytes': memory['total_bytes']['value'],
                                          'memory_used_bytes': memory['used_bytes']['value'],
                                          'herdr_status': sources['herdr']['status']}), flush=True)


def sys_platform_linux():
    import sys
    return sys.platform.startswith('linux')


if __name__ == '__main__':
    unittest.main(verbosity=2)
