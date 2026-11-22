#!/usr/bin/env python3
"""Draws the Windows programs' icon (task 4.16 I; our own design, nothing from the game):
a white line on a dark rounded square, running into a square bump and then a loop, as
the course runs into obstacles; a pink rim. Writes apps/common/icon/vibribbon.ico (16 to
256 pixels) and vibribbon.png (256, for the window icon).

  make_icon.py <out_folder>
"""
import math
import pathlib
import sys

from PIL import Image, ImageDraw

out = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "apps/common/icon")
out.mkdir(parents=True, exist_ok=True)
S = 1024  # drawn large, then scaled down smoothly


def icon(size_hint):
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    pad = S * 0.04
    d.rounded_rectangle([pad, pad, S - pad, S - pad], radius=S * 0.2, fill=(16, 16, 24, 255), outline=(242, 70, 143, 255),
                        width=int(S * (0.05 if size_hint <= 32 else 0.035)))
    w = int(S * (0.075 if size_hint <= 32 else 0.055))  # thicker at small sizes so the line stays visible
    y = S * 0.62
    pts = [(S * 0.14, y), (S * 0.30, y), (S * 0.30, S * 0.38), (S * 0.47, S * 0.38), (S * 0.47, y), (S * 0.60, y)]
    # The loop: a circle sitting on the line, entered and left at its bottom.
    r = S * 0.13
    cx, cy = S * 0.70, y - r
    loop = [(cx + r * math.sin(t), cy + r * math.cos(t)) for t in [i / 48 * 2 * math.pi - math.pi * 0.25 for i in range(0, 49)]]
    pts += [(S * 0.70 - r * 0.7, y)] + loop[1:] + [(S * 0.70 + r * 0.7, y), (S * 0.86, y)]
    d.line(pts, fill=(240, 240, 248, 255), width=w, joint="curve")
    for p in (pts[0], pts[-1]):
        d.ellipse([p[0] - w / 2, p[1] - w / 2, p[0] + w / 2, p[1] + w / 2], fill=(240, 240, 248, 255))
    return img


sizes = [16, 24, 32, 48, 64, 128, 256]
images = [icon(s).resize((s, s), Image.LANCZOS) for s in sizes]
images[-1].save(out / "vibribbon.ico", sizes=[(s, s) for s in sizes], append_images=images[:-1])
images[-1].save(out / "vibribbon.png")
print("wrote", out / "vibribbon.ico", out / "vibribbon.png")
