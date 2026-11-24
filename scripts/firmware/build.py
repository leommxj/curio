#!/usr/bin/env python3
"""Build the application and its official System Update bundle."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--idf-path', type=Path, default=os.environ.get('IDF_PATH'))
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build/firmware')
    args = parser.parse_args()
    if not args.idf_path:
        parser.error('activate the ESP-IDF environment or set IDF_PATH')
    idf = args.idf_path.expanduser().resolve()
    expected = json.loads((ROOT / 'dependencies.json').read_text())['idf_commit']
    actual = subprocess.check_output(['git', '-C', str(idf), 'rev-parse', 'HEAD'], text=True).strip()
    if actual != expected:
        parser.error(f'ESP-IDF revision mismatch: need {expected}, found {actual}')
    tool_root = ROOT / '.deps/esp-mosaico-utils/mosaico-tools'
    sys.path.insert(0, str(tool_root / 'tools'))
    from mosaico_cli.errors import MosaicoError
    from mosaico_cli.gateway import ensure_iris_tools
    from mosaico_cli.runtime import RunContext
    from mosaico_cli.workspace import load_workspace
    try:
        workspace = load_workspace(tool_root, explicit=str(ROOT))
        iris_python, _ = ensure_iris_tools(RunContext(workspace, 'firmware'))
    except MosaicoError as error:
        raise SystemExit(str(error))
    environment = dict(os.environ, IDF_PATH=str(idf))
    python_env = environment.get('IDF_PYTHON_ENV_PATH')
    python = str(Path(python_env) / 'bin/python') if python_env else sys.executable
    subprocess.run([python, str(idf / 'tools/idf.py'), '-C', str(ROOT / 'firmware'),
                    '-B', str(args.build_dir.expanduser().resolve()),
                    '-D', f'ESP_IRIS_PYTHON={iris_python}', 'build', 'system-update-bundle'],
                   env=environment, check=True)


if __name__ == '__main__':
    try:
        main()
    except (OSError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error))
