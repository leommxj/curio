import importlib.util
import os
from pathlib import Path
import pwd
import shutil
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('collector_service', ROOT / 'scripts/collector/service.py')
service = importlib.util.module_from_spec(spec)
spec.loader.exec_module(service)


class CollectorScriptsTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.bin = self.root / 'bin'
        self.bin.mkdir()
        self.log = self.root / 'calls'
        self.target = self.root / 'installed/collector'
        self.unit = self.root / 'units/curio-collector.service'
        self.config_root = self.root / 'config root'
        self.config_root.mkdir()
        self.config = self.config_root / 'collector.yaml'
        self.config.write_text('original credentials')
        self.collector = self.fake('collector', 'printf "check:%s\\n" "$*" >> "$CALLS"\nexit "${CHECK_EXIT:-0}"\n')
        self.fake('systemctl', 'printf "systemctl:%s\\n" "$*" >> "$CALLS"\nif [[ "$*" == *FragmentPath* ]]; then echo "$TEST_UNIT"; fi\nif [[ "$*" == *restart* ]]; then exit "${RESTART_EXIT:-0}"; fi\nexit "${CTL_EXIT:-0}"\n')
        self.fake('runuser', 'printf "runuser:%s\\n" "$*" >> "$CALLS"\nshift 3\nexec "$@"\n')
        self.env = patch.dict(os.environ, PATH=str(self.bin) + ':' + os.environ['PATH'], CALLS=str(self.log), TEST_UNIT=str(self.unit))
        self.env.start()
        self.addCleanup(self.env.stop)
        self.paths = patch.object(service, 'service_paths', return_value=(self.target, self.unit))
        self.paths.start()
        self.addCleanup(self.paths.stop)
        self.account = pwd.getpwuid(os.getuid())

    def fake(self, name, body):
        path = self.bin / name
        path.write_text('#!/bin/bash\n' + body)
        path.chmod(0o755)
        return path

    def deploy(self, system=False, action='install'):
        service.deploy(SimpleNamespace(system=system, action=action), self.collector,
                       self.config, self.config_root, self.account)

    def test_user_install_real_files_and_commands(self):
        self.deploy()
        unit = self.unit.read_text()
        self.assertIn('ExecStart="' + str(self.target), unit)
        self.assertIn('--config=' + str(self.config), unit)
        self.assertIn('WantedBy=default.target', unit)
        self.assertNotIn('User=', unit)
        self.assertIn('Environment="XDG_CONFIG_HOME=' + str(self.config_root), unit)
        self.assertEqual(self.target.read_bytes(), self.collector.read_bytes())
        self.assertEqual(self.config.read_text(), 'original credentials')
        calls = self.log.read_text().splitlines()
        self.assertEqual(calls[0], 'systemctl:--user show --property=Version')
        self.assertTrue(calls[1].startswith('check:check --config '))
        self.assertEqual(calls[2:], ['systemctl:--user daemon-reload',
                                    'systemctl:--user show --property=FragmentPath --value curio-collector.service',
                                    'systemctl:--user enable curio-collector.service',
                                    'systemctl:--user restart curio-collector.service',
                                    'systemctl:--user is-active --quiet curio-collector.service'])

    def test_system_install_checks_as_run_user(self):
        self.deploy(system=True)
        unit = self.unit.read_text()
        self.assertIn('User=' + self.account.pw_name, unit)
        self.assertIn('HOME=' + self.account.pw_dir, unit)
        self.assertIn('XDG_CONFIG_HOME=' + str(self.config_root), unit)
        self.assertIn('WantedBy=multi-user.target', unit)
        calls = self.log.read_text().splitlines()
        self.assertTrue(calls[1].startswith('runuser:-u ' + self.account.pw_name))
        self.assertIn('systemctl:enable curio-collector.service', calls)
        self.assertEqual(self.config.read_text(), 'original credentials')

    def test_check_failure_changes_nothing(self):
        with patch.dict(os.environ, CHECK_EXIT='4'):
            with self.assertRaises(subprocess.CalledProcessError):
                self.deploy()
        self.assertFalse(self.unit.exists())
        self.assertFalse(self.target.exists())
        self.assertNotIn('enable', self.log.read_text())

    def test_restart_failure_propagates(self):
        with patch.dict(os.environ, RESTART_EXIT='5'):
            with self.assertRaises(subprocess.CalledProcessError):
                self.deploy()
        self.assertNotIn('is-active', self.log.read_text())

    def test_unavailable_manager_does_not_overwrite(self):
        self.deploy()
        previous = self.target.read_bytes(), self.unit.read_bytes()
        with patch.dict(os.environ, CTL_EXIT='5'):
            with self.assertRaises(subprocess.CalledProcessError):
                self.deploy()
        self.assertEqual(previous, (self.target.read_bytes(), self.unit.read_bytes()))

    def test_overrides_refused(self):
        override = self.unit.parent / (service.SERVICE + '.d')
        override.mkdir(parents=True)
        (override / 'custom.conf').write_text('[Service]\nExecStart=/other')
        with self.assertRaisesRegex(ValueError, 'overrides'):
            self.deploy()
        self.assertFalse(self.target.exists())

    def test_uninstall_preserves_binary_and_config_and_is_idempotent(self):
        self.deploy()
        self.log.unlink()
        self.deploy(action='uninstall')
        self.assertFalse(self.unit.exists())
        self.assertTrue(self.target.exists())
        self.assertEqual(self.config.read_text(), 'original credentials')
        self.assertEqual(self.log.read_text().splitlines(), [
            'systemctl:--user disable --now curio-collector.service', 'systemctl:--user daemon-reload'])
        self.deploy(action='uninstall')

    def test_sudo_preserves_user_config_directory(self):
        caller = SimpleNamespace(pw_uid=1000, pw_name='alice', pw_dir='/home/alice')
        other = SimpleNamespace(pw_uid=1001, pw_name='bob', pw_dir='/home/bob')
        self.fake('sudo', 'printf "%s\\n" "$@" > "$CALLS"\n')
        for selected in (None, 'bob'):
            with self.subTest(run_as=selected), \
                    patch.object(service.os, 'getuid', return_value=1000), \
                    patch.object(service.pwd, 'getpwuid', return_value=caller), \
                    patch.object(service.pwd, 'getpwnam', return_value=other), \
                    patch.dict(os.environ, XDG_CONFIG_HOME=str(self.config_root)):
                args = ['install', '--system', '--binary', str(self.collector)]
                if selected:
                    args += ['--run-as', selected]
                service.main(args)
                forwarded = self.log.read_text().splitlines()
                root = Path('/home/bob/.config') if selected else self.config_root
                self.assertIn('CURIO_DEPLOY_CONFIG_ROOT=' + str(root), forwarded)
                self.assertIn(str(root / 'curio/collector.yaml'), forwarded)
                self.assertIn(selected or 'alice', forwarded)
                self.assertFalse(self.target.exists())

    def test_invalid_mode_or_root_target_never_deploys(self):
        caller = SimpleNamespace(pw_uid=0, pw_name='root', pw_dir='/root')
        with patch.object(service.os, 'getuid', return_value=0), \
                patch.object(service.pwd, 'getpwuid', return_value=caller), \
                patch.object(service.pwd, 'getpwnam', return_value=caller):
            for args in (['install', '--user'], ['install', '--system'],
                         ['install', '--system', '--run-as', 'root']):
                args += ['--binary', str(self.collector)]
                with self.assertRaises(ValueError):
                    service.main(args)
        self.assertFalse(self.log.exists())

    def test_generated_units_pass_systemd_validation(self):
        analyze = shutil.which('systemd-analyze')
        if not analyze:
            self.skipTest('systemd-analyze unavailable')
        self.target = self.root / 'space $cash %h' / 'collector'
        self.paths.return_value = self.target, self.unit
        self.config = self.config_root / 'space $cash %h "quoted".yaml'
        self.config.write_text('original credentials')
        for system in (False, True):
            with self.subTest(system=system):
                self.deploy(system=system)
                command = [analyze] + ([] if system else ['--user']) + ['verify', str(self.unit)]
                result = subprocess.run(command, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_mismatched_manager_directory_does_not_start(self):
        with patch.dict(os.environ, TEST_UNIT='/other/curio-collector.service'):
            with self.assertRaisesRegex(ValueError, 'Systemd loaded'):
                self.deploy()
        calls = self.log.read_text()
        self.assertNotIn('enable', calls)
        self.assertNotIn('restart', calls)

    def test_uninstall_help_has_no_binary(self):
        result = subprocess.run([sys.executable, str(ROOT / 'scripts/collector/service.py'), 'uninstall', '--help'],
                                cwd=self.root, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0)
        self.assertNotIn('--binary', result.stdout)


if __name__ == '__main__':
    unittest.main()
