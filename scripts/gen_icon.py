#!/usr/bin/env python3
"""Regenerates TraceView's app icon assets from the theme palette.

Draws a live telemetry trace that fans out to two connected endpoints and
exports it as a multi-resolution PNG set + a Windows .ico into
resources/icons/. Re-run this after changing the design below; there is no
separate source-of-truth image to keep in sync.
"""

from math import ceil, hypot
from pathlib import Path

from PIL import Image, ImageColor, ImageDraw

BACKGROUND = "#0A0F1E"
BRANCH = "#3D8BFF"
TRACE = "#F5F7FA"

SUPERSAMPLE = 1024
SIZES = [16, 32, 48, 64, 128, 256]

OUT_DIR = Path(__file__).resolve().parent.parent / "resources" / "icons"


def cubic(p0, p1, p2, p3, steps: int = 80):
    """Samples a cubic Bezier in the icon's 256-unit design space."""
    points = []
    for i in range(steps + 1):
        t = i / steps
        u = 1.0 - t
        points.append(
            (
                u**3 * p0[0] + 3 * u**2 * t * p1[0] + 3 * u * t**2 * p2[0] + t**3 * p3[0],
                u**3 * p0[1] + 3 * u**2 * t * p1[1] + 3 * u * t**2 * p2[1] + t**3 * p3[1],
            )
        )
    return points


def round_path(draw: ImageDraw.ImageDraw, points, width: float, fill) -> None:
    """Draws a smooth path with round end caps at supersampled resolution."""
    scale = SUPERSAMPLE / 256.0
    scaled = [(x * scale, y * scale) for x, y in points]
    scaled_width = int(round(width * scale))
    radius = scaled_width / 2

    # Pillow's polyline joins produce tiny triangular gaps on densely sampled
    # Beziers. Overlapping circles give the exact same round stroke geometry
    # without those rasterization artefacts.
    for (x0, y0), (x1, y1) in zip(scaled, scaled[1:]):
        steps = max(1, ceil(hypot(x1 - x0, y1 - y0) / (radius * 0.5)))
        for i in range(steps):
            t = i / steps
            x = x0 + (x1 - x0) * t
            y = y0 + (y1 - y0) * t
            draw.ellipse([x - radius, y - radius, x + radius, y + radius], fill=fill)

    for x, y in (scaled[0], scaled[-1]):
        draw.ellipse([x - radius, y - radius, x + radius, y + radius], fill=fill)


def circle(draw: ImageDraw.ImageDraw, center, radius: float, fill) -> None:
    scale = SUPERSAMPLE / 256.0
    x, y = (center[0] * scale, center[1] * scale)
    r = radius * scale
    draw.ellipse([x - r, y - r, x + r, y + r], fill=fill)


def render(size: int) -> Image.Image:
    s = SUPERSAMPLE

    radius = int(s * 0.22)
    mask = Image.new("L", (s, s), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, s - 1, s - 1], radius=radius, fill=255)

    tile = Image.new("RGBA", (s, s), ImageColor.getrgb(BACKGROUND) + (255,))

    tile.putalpha(mask)

    # The two blue paths sit behind the white trace so the signal remains a
    # single uninterrupted stroke through the centre connection.
    draw = ImageDraw.Draw(tile)
    stroke = 17.0
    junction = (164.0, 124.0)
    upper_node = (216.0, 76.0)
    middle_node = (216.0, 124.0)
    lower_node = (216.0, 172.0)
    left_node = (32.0, 124.0)

    upper_branch = cubic(junction, (180.0, 124.0), (194.0, 76.0), upper_node)
    lower_branch = cubic(junction, (180.0, 124.0), (194.0, 172.0), lower_node)
    round_path(draw, upper_branch, stroke, BRANCH)
    round_path(draw, lower_branch, stroke, BRANCH)

    signal = [left_node, (68.0, 124.0)]
    signal += cubic((68.0, 124.0), (82.0, 124.0), (84.0, 111.0), (91.0, 86.0))[1:]
    signal += cubic((91.0, 86.0), (97.0, 63.0), (108.0, 64.0), (114.0, 90.0))[1:]
    signal += cubic((114.0, 90.0), (121.0, 121.0), (132.0, 176.0), (140.0, 166.0))[1:]
    signal += cubic((140.0, 166.0), (146.0, 159.0), (149.0, 128.0), junction)[1:]
    signal.append(middle_node)
    round_path(draw, signal, stroke, TRACE)

    # Identical endpoint markers keep every connection visually equivalent.
    for node in (left_node, upper_node, middle_node, lower_node):
        circle(draw, node, 15.0, TRACE)
        circle(draw, node, 8.5, BRANCH)

    return tile.resize((size, size), Image.LANCZOS)


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
