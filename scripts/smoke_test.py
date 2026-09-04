#!/usr/bin/env python3
"""Build + launch smoke test for TraceView (see CONTRIBUTING.md).

Configures and builds the project via CMake, then launches the resulting
executable and checks it stays alive for a few seconds instead of
crashing on startup. This is NOT a UI regression suite -- it doesn't
click anything inside the app. Manual verification of dashboard behavior
(drag, resize, save/load) is still documented in docs/DASHBOARD.md.

Run manually: `python scripts/smoke_test.py [--preset NAME] [--no-preset]`.
The default preset is selected from the host platform.
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
DEFAULT_PRESET = "windows-mingw" if os.name == "nt" else "linux-ninja"
ALIVE_CHECK_SECONDS = 3

ENV_RE = re.compile(r"\$(p?env)\{(\w+)\}")
PRESET_FILES = (REPO_ROOT / "CMakePresets.json", REPO_ROOT / "CMakeUserPresets.json")


def load_presets(section: str) -> dict[str, dict]:
    """Loads public and optional user presets from the repository root."""
    presets: dict[str, dict] = {}
    for preset_file in PRESET_FILES:
        if not preset_file.exists():
            continue
        try:
            data = json.loads(preset_file.read_text(encoding="utf-8"))
        except json.JSONDecodeError as error:
            raise RuntimeError(f"cannot parse {preset_file.name}: {error}") from error
        presets.update({preset["name"]: preset for preset in data.get(section, [])})
    return presets


def resolve_configure_preset(
    preset_name: str, presets: dict[str, dict], resolving: set[str] | None = None
) -> dict:
    """Resolves the small inherited subset needed to launch the built app."""
    if preset_name not in presets:
        raise RuntimeError(f"configure preset '{preset_name}' was not found")

    resolving = set() if resolving is None else resolving
    if preset_name in resolving:
        raise RuntimeError(f"configure preset inheritance cycle at '{preset_name}'")
    resolving.add(preset_name)

    preset = presets[preset_name]
    resolved: dict = {}
    parents = preset.get("inherits", [])
    if isinstance(parents, str):
        parents = [parents]
    for parent_name in reversed(parents):
        parent = resolve_configure_preset(parent_name, presets, resolving)
        inherited_environment = resolved.get("environment", {})
        resolved.update(parent)
        resolved["environment"] = {
            **parent.get("environment", {}),
            **inherited_environment,
        }

    inherited_environment = resolved.get("environment", {})
    resolved.update(preset)
    resolved["environment"] = {
        **inherited_environment,
        **preset.get("environment", {}),
    }
    resolving.remove(preset_name)
    return resolved


def expand_macros(value: str, preset_name: str) -> str:
    """Expands the CMake preset macros used by this repository."""
    value = value.replace("${sourceDir}", str(REPO_ROOT))
    value = value.replace("${sourceParentDir}", str(REPO_ROOT.parent))
    value = value.replace("${presetName}", preset_name)
    return ENV_RE.sub(lambda match: os.environ.get(match.group(2), ""), value)


def preset_launch_details(preset_name: str | None) -> tuple[Path, str | None, dict[str, str]]:
    """Returns the build directory, configuration and launch environment."""
    if preset_name is None:
        return REPO_ROOT / "build", None, {}

    configure_presets = load_presets("configurePresets")
    build_presets = load_presets("buildPresets")
    build_preset = build_presets.get(preset_name)
    if build_preset is None:
        raise RuntimeError(f"build preset '{preset_name}' was not found")

    configure_name = build_preset.get("configurePreset", preset_name)
    configure_preset = resolve_configure_preset(configure_name, configure_presets)
    binary_dir = configure_preset.get("binaryDir")
    if not binary_dir:
        raise RuntimeError(f"configure preset '{configure_name}' has no binaryDir")

    environment = {
        key: expand_macros(value, configure_name)
        for key, value in configure_preset.get("environment", {}).items()
        if value is not None
    }
    return (
        Path(expand_macros(binary_dir, configure_name)),
        build_preset.get("configuration"),
        environment,
    )


def run(cmd: list[str]) -> subprocess.CompletedProcess:
    print(f"$ {' '.join(cmd)}")
    return subprocess.run(cmd, cwd=REPO_ROOT, capture_output=True, text=True)


def configure_and_build(preset: str | None) -> bool:
    configure_cmd = (
        ["cmake", "--preset", preset]
        if preset
        else ["cmake", "-S", ".", "-B", "build"]
    )
    build_cmd = (
        ["cmake", "--build", "--preset", preset]
        if preset
        else ["cmake", "--build", "build"]
    )

    for step_name, cmd in (("configure", configure_cmd), ("build", build_cmd)):
        result = run(cmd)
        if result.returncode != 0:
            print(f"FAIL: cmake {step_name} failed\n")
            tail = "\n".join((result.stdout + result.stderr).splitlines()[-40:])
            print(tail)
            return False
    return True


def find_executable(build_dir: Path, configuration: str | None) -> Path | None:
    """Finds TraceView for both single- and multi-configuration generators."""
    executable_name = "TraceView.exe" if os.name == "nt" else "TraceView"
    configurations = [configuration] if configuration else []
    configurations.extend(["Debug", "Release", "RelWithDebInfo", "MinSizeRel"])

    candidates = [build_dir / executable_name]
    candidates.extend(build_dir / item / executable_name for item in configurations if item)
    return next((candidate for candidate in candidates if candidate.is_file()), None)


def launch_and_check(
    build_dir: Path, configuration: str | None, env_overrides: dict[str, str]
) -> bool:
    exe_path = find_executable(build_dir, configuration)
    if exe_path is None:
        print(f"FAIL: TraceView executable not found under {build_dir}")
        return False

    env = os.environ.copy()
    env.update(env_overrides)

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
    parser.add_argument(
        "--preset",
        default=DEFAULT_PRESET,
        help=f"CMake preset name (default: {DEFAULT_PRESET})",
    )
    parser.add_argument(
        "--no-preset",
        action="store_true",
        help="Configure/build without a CMake preset",
    )
    args = parser.parse_args()

    preset = None if args.no_preset else args.preset

    try:
        build_dir, configuration, env_overrides = preset_launch_details(preset)
    except RuntimeError as error:
        print(f"FAIL: {error}")
        return 1

    if not configure_and_build(preset):
        return 1

    if not launch_and_check(build_dir, configuration, env_overrides):
        return 1

    print("\nsmoke_test: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
