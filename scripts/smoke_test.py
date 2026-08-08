#!/usr/bin/env python3
"""Build + launch smoke test for TraceView (see CONTRIBUTING.md).

Configures and builds the project via CMake, then launches the resulting
executable and checks it stays alive for a few seconds instead of
crashing on startup. This is NOT a UI regression suite -- it doesn't
click anything inside the app. Manual verification of dashboard behavior
(drag, resize, save/load) is still documented in docs/DASHBOARD.md.

Run manually: `python scripts/smoke_test.py [--preset NAME] [--no-preset]`
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _bootstrap import ensure_dependencies  # noqa: E402

ensure_dependencies([], Path(__file__).resolve().parent / ".venv-smoke-test")

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_PRESET = "windows-mingw-qt"
ALIVE_CHECK_SECONDS = 3

PENV_RE = re.compile(r"\$penv\{(\w+)\}")


def expand_penv(value: str) -> str:
    """Resolves CMakePresets' `$penv{VAR}` macro against the current
    process environment (e.g. "...;$penv{PATH}" -> "...;<inherited PATH>")."""
    return PENV_RE.sub(lambda m: os.environ.get(m.group(1), ""), value)


def load_preset_env(preset_name: str) -> dict[str, str] | None:
    """Reads CMakeUserPresets.json (gitignored, machine-specific) for the
    named preset's `environment` block, so the launched exe can find Qt's
    DLLs the same way the build did. Returns None if unavailable."""
    preset_file = REPO_ROOT / "CMakeUserPresets.json"
    if not preset_file.exists():
        return None
    try:
        data = json.loads(preset_file.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return None

    for preset in data.get("configurePresets", []):
        if preset.get("name") == preset_name:
            return preset.get("environment")
    return None


def run(cmd: list[str]) -> subprocess.CompletedProcess:
    print(f"$ {' '.join(cmd)}")
    return subprocess.run(cmd, cwd=REPO_ROOT, capture_output=True, text=True)


def configure_and_build(preset: str | None) -> bool:
    configure_cmd = ["cmake", "--preset", preset] if preset else ["cmake", "-S", ".", "-B", "build"]
    build_cmd = ["cmake", "--build", "--preset", preset] if preset else ["cmake", "--build", "build"]

    for step_name, cmd in (("configure", configure_cmd), ("build", build_cmd)):
        result = run(cmd)
        if result.returncode != 0:
            print(f"FAIL: cmake {step_name} failed\n")
            tail = "\n".join((result.stdout + result.stderr).splitlines()[-40:])
            print(tail)
            return False
    return True


def launch_and_check(env_overrides: dict[str, str] | None) -> bool:
    exe_path = REPO_ROOT / "build" / "TraceView.exe"
    if not exe_path.exists():
        print(f"FAIL: {exe_path} not found after build")
        return False

    env = os.environ.copy()
    if env_overrides:
        for key, value in env_overrides.items():
            env[key] = expand_penv(value)

    process = subprocess.Popen([str(exe_path)], cwd=exe_path.parent, env=env)
    time.sleep(ALIVE_CHECK_SECONDS)

    exit_code = process.poll()
    if exit_code is not None:
        print(f"FAIL: app exited immediately (exit code {exit_code})")
        return False

    print("PASS: app launched and stayed alive")
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preset", default=DEFAULT_PRESET, help=f"CMake preset name (default: {DEFAULT_PRESET})")
    parser.add_argument("--no-preset", action="store_true", help="Configure/build without a CMake preset")
    args = parser.parse_args()

    preset = None if args.no_preset else args.preset

    if not configure_and_build(preset):
        return 1

    env_overrides = load_preset_env(preset) if preset else None
    if preset and env_overrides is None:
        print(
            f"note: no environment found for preset '{preset}' in CMakeUserPresets.json; "
            "launching with the current PATH as-is"
        )

    if not launch_and_check(env_overrides):
        return 1

    print("\nsmoke_test: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
