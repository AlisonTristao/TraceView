#!/usr/bin/env python3
"""Vendors the Lucide icon set (https://lucide.dev, ISC) into
resources/icons/lucide/ as the pick-your-own-icon library behind
IconLibrary (lib/theme/iconlibrary.h).

Output is ONE JSON file rather than ~2000 loose SVGs: rcc then compresses a
single blob, and IconLibrary gets each icon's search tags in the same read.
Each entry keeps only the SVG's inner markup; IconLibrary re-wraps it in a
root carrying Lucide's shared attributes (see kSvgHeader there), which is
also where `currentColor` becomes a concrete color -- only the alpha shape
survives tinting anyway (see iconutils.cpp).

Usage:
    python tools/import_lucide.py            # pinned LUCIDE_VERSION below
    python tools/import_lucide.py 1.48.0     # bump to another release

Re-run after bumping, then commit resources/icons/lucide/. Icons already
chosen in saved projects are referenced by name ("lucide:<name>"), so a
rename or removal upstream just falls back to the default glyph.
"""

import io
import json
import re
import sys
import tarfile
import urllib.request
from pathlib import Path

LUCIDE_VERSION = "1.47.0"

ROOT = Path(__file__).resolve().parent.parent
OUT_DIR = ROOT / "resources" / "icons" / "lucide"

SVG_BODY = re.compile(r"<svg\b[^>]*>(.*)</svg>", re.S)


def fetch(version: str) -> tarfile.TarFile:
    url = f"https://registry.npmjs.org/lucide-static/-/lucide-static-{version}.tgz"
    print(f"Downloading {url}")
    with urllib.request.urlopen(url, timeout=120) as response:
        return tarfile.open(fileobj=io.BytesIO(response.read()), mode="r:gz")


def compact(body: str) -> str:
    # Collapse the pretty-printed markup: whitespace between tags and runs
    # inside attribute lists carry no meaning for these path-only glyphs.
    body = re.sub(r">\s+<", "><", body.strip())
    return re.sub(r"\s+", " ", body)


def main() -> None:
    version = sys.argv[1] if len(sys.argv) > 1 else LUCIDE_VERSION
    archive = fetch(version)

    def read(name: str) -> str:
        return archive.extractfile(f"package/{name}").read().decode("utf-8")

    tags = json.loads(read("tags.json"))
    icons = {}
    for member in archive.getmembers():
        match = re.fullmatch(r"package/icons/([a-z0-9-]+)\.svg", member.name)
        if not match:
            continue
        name = match.group(1)
        body = SVG_BODY.search(read(f"icons/{name}.svg"))
        if not body:
            print(f"  skipped {name}: no <svg> root")
            continue
        icons[name] = {"t": tags.get(name, []), "b": compact(body.group(1))}

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    payload = {"version": version, "icons": dict(sorted(icons.items()))}
    (OUT_DIR / "lucide.json").write_text(
        json.dumps(payload, ensure_ascii=False, separators=(",", ":")), encoding="utf-8"
    )
    (OUT_DIR / "LICENSE").write_text(read("LICENSE"), encoding="utf-8")
    print(f"Wrote {len(icons)} icons (lucide-static {version}) to {OUT_DIR}")


if __name__ == "__main__":
    main()
