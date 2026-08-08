#!/usr/bin/env python3
"""Regenerates TraceView's app icon assets from the theme palette.

Draws a simple mark (dark navy tile, white winding line, blue accent dot)
and exports it as a multi-resolution PNG set + a Windows .ico into
resources/icons/. Re-run this after changing the design below; there is no
separate source-of-truth image to keep in sync.
"""

from pathlib import Path

from PIL import Image, ImageDraw

BACKGROUND = "#0A0F1E"
LINE = "#FFFFFF"
ACCENT = "#3D8BFF"

SUPERSAMPLE = 1024
SIZES = [16, 32, 48, 64, 128, 256]

OUT_DIR = Path(__file__).resolve().parent.parent / "resources" / "icons"


def bezier_points(p0, p1, p2, p3, steps=300):
    points = []
    for i in range(steps + 1):
        t = i / steps
        mt = 1 - t
        x = (mt**3) * p0[0] + 3 * (mt**2) * t * p1[0] + 3 * mt * (t**2) * p2[0] + (t**3) * p3[0]
        y = (mt**3) * p0[1] + 3 * (mt**2) * t * p1[1] + 3 * mt * (t**2) * p2[1] + (t**3) * p3[1]
        points.append((x, y))
    return points


def render(size: int) -> Image.Image:
    scale = SUPERSAMPLE / size
    img = Image.new("RGBA", (SUPERSAMPLE, SUPERSAMPLE), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    radius = int(SUPERSAMPLE * 0.22)
    draw.rounded_rectangle([0, 0, SUPERSAMPLE - 1, SUPERSAMPLE - 1], radius=radius, fill=BACKGROUND)

    pad = SUPERSAMPLE * 0.22
    p0 = (pad, SUPERSAMPLE - pad)
    p1 = (pad, SUPERSAMPLE * 0.35)
    p2 = (SUPERSAMPLE - pad, SUPERSAMPLE * 0.65)
    p3 = (SUPERSAMPLE - pad, pad)
    points = bezier_points(p0, p1, p2, p3)

    line_width = max(2, int(SUPERSAMPLE * 0.09))
    r = line_width / 2
    for x, y in points:
        draw.ellipse([x - r, y - r, x + r, y + r], fill=LINE)

    dot_r = SUPERSAMPLE * 0.09
    cx, cy = p3
    ring_r = dot_r + SUPERSAMPLE * 0.018
    draw.ellipse([cx - ring_r, cy - ring_r, cx + ring_r, cy + ring_r], fill=BACKGROUND)
    draw.ellipse([cx - dot_r, cy - dot_r, cx + dot_r, cy + dot_r], fill=ACCENT)

    return img.resize((size, size), Image.LANCZOS)


def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    images = {size: render(size) for size in SIZES}
    for size, img in images.items():
        img.save(OUT_DIR / f"app_{size}.png")

    largest = images[max(SIZES)]
    largest.save(
        OUT_DIR / "app.ico",
        sizes=[(s, s) for s in SIZES],
    )
    print(f"Wrote {len(SIZES)} PNGs and app.ico to {OUT_DIR}")


if __name__ == "__main__":
    main()
