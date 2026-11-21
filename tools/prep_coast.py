#!/usr/bin/env python3
"""prep_coast.py - build NAV47's COAST_x.BIN from GSHHG (.b) or Natural Earth
(GeoJSON) coastlines.

  python3 prep_coast.py gshhs_c.b      COAST_C.BIN [--tol 0.05] [--min-km2 5]
  python3 prep_coast.py ne_50m_land.geojson COAST_L.BIN --tol 0.02

File layout (little-endian, all angles in 0.01 deg units = SCALE):
  header 16 B : 'N47C', u16 version=1, u16 npoly, u16 scale=100,
                u16 flags (bit0 = delta-coded payload), u32 payload_offset
  directory   : npoly x 16 B
                i16 min_lat, max_lat, min_lon, max_lon   (lon may run past
                                                         +-180: unrolled)
                u32 offset (from start of file), u16 npts, u8 level, u8 flags
                flags bit0: crosses the antimeridian (lon unrolled > 180)
  payload     : per polygon, first vertex i16 lat, i16 lon, then either
                (npts-1) x (i16 lat, i16 lon)          [flags bit0 = 0]
                (npts-1) x (i8 dlat, i8 dlon), with escape 0x80 followed by
                           a full i16 pair when a step exceeds +-127 [bit0 = 1]
"""
import json, math, struct, sys, argparse

SCALE = 100


def read_gshhg(path, levels, min_km2):
    """GSHHG native binary: big-endian int32 header of 11 words, then n
    (lon, lat) int32 pairs in micro-degrees.  area is in 0.1 km^2."""
    polys = []
    data = open(path, 'rb').read()
    p = 0
    while p + 44 <= len(data):
        (pid, n, flag, west, east, south, north, area, area_full,
         container, ancestor) = struct.unpack_from('>11i', data, p)
        p += 44
        pts = struct.unpack_from('>%di' % (2 * n), data, p)
        p += 8 * n
        level = flag & 255
        if level not in levels or area / 10.0 < min_km2:
            continue
        ring = [(pts[2 * i + 1] * 1e-6, pts[2 * i] * 1e-6) for i in range(n)]
        polys.append((level, ring))
    return polys


def read_geojson(path):
    g = json.load(open(path))
    polys = []
    for f in g['features']:
        geom = f['geometry']
        parts = geom['coordinates'] if geom['type'] == 'MultiPolygon' else [geom['coordinates']]
        for poly in parts:
            for k, ring in enumerate(poly):
                polys.append((1 if k == 0 else 2, [(lat, lon) for lon, lat in ring]))
    return polys


def unroll(ring):
    """make longitudes continuous so a ring never jumps by 360"""
    out, prev = [], None
    for lat, lon in ring:
        if prev is not None:
            while lon - prev > 180: lon -= 360
            while lon - prev < -180: lon += 360
        out.append((lat, lon)); prev = lon
    lo = min(p[1] for p in out)
    if lo < -180:                       # keep min lon in [-180, 180)
        out = [(a, b + 360) for a, b in out]
    return out


def dp(pts, tol):
    """Douglas-Peucker in degree space (cos-lat scaled)"""
    if len(pts) < 3 or tol <= 0:
        return pts
    keep = [False] * len(pts); keep[0] = keep[-1] = True
    stack = [(0, len(pts) - 1)]
    while stack:
        a, b = stack.pop()
        (y1, x1), (y2, x2) = pts[a], pts[b]
        c = math.cos(math.radians((y1 + y2) / 2))
        dx, dy = (x2 - x1) * c, y2 - y1
        L = math.hypot(dx, dy)
        best, bi = -1, -1
        for i in range(a + 1, b):
            y, x = pts[i]
            px, py = (x - x1) * c, y - y1
            d = abs(dx * py - dy * px) / L if L else math.hypot(px, py)
            if d > best: best, bi = d, i
        if best > tol:
            keep[bi] = True
            stack += [(a, bi), (bi, b)]
    return [p for p, k in zip(pts, keep) if k]


def build(polys, tol, delta):
    recs = []
    for level, ring in polys:
        ring = dp(unroll(ring), tol)
        q = []
        for lat, lon in ring:
            v = (round(lat * SCALE), round(lon * SCALE))
            if not q or v != q[-1]:
                q.append(v)
        if len(q) < 3 or len(q) > 65535:
            continue
        recs.append((level, q))
    hdr_len, dir_len = 16, 16 * len(recs)
    payload, dirs = bytearray(), bytearray()
    off0 = hdr_len + dir_len
    for level, q in recs:
        lats = [a for a, _ in q]; lons = [b for _, b in q]
        flags = 1 if max(lons) > 180 * SCALE else 0
        off = off0 + len(payload)
        payload += struct.pack('<hh', *q[0])
        for (a0, b0), (a1, b1) in zip(q, q[1:]):
            da, db = a1 - a0, b1 - b0
            if delta and -127 <= da <= 127 and -127 <= db <= 127:
                payload += struct.pack('<bb', da, db)
            elif delta:
                payload += struct.pack('<Bxhh', 0x80, a1, b1)  # escape + pad
            else:
                payload += struct.pack('<hh', a1, b1)
        dirs += struct.pack('<hhhhIHBB', min(lats), max(lats), min(lons),
                            max(lons), off, len(q), level, flags)
    hdr = b'N47C' + struct.pack('<HHHHI', 1, len(recs), SCALE, 1 if delta else 0, off0)
    return hdr + bytes(dirs) + bytes(payload), recs


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('src'); ap.add_argument('out')
    ap.add_argument('--tol', type=float, default=0.05, help='simplify tolerance, deg')
    ap.add_argument('--min-km2', type=float, default=5.0)
    ap.add_argument('--levels', default='1,2')
    ap.add_argument('--raw', action='store_true', help='no delta coding')
    a = ap.parse_args()
    levels = {int(x) for x in a.levels.split(',')}
    polys = read_geojson(a.src) if a.src.endswith('json') else read_gshhg(a.src, levels, a.min_km2)
    blob, recs = build(polys, a.tol, not a.raw)
    open(a.out, 'wb').write(blob)
    npts = sum(len(q) for _, q in recs)
    print(f'{a.out}: {len(recs)} polygons, {npts} vertices, {len(blob)/1024:.1f} KB '
          f'(directory {16*len(recs)/1024:.1f} KB)')
