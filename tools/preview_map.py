#!/usr/bin/env python3
"""preview_map.py - desktop preview of the NAV47 400x240 chart page.
Decodes COAST_x.BIN exactly as map47.c will (directory scan + bbox cull),
projects on ellipsoidal Mercator, and overlays:
  composite/geodesic track (solid), rhumb line (dashed), plain geodesic (dotted)
usage: preview_map.py COAST.BIN route_out.txt out.png lat1 lon1 lat2 lon2
"""
import math, struct, sys
from PIL import Image, ImageDraw
from geographiclib.geodesic import Geodesic

W, H = 400, 240
G = Geodesic.WGS84
E = math.sqrt(G.f * (2 - G.f))

def psi(lat):
    p = math.radians(max(-85, min(85, lat)))
    return math.asinh(math.tan(p)) - E * math.atanh(E * math.sin(p))

def load(path):
    d = open(path, 'rb').read()
    assert d[:4] == b'N47C'
    ver, n, scale, flags, off0 = struct.unpack_from('<HHHHI', d, 4)
    polys = []
    for i in range(n):
        a, b, c, e, off, npts, lev, fl = struct.unpack_from('<hhhhIHBB', d, 16 + 16 * i)
        polys.append(((a / scale, b / scale, c / scale, e / scale), off, npts, lev))
    return d, scale, flags & 1, polys

def vertices(d, scale, delta, off, npts):
    la, lo = struct.unpack_from('<hh', d, off); off += 4
    yield la / scale, lo / scale
    for _ in range(npts - 1):
        if not delta:
            la, lo = struct.unpack_from('<hh', d, off); off += 4
        elif d[off] == 0x80:
            la, lo = struct.unpack_from('<hh', d, off + 2); off += 6
        else:
            da, db = struct.unpack_from('<bb', d, off); off += 2
            la += da; lo += db
        yield la / scale, lo / scale

def main():
    coast, route_txt, out = sys.argv[1:4]
    lat1, lon1, lat2, lon2 = map(float, sys.argv[4:8])
    wp = [(float(r.split(',')[2]), float(r.split(',')[3]), r.split(',')[8])
          for r in open(route_txt) if r.startswith('CW,')]
    # view: route bbox + margin, fitted to the 400x240 aspect on Mercator
    lats = [w[0] for w in wp] + [lat1, lat2]; lons = [w[1] for w in wp]
    lo0, lo1 = min(lons) - 8, max(lons) + 8
    y0, y1 = psi(min(lats) - 12), psi(max(lats) + 6)
    kx = W / math.radians(lo1 - lo0); ky = (H - 12) / (y1 - y0); k = min(kx, ky)
    cx, cy = (lo0 + lo1) / 2, (y0 + y1) / 2
    P = lambda la, lo: (W / 2 + k * math.radians(lo - cx), (H - 12) / 2 - k * (psi(la) - cy))
    img = Image.new('1', (W, H), 1); dr = ImageDraw.Draw(img)

    d, scale, delta, polys = load(coast)
    vlat0, vlat1 = min(lats) - 25, max(lats) + 15
    drawn = 0
    for (a, b, c, e), off, npts, lev in polys:
        for shift in (0, -360, 360):              # antimeridian copies
            if b < vlat0 or a > vlat1 or e + shift < lo0 - 5 or c + shift > lo1 + 5:
                continue
            pts = [P(la, lo + shift) for la, lo in vertices(d, scale, delta, off, npts)]
            dr.line(pts + [pts[0]], fill=0); drawn += 1
    # graticule every 10 deg, dotted
    for lo in range(int(lo0 // 10 * 10), int(lo1) + 1, 10):
        for y in range(0, H - 12, 4):
            x = P(0, lo)[0]
            if 0 <= x < W: dr.point((x, y), 0)
    for la in range(-80, 81, 10):
        y = P(la, 0)[1]
        if 0 <= y < H - 12:
            for x in range(0, W, 4): dr.point((x, y), 0)
    # plain geodesic, dotted
    l = G.InverseLine(lat1, lon1, lat2, lon2)
    for i in range(0, 400):
        p = l.Position(l.s13 * i / 399, Geodesic.STANDARD | Geodesic.LONG_UNROLL)
        if i % 3 == 0: dr.point(P(p['lat2'], p['lon2']), 0)
    # rhumb: straight on Mercator, dashed
    (xa, ya), (xb, yb) = P(lat1, lon1), P(lat2, lon2)
    for i in range(0, 60, 2):
        t0, t1 = i / 60, (i + 1) / 60
        dr.line([(xa + (xb - xa) * t0, ya + (yb - ya) * t0), (xa + (xb - xa) * t1, ya + (yb - ya) * t1)], fill=0)
    # planned composite track: densify each geodesic piece, parallel straight
    for (la0, lo0_, _), (la1, lo1_, kind) in zip(wp, wp[1:]):
        if kind == 'G':
            s = G.InverseLine(la0, lo0_, la1, lo1_)
            pts = [s.Position(s.s13 * j / 20, Geodesic.STANDARD | Geodesic.LONG_UNROLL) for j in range(21)]
            pts = [P(p['lat2'], p['lon2']) for p in pts]
        else:
            pts = [P(la0, lo0_), P(la1, lo1_)]
        dr.line(pts, fill=0, width=2)
    for la, lo, _ in wp:
        x, y = P(la, lo); dr.rectangle([x - 2, y - 2, x + 2, y + 2], outline=0)
    # soft-key bar mock
    dr.rectangle([0, H - 11, W - 1, H - 1], fill=0)
    for i, lab in enumerate(['GEOD', 'RHUMB', 'COMP', 'SPLIT', 'WPTS', 'MAP']):
        dr.text((i * 66 + 10, H - 11), lab, fill=1)
    img.save(out)
    big = img.convert('L').resize((W * 2, H * 2), Image.NEAREST)
    big.save(out.replace('.png', '_x2.png'))
    print(f'{drawn} polygons drawn (of {len(polys)})')

main()
