#!/usr/bin/env python3
"""vessel_bmp.py - your vessel's pictures for NAV47, as BMP files for the calculator's /NAV47/ folder.

    python3 tools/vessel_bmp.py templates OUTDIR        # BOW.BMP and SHIP.BMP: NAV47's own drawings, to start from
    python3 tools/vessel_bmp.py bow PHOTO.jpg BOW.BMP [--top Y] [--bottom Y] [--left X] [--right X]
                                [--threshold 0..255] [--dither] [--height 150] [--png preview.png]
    python3 tools/vessel_bmp.py ship PICTURE.png SHIP.BMP [--length 64] [--threshold 128] [--png preview.png]

The files (any paint program can make or change them; NAV47 reads 1, 4, 8, 24 or 32-bit BMP, not compressed):
  /NAV47/BOW.BMP   the view over the bow from the bridge or the helideck, for BOW: up to 400 x 150 pixels, drawn at
                   the bottom in the middle (its top: the stem, a little below the horizon, 58 rows from the top).
  /NAV47/SHIP.BMP  the vessel from above, the bow UP, for the CHART: up to 64 x 160; turned to her heading, drawn at
                   her true length when the chart is that close, else at its own size (40 pixels at least).
Dark pixels are ink, light are paper. Light pixels joined to the picture's edge (the sky, the sea round her), and
pure magenta (255, 0, 255), are see-through: NAV47's sea, lane and platforms show there. On the calculator: FILES,
LOAD reads them again (or the next start). A photo of your ship stays yours: never commit it.
Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later.
"""
import argparse
import os
from PIL import Image, ImageDraw, ImageOps

W = 400


def save_bmp(img, path, png=None):
    img.convert("1").save(path, "BMP")
    if png:
        img.convert("1").convert("RGB").resize((img.width * 2, img.height * 2), Image.NEAREST).save(png)


def template_bow():
    """the drawn bow (src/gpsui.c bow_ship): the stem at row 0 of 90 rows"""
    im = Image.new("L", (W, 90), 255)
    d = ImageDraw.Draw(im)
    oy = 122
    P = lambda pts: [(x, y - oy) for x, y in pts]
    d.polygon(P([(112, 211), (112, 200), (172, 144), (200, 122), (228, 144), (288, 200), (288, 211)]), fill=255, outline=0)
    d.line(P([(200, 122), (200, 211)]), fill=0)
    for k in range(5):
        y = 146 + k * 12
        w = int((y - 122) * 1.04)
        d.line(P([(200 - w, y), (200 + w, y)]), fill=0)
    d.line(P([(120, 196), (174, 142), (200, 124)]), fill=0)
    d.line(P([(280, 196), (226, 142), (200, 124)]), fill=0)
    d.ellipse([196, 130 - oy, 204, 138 - oy], outline=0)
    d.rectangle([188, 176 - oy, 212, 187 - oy], outline=0)
    d.rectangle([60, 196 - oy, 339, 211 - oy], fill=0)
    for x in range(70, 330, 22):
        d.rectangle([x, 199 - oy, x + 15, 204 - oy], fill=255)
    return im


def template_ship():
    """from above, the bow up: 95 x 22 m as 14 x 60 px (a hull, the bridge forward, the helideck aft)"""
    w, h = 16, 62
    im = Image.new("L", (w, h), 255)
    d = ImageDraw.Draw(im)
    d.polygon([(8, 0), (14, 12), (14, 61), (1, 61), (1, 12)], fill=255, outline=0)
    d.rectangle([3, 13, 12, 18], fill=0)              # the bridge
    d.ellipse([3, 42, 12, 51], outline=0)             # the helideck
    d.point((8, 47), fill=0)
    return im


def main():
    a = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = a.add_subparsers(dest="cmd", required=True)
    t = sub.add_parser("templates"); t.add_argument("outdir")
    b = sub.add_parser("bow"); b.add_argument("photo"); b.add_argument("out")
    for k in ("--top", "--bottom", "--left", "--right"):
        b.add_argument(k, type=int)
    b.add_argument("--threshold", type=int, default=110); b.add_argument("--dither", action="store_true")
    b.add_argument("--height", type=int, default=150); b.add_argument("--png")
    s = sub.add_parser("ship"); s.add_argument("picture"); s.add_argument("out")
    s.add_argument("--length", type=int, default=64); s.add_argument("--threshold", type=int, default=128); s.add_argument("--png")
    o = a.parse_args()
    if o.cmd == "templates":
        os.makedirs(o.outdir, exist_ok=True)
        save_bmp(template_bow(), os.path.join(o.outdir, "BOW.BMP"))
        save_bmp(template_ship(), os.path.join(o.outdir, "SHIP.BMP"))
        print("BOW.BMP and SHIP.BMP in", o.outdir)
    elif o.cmd == "bow":
        im = ImageOps.exif_transpose(Image.open(o.photo)).convert("L")
        top = o.top if o.top is not None else int(im.height * 0.4)
        im = im.crop((o.left or 0, top, o.right or im.width, o.bottom or im.height))
        h = min(o.height, 150, round(W * im.height / im.width))
        im = ImageOps.autocontrast(im.resize((W, h), Image.LANCZOS), cutoff=1)
        im = im.convert("1") if o.dither else im.point(lambda v: 0 if v < o.threshold else 255)
        save_bmp(im, o.out, o.png)
        print("%s: %d x %d" % (o.out, W, h))
    else:
        im = ImageOps.exif_transpose(Image.open(o.picture)).convert("L")
        L = min(o.length, 160)
        w = max(4, min(64, round(im.width * L / im.height)))
        im = im.resize((w, L), Image.LANCZOS).point(lambda v: 0 if v < o.threshold else 255)
        save_bmp(im, o.out, o.png)
        print("%s: %d x %d (the bow up)" % (o.out, w, L))


if __name__ == "__main__":
    main()
