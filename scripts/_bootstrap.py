"""Shared helper for TraceView's dev scripts (see CONTRIBUTING.md).

Each script calls ensure_dependencies() with the pip packages it needs (an
empty list if it only needs the stdlib, which is the case for both
scripts today). If the list is non-empty, this creates a local venv next
to the script on first run, installs the packages into it, then
re-executes the calling script under that venv's interpreter -- so a
plain `python` on PATH is all a contributor needs to get started.
"""

from __future__ import annotations

import os
import subprocess
import sys
import venv
from pathlib import Path

_BOOTSTRAP_MARKER = "TRACEVIEW_SCRIPT_BOOTSTRAPPED"


def ensure_dependencies(requirements: list[str], venv_dir: Path) -> None:
    if not requirements or os.environ.get(_BOOTSTRAP_MARKER) == "1":
        return

    python = venv_dir / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
    if not python.exists():
        print(f"Creating virtual env at {venv_dir} ...")
        venv.EnvBuilder(with_pip=True).create(venv_dir)
        subprocess.check_call([str(python), "-m", "pip", "install", "--quiet", *requirements])

    env = os.environ.copy()
    env[_BOOTSTRAP_MARKER] = "1"
    os.execve(str(python), [str(python), *sys.argv], env)
