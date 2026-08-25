#!/usr/bin/env python3
"""Checks TraceView's C++ source against CONTRIBUTING.md's "Code style"
rules: .clang-format formatting (delegated to clang-format itself) plus
heuristic naming checks (PascalCase classes, k-prefixed constants,
m_-prefixed member variables) that clang-format doesn't enforce.

Covers src/, lib/, include/, tests/ and tools/ -- everything in this
repository written here. Two things are excluded because their bytes
belong to someone else: vendored third-party code (lib/vendor), where
reformatting would turn the next update from upstream into a merge
conflict, and include/bally_channels.h, which is one of three
byte-identical copies across the Bally repositories and is hash-checked
in all three (see EXCLUDED_FILES).

Best-effort: the naming checks are regex/line-based, not a real C++
parser, so they can both miss things and occasionally misfire on unusual
formatting. Function/parameter naming isn't checked at all -- too
failure-prone as a regex heuristic to be worth shipping half-working.

Run manually: `python scripts/check_style.py`
"""

from __future__ import annotations

import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _bootstrap import ensure_dependencies  # noqa: E402

ensure_dependencies([], Path(__file__).resolve().parent / ".venv-check-style")

REPO_ROOT = Path(__file__).resolve().parent.parent
SOURCE_DIRS = ["src", "lib", "include", "tests", "tools"]
SOURCE_SUFFIXES = {".cpp", ".h"}

# Third-party code copied in as-is. It is not ours to reformat or to hold to
# our naming rules, and reformatting it would make the next update from
# upstream a merge conflict instead of a copy. Matched against each file's
# path parts, so this covers anything under a directory of that name.
EXCLUDED_DIRS = {"vendor"}

# Files that exist here as an exact copy of something owned elsewhere, and
# whose bytes are the contract.
#
# bally_channels.h is one of THREE byte-identical copies -- bally_OS,
# bally_dongle and this repository -- each guarded by a SHA-256 committed
# beside it (tests/test_ballychannels.cpp). Reformatting it to this repo's
# .clang-format would break that hash in all three, and the failure a drift
# actually causes is silent: a device added to the fleet afterwards simply
# stops working. Whoever owns the file owns its formatting; we copy it.
EXCLUDED_FILES = {"bally_channels.h"}

CLASS_RE = re.compile(r"^\s*(?:class|struct)\s+([A-Za-z_]\w*)\s*(?:[:{]|$)")
CONST_RE = re.compile(r"\b(?:static\s+)?constexpr\s+[\w:<>*&]+\s+([A-Za-z_]\w*)\s*=")
ACCESS_RE = re.compile(r"^\s*(private|protected|public)\s*:\s*$")
MEMBER_RE = re.compile(r"^\s{4,}[\w:<>,]+[\s*&]+(\w+)(?:\s*=.*|\s*\[.*\])?;\s*$")


def iter_source_files():
    for source_dir in SOURCE_DIRS:
        base = REPO_ROOT / source_dir
        if not base.exists():
            continue
        for path in sorted(base.rglob("*")):
            if path.suffix not in SOURCE_SUFFIXES:
                continue
            if EXCLUDED_DIRS.intersection(path.relative_to(REPO_ROOT).parts):
                continue
            if path.name in EXCLUDED_FILES:
                continue
            yield path


def check_clang_format(files) -> list[str]:
    clang_format = shutil.which("clang-format")
    if not clang_format:
        print("warning: clang-format not found on PATH, skipping format check", file=sys.stderr)
        return []

    problems = []
    for path in files:
        result = subprocess.run(
            [clang_format, "--dry-run", "--Werror", str(path)],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            problems.append(f"{path.relative_to(REPO_ROOT)}: not clang-format clean (run `clang-format -i`)")
    return problems


def is_pascal_case(name: str) -> bool:
    return bool(re.fullmatch(r"[A-Z][A-Za-z0-9]*", name))


def is_k_constant(name: str) -> bool:
    return bool(re.fullmatch(r"k[A-Z][A-Za-z0-9]*", name))


def check_naming(path: Path) -> list[str]:
    problems = []
    lines = path.read_text(encoding="utf-8", errors="ignore").splitlines()
    rel = path.relative_to(REPO_ROOT)

    depth = 0
    class_body_depth = None  # brace depth of the class/struct body currently being scanned
    in_private_section = False

    for lineno, line in enumerate(lines, start=1):
        class_match = CLASS_RE.match(line)
        if class_match and "{" in line:
            name = class_match.group(1)
            if not is_pascal_case(name):
                problems.append(f"{rel}:{lineno}: class/struct '{name}' should be PascalCase")
            class_body_depth = depth + 1
            in_private_section = False

        const_match = CONST_RE.search(line)
        if const_match:
            name = const_match.group(1)
            if not is_k_constant(name):
                problems.append(f"{rel}:{lineno}: constant '{name}' should be k-prefixed (e.g. kFoo)")

        if class_body_depth is not None and depth == class_body_depth:
            access_match = ACCESS_RE.match(line)
            if access_match:
                in_private_section = access_match.group(1) in ("private", "protected")
            elif in_private_section:
                member_match = MEMBER_RE.match(line)
                if member_match:
                    name = member_match.group(1)
                    if not name.startswith("m_") and not is_k_constant(name):
                        problems.append(f"{rel}:{lineno}: member variable '{name}' should be m_-prefixed")

        depth += line.count("{") - line.count("}")
        if class_body_depth is not None and depth < class_body_depth:
            class_body_depth = None
            in_private_section = False

    return problems


def main() -> int:
    files = list(iter_source_files())
    if not files:
        print("No source files found.")
        return 0

    problems = check_clang_format(files)
    for path in files:
        problems.extend(check_naming(path))

    if not problems:
        print(f"check_style: {len(files)} files clean.")
        return 0

    print(f"check_style: {len(problems)} issue(s) found:\n")
    for problem in problems:
        print(f"  {problem}")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
