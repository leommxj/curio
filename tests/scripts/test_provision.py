import datetime
import contextlib
import importlib.util
import io
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]


class ConfigureTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='curio-provision-')
        self.addCleanup(self.tmp.cleanup)
        self.output = Path(self.tmp.name) / 'local'

    def configure(self):
        return subprocess.run([
            sys.executable, str(ROOT / 'scripts/firmware/configure.py'), '--ssid', 'Lab "WiFi"',
            '--password-env', 'CURIO_TEST_PASSWORD', '--hostname', 'test-curio', '--ip', '192.0.2.12',
            '--output', str(self.output),
        ], env=dict(os.environ, CURIO_TEST_PASSWORD='test-password'), capture_output=True, text=True)

    def test_generates_private_provisioning_yaml_and_matching_certificate(self):
        result = self.configure()
        self.assertEqual(result.returncode, 0, result.stderr)
        source = (self.output / 'curio.yaml').read_text(encoding='utf-8')
        self.assertLessEqual(len(source.encode('utf-8')), 16384)
        self.assertEqual(set(path.name for path in self.output.iterdir()), {
            'curio.yaml', 'server-cert.pem', 'server-key.pem', 'token.txt', 'admin-token.txt',
        })
        self.assertTrue(all(path.stat().st_mode & 0o777 == 0o600 for path in self.output.iterdir()))
        self.assertIn('  ssid: "Lab \\"WiFi\\""\n', source)
        self.assertIn('  password: "test-password"\n', source)
        self.assertIn('  hostname: "test-curio"\n', source)
        self.assertIn('  admin_token: "' + (self.output / 'admin-token.txt').read_text().strip() + '"', source)
        self.assertIn('  collector_token: "' + (self.output / 'token.txt').read_text().strip() + '"', source)
        self.assertIn('          type: agents\n', source)
        try:
            import yaml
        except ModuleNotFoundError:
            pass
        else:
            config = yaml.safe_load(source)
            self.assertEqual(config['tls']['certificate'], (self.output / 'server-cert.pem').read_text())
            self.assertEqual(config['tls']['private_key'], (self.output / 'server-key.pem').read_text())
        cert = subprocess.run(['openssl', 'x509', '-in', str(self.output / 'server-cert.pem'),
                               '-noout', '-enddate', '-ext', 'subjectAltName'],
                              check=True, capture_output=True, text=True).stdout
        self.assertIn('DNS:test-curio.local', cert)
        self.assertIn('DNS:test-curio', cert)
        self.assertIn('IP Address:192.0.2.12', cert)
        expiry = datetime.datetime.strptime(cert.splitlines()[0].split('=', 1)[1],
                                            '%b %d %H:%M:%S %Y %Z')
        self.assertGreater(expiry, datetime.datetime.now(datetime.timezone.utc).replace(tzinfo=None) +
                           datetime.timedelta(days=3600))
        cert_pubkey = subprocess.run(['openssl', 'x509', '-in', str(self.output / 'server-cert.pem'),
                                      '-pubkey', '-noout'], check=True, capture_output=True).stdout
        key_pubkey = subprocess.run(['openssl', 'pkey', '-in', str(self.output / 'server-key.pem'),
                                     '-pubout'], check=True, capture_output=True).stdout
        self.assertEqual(cert_pubkey, key_pubkey)

    def test_existing_output_is_not_replaced(self):
        self.assertEqual(self.configure().returncode, 0)
        original = (self.output / 'curio.yaml').read_bytes()
        second = self.configure()
        self.assertNotEqual(second.returncode, 0)
        self.assertEqual((self.output / 'curio.yaml').read_bytes(), original)


class ProvisionTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='curio-rpc-')
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.config = self.root / 'curio.yaml'
        spec = importlib.util.spec_from_file_location('curio_provision', ROOT / 'scripts/firmware/provision.py')
        self.provision = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.provision)
        tools = self.root / 'mosaico-tools'
        (tools / 'tools/mosaico_cli').mkdir(parents=True)
        (tools / 'tools/mosaico_cli/cli.py').touch()
        self.provision.TOOL_ROOT = tools

    def test_sends_yaml_via_pinned_rpc_then_restarts(self):
        payload = b'version: 1\n#' + b'a' * 1020 + b'\n'
        self.config.write_bytes(payload)
        calls = []
        def fake_main(args, *, tool_root):
            calls.append((args, tool_root))
            return 0
        module = types.ModuleType('mosaico_cli.cli')
        module.main = fake_main
        with patch.dict(sys.modules, {'mosaico_cli': types.ModuleType('mosaico_cli'),
                                      'mosaico_cli.cli': module}):
            result = self.provision.main(['--config', str(self.config), '--device-id', 'board-1'])
        self.assertEqual(result, 0)
        self.assertEqual(len(calls), 4)
        self.assertEqual(calls[0][0], ['iris', 'rpc', '0x4355', '1', '--payload-hex',
                                       (0).to_bytes(4, 'little').hex() + payload[:1020].hex(),
                                       '--deadline-ms', '10000', '--device-id', 'board-1'])
        self.assertEqual(calls[1][0], ['iris', 'rpc', '0x4355', '1', '--payload-hex',
                                       (1020).to_bytes(4, 'little').hex() + payload[1020:].hex(),
                                       '--deadline-ms', '10000', '--device-id', 'board-1'])
        self.assertEqual(calls[2][0], ['iris', 'rpc', '0x4355', '2', '--deadline-ms', '10000',
                                       '--device-id', 'board-1'])
        self.assertEqual(calls[3][0], ['iris', 'rpc', '0x4355', '3', '--deadline-ms', '3000',
                                       '--device-id', 'board-1'])
        self.assertTrue(all(tool_root == self.provision.TOOL_ROOT for _, tool_root in calls))

    def test_failed_write_does_not_restart(self):
        self.config.write_text('version: 1\n')
        calls = []
        module = types.ModuleType('mosaico_cli.cli')
        module.main = lambda args, **kwargs: calls.append(args) or 5
        with patch.dict(sys.modules, {'mosaico_cli': types.ModuleType('mosaico_cli'),
                                      'mosaico_cli.cli': module}):
            result = self.provision.main(['--config', str(self.config)])
        self.assertEqual(result, 5)
        self.assertEqual(len(calls), 1)

    def test_failed_commit_does_not_restart(self):
        self.config.write_text('version: 1\n')
        calls = []
        module = types.ModuleType('mosaico_cli.cli')
        def fake_main(args, **kwargs):
            calls.append(args)
            return 5 if args[3] == '2' else 0
        module.main = fake_main
        with patch.dict(sys.modules, {'mosaico_cli': types.ModuleType('mosaico_cli'),
                                      'mosaico_cli.cli': module}):
            result = self.provision.main(['--config', str(self.config)])
        self.assertEqual(result, 5)
        self.assertEqual([args[3] for args in calls], ['1', '2'])

    def test_oversize_config_is_rejected_before_rpc(self):
        self.config.write_bytes(b'a' * 16385)
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as error:
            self.provision.main(['--config', str(self.config)])
        self.assertEqual(error.exception.code, 2)


if __name__ == '__main__':
    unittest.main()
