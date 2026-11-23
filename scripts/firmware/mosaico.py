#!/usr/bin/env python3
"""Run the pinned Mosaico tools for this workspace."""

from pathlib import Path
import os
import sys

ROOT = Path(__file__).resolve().parents[2]
UTILS_ROOT = Path(os.environ.get("MOSAICO_UTILS_ROOT", ROOT / ".deps/esp-mosaico-utils")).expanduser().resolve()
TOOL_ROOT = UTILS_ROOT / "mosaico-tools"
if not (TOOL_ROOT / "tools/mosaico_cli/cli.py").is_file():
    raise SystemExit("Run make deps first")
sys.path.insert(0, str(TOOL_ROOT / "tools"))

from mosaico_cli.cli import main

if __name__ == "__main__":
    os.environ.setdefault("MOSAICO_WORKSPACE", str(ROOT))
    raise SystemExit(main(tool_root=TOOL_ROOT))
