#!/usr/bin/env python3
"""Draws the Android app's icon (task 4.20; our own design, the same as the Windows icon:
a white line running into a square bump and a loop, on a dark background, pink rim).
Writes the launcher icons into android/app/src/main/res:
  mipmap-<density>/ic_launcher.png         the older launchers' icon (a rounded square)
  drawable-nodpi/ic_launcher_foreground.png the adaptive icon's line (432 px = 108 dp at xxxhdpi)
  drawable-nodpi/ic_launcher_monochrome.png the same line for themed icons
  mipmap-anydpi-v26/ic_launcher.xml        the adaptive icon (dark background, the line, monochrome)
  values/ic_launcher_colors.xml            the background colour

  make_android_icon.py [res_folder]
"""
import math
import pathlib
import sys

from PIL import Image, ImageDraw

res = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "android/app/src/main/res")
S = 1024
BG = (16, 16, 24, 255)
PINK = (242, 70, 143, 255)
WHITE = (240, 240, 248, 255)


def line(d, scale, off, w):
    """The course: in a box of size `scale` at `off` (x, y)."""
    def p(x, y):
        return (off[0] + x * scale, off[1] + y * scale)
    y = 0.62
    pts = [p(0.14, y), p(0.30, y), p(0.30, 0.38), p(0.47, 0.38), p(0.47, y), p(0.60, y)]
    r = 0.13
    cx, cy = 0.70, y - r
    loop = [p(cx + r * math.sin(t), cy + r * math.cos(t)) for t in [i / 48 * 2 * math.pi - math.pi * 0.25 for i in range(49)]]
    pts += [p(0.70 - r * 0.7, y)] + loop[1:] + [p(0.70 + r * 0.7, y), p(0.86, y)]
    d.line(pts, fill=WHITE, width=w, joint="curve")
    for q in (pts[0], pts[-1]):
        d.ellipse([q[0] - w / 2, q[1] - w / 2, q[0] + w / 2, q[1] + w / 2], fill=WHITE)


def legacy():
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    pad = S * 0.04
    d.rounded_rectangle([pad, pad, S - pad, S - pad], radius=S * 0.2, fill=BG, outline=PINK, width=int(S * 0.035))
    line(d, S, (0, 0), int(S * 0.055))
    return img


def foreground(rim):
    # 108 dp canvas; launchers show the middle 72 dp (a circle of 66 dp is always visible).
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    if rim:
        c, rr = S / 2, S * 33 / 108
        d.ellipse([c - rr, c - rr, c + rr, c + rr], outline=PINK, width=int(S * 0.028))
    inner = S * 54 / 108  # the line drawn inside the safe circle
    line(d, inner, ((S - inner) / 2, (S - inner) / 2 - inner * 0.0), int(S * 0.04))
    return img


densities = {"mdpi": 48, "hdpi": 72, "xhdpi": 96, "xxhdpi": 144, "xxxhdpi": 192}
big = legacy()
for name, px in densities.items():
    folder = res / f"mipmap-{name}"
    folder.mkdir(parents=True, exist_ok=True)
    big.resize((px, px), Image.LANCZOS).save(folder / "ic_launcher.png")
nodpi = res / "drawable-nodpi"
nodpi.mkdir(parents=True, exist_ok=True)
foreground(True).resize((432, 432), Image.LANCZOS).save(nodpi / "ic_launcher_foreground.png")
foreground(False).resize((432, 432), Image.LANCZOS).save(nodpi / "ic_launcher_monochrome.png")
(res / "mipmap-anydpi-v26").mkdir(parents=True, exist_ok=True)
(res / "mipmap-anydpi-v26" / "ic_launcher.xml").write_text(
    '<?xml version="1.0" encoding="utf-8"?>\n'
    '<!-- Made by tools/package/make_android_icon.py (our own design). -->\n'
    '<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">\n'
    '    <background android:drawable="@color/ic_launcher_background" />\n'
    '    <foreground android:drawable="@drawable/ic_launcher_foreground" />\n'
    '    <monochrome android:drawable="@drawable/ic_launcher_monochrome" />\n'
    '</adaptive-icon>\n')
(res / "values").mkdir(parents=True, exist_ok=True)
(res / "values" / "ic_launcher_colors.xml").write_text(
    '<?xml version="1.0" encoding="utf-8"?>\n<resources>\n    <color name="ic_launcher_background">#101018</color>\n</resources>\n')
print("wrote the Android icons into", res)
