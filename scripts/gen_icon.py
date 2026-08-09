#!/usr/bin/env python3
"""Regenerates TraceView's app icon assets from the theme palette.

Draws a terminal-prompt mark (dark navy tile, soft blue glow, white ">_"
glyph) and exports it as a multi-resolution PNG set + a Windows .ico into
resources/icons/. Re-run this after changing the design below; there is no
separate source-of-truth image to keep in sync.
"""

from pathlib import Path

from PIL import Image, ImageColor, ImageDraw, ImageFilter

BACKGROUND = "#0A0F1E"
GLOW = "#3D8BFF"
GLYPH = "#F5F7FA"

SUPERSAMPLE = 1024
SIZES = [16, 32, 48, 64, 128, 256]

OUT_DIR = Path(__file__).resolve().parent.parent / "resources" / "icons"


def stamp_line(draw: ImageDraw.ImageDraw, p0, p1, width: float, fill, steps: int = 200) -> None:
    """Draws a straight segment with round caps by stamping overlapping
    circles along it -- Pillow's ImageDraw.line() only rounds the joints
    between segments, not the end caps of a single one."""
    r = width / 2
    for i in range(steps + 1):
        t = i / steps
        x = p0[0] + (p1[0] - p0[0]) * t
        y = p0[1] + (p1[1] - p0[1]) * t
        draw.ellipse([x - r, y - r, x + r, y + r], fill=fill)


def render(size: int) -> Image.Image:
    s = SUPERSAMPLE

    radius = int(s * 0.22)
    mask = Image.new("L", (s, s), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, s - 1, s - 1], radius=radius, fill=255)

    tile = Image.new("RGBA", (s, s), ImageColor.getrgb(BACKGROUND) + (255,))

    # Soft glow in the lower-right, echoing the depth of a terminal window
    # without needing a true multi-stop gradient fill.
    glow = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    glow_draw = ImageDraw.Draw(glow)
    gr = s * 0.55
    gc = (s * 0.80, s * 0.82)
    glow_draw.ellipse([gc[0] - gr, gc[1] - gr, gc[0] + gr, gc[1] + gr], fill=ImageColor.getrgb(GLOW) + (150,))
    glow = glow.filter(ImageFilter.GaussianBlur(s * 0.14))

    tile = Image.alpha_composite(tile, glow)
    tile.putalpha(mask)

    # The ">_" prompt glyph: a right-pointing chevron plus a trailing
    # underscore, read together as a command-line cursor.
    draw = ImageDraw.Draw(tile)
    stroke = s * 0.085
    top = (s * 0.30, s * 0.32)
    apex = (s * 0.56, s * 0.52)
    bottom = (s * 0.30, s * 0.72)
    stamp_line(draw, top, apex, stroke, GLYPH)
    stamp_line(draw, apex, bottom, stroke, GLYPH)
    stamp_line(draw, (s * 0.62, s * 0.72), (s * 0.82, s * 0.72), stroke, GLYPH)

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
