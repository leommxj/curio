import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class WorkflowTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='curio-workflow-')
        self.addCleanup(self.tmp.cleanup)
        self.folder = Path(self.tmp.name)
        self.log = self.folder / 'calls.jsonl'
        self.runner = self.folder / 'command stub'
        self.runner.write_text(f'#!{sys.executable}\n' + '''import json, os, sys
with open(os.environ['CURIO_TEST_CALLS'], 'a') as stream:
    stream.write(json.dumps(sys.argv[1:]) + '\\n')
sys.exit(7 if os.environ.get('CURIO_TEST_FAIL') in sys.argv[1:] else 0)
''')
        self.runner.chmod(0o755)

    def run_make(self, *args, fail=None):
        env = dict(os.environ, CURIO_TEST_CALLS=str(self.log))
        if fail:
            env['CURIO_TEST_FAIL'] = fail
        result = subprocess.run([shutil.which('make'), '-s', '--no-print-directory', '-C', str(ROOT),
                                 'PYTHON=' + str(self.runner), 'GO=' + str(self.runner),
                                 'BUILD_DIR=' + str(self.folder / 'build output'), *args],
                                env=env, capture_output=True, text=True)
        calls = [json.loads(line) for line in self.log.read_text().splitlines()] if self.log.exists() else []
        return result, calls

    def test_flash_builds_before_installing(self):
        result, calls = self.run_make('flash', 'DEVICE=board-1')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual([c[0] for c in calls], ['scripts/bootstrap.py', 'scripts/firmware/build.py', 'scripts/firmware/mosaico.py'])
        bundle = str(self.folder / 'build output/firmware/curio-system-update.irisfw')
        self.assertEqual(calls[-1][1:], ['iris', 'system-update', '--bundle', bundle, '--device-id', 'board-1'])

    def test_firmware_failure_stops_flash(self):
        result, calls = self.run_make('flash', fail='scripts/firmware/build.py')
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual([c[0] for c in calls], ['scripts/bootstrap.py', 'scripts/firmware/build.py'])

    def test_existing_bundle_skips_build(self):
        bundle = str(self.folder / "release's firmware.irisfw")
        result, calls = self.run_make('flash', 'BUNDLE=' + bundle)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual([c[0] for c in calls], ['scripts/bootstrap.py', 'scripts/firmware/mosaico.py'])
        self.assertEqual(calls[-1][-1], bundle)

    def test_collector_build_does_not_need_firmware_tools(self):
        result, calls = self.run_make('collector')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(calls, [['-C', 'collector', 'build', '-buildvcs=false', '-o',
                                 str(self.folder / 'build output/curio-collector'), '.']])

    def test_collector_failure_stops_installation(self):
        result, calls = self.run_make('install-collector', 'MODE=system', fail='build')
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(len(calls), 1)
        self.assertIn('build', calls[0])

    def test_existing_binary_and_config_paths(self):
        binary = str(self.folder / "collector's binary")
        config = str(self.folder / 'user config/collector.yaml')
        result, calls = self.run_make('install-collector', 'MODE=system', 'BINARY=' + binary,
                                      'CONFIG=' + config, 'RUN_AS=alice')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(calls, [['scripts/collector/service.py', 'install', '--system', '--binary', binary,
                                 '--config', config, '--run-as', 'alice']])


if __name__ == '__main__':
    unittest.main()
