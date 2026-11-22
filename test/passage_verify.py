#!/usr/bin/env python3
"""NAV47 GPS: src/passage.c's cross-track (XTE), along-track (ATD), DTG and BRG against geographiclib.

Reads passage_test's X lines. Each leg (G geodesic, R rhumb line on the ellipsoid) is sampled finely, the nearest
sample to the point found and refined: its geodesic distance is |XTE|, its side the sign, its run along the leg ATD.
"""
import math
import sys
from geographiclib.geodesic import Geodesic

G = Geodesic.WGS84
NM = 1852.0
E2 = G.f * (2 - G.f)
E = math.sqrt(E2)


def psi(lat):
    p = math.radians(lat)
    return math.asinh(math.tan(p)) - E * math.atanh(E * math.sin(p))


def lat_of_psi(y):
    p = math.atan(math.sinh(y))
    for _ in range(20):
        p = math.atan(math.sinh(y + E * math.atanh(E * math.sin(p))))
    return math.degrees(p)


def leg_points(kind, a, b, n):
    """n + 1 points along the leg, with their distance from a (m)"""
    if kind == "G":
        line = G.InverseLine(a[0], a[1], b[0], b[1])
        return [(line.Position(line.s13 * k / n)["lat2"], line.Position(line.s13 * k / n)["lon2"],
                 line.s13 * k / n) for k in range(n + 1)]
    ya, yb = psi(a[0]), psi(b[0])
    dl = (b[1] - a[1] + 540) % 360 - 180
    pts, s, prev = [], 0.0, None
    for k in range(n + 1):
        t = k / n
        p = (lat_of_psi(ya + (yb - ya) * t), a[1] + dl * t)
        if prev:
            s += G.Inverse(prev[0], prev[1], p[0], p[1])["s12"]
        pts.append((p[0], p[1], s))
        prev = p
    return pts


def nearest(kind, a, b, p):
    pts = leg_points(kind, a, b, 2000)
    best = min(range(len(pts)), key=lambda i: G.Inverse(p[0], p[1], pts[i][0], pts[i][1])["s12"])
    lo, hi = max(0, best - 1), min(len(pts) - 1, best + 1)
    sub = []                                     # refine between the neighbours
    for i in range(lo, hi):
        q0, q1 = pts[i], pts[i + 1]
        for k in range(101):
            t = k / 100
            if kind == "G":
                line = G.InverseLine(q0[0], q0[1], q1[0], q1[1])
                r = line.Position(line.s13 * t)
                sub.append((r["lat2"], r["lon2"], q0[2] + line.s13 * t))
            else:
                y = psi(q0[0]) + (psi(q1[0]) - psi(q0[0])) * t
                dl = (q1[1] - q0[1] + 540) % 360 - 180
                sub.append((lat_of_psi(y), q0[1] + dl * t, q0[2] + (q1[2] - q0[2]) * t))
    q = min(sub, key=lambda r: G.Inverse(p[0], p[1], r[0], r[1])["s12"])
    inv = G.Inverse(q[0], q[1], p[0], p[1])
    # the side: the leg's direction at q (towards b; at b, from a) against the direction to p
    if kind == "G":
        far = G.Inverse(q[0], q[1], b[0], b[1])
        d = far["azi1"] if far["s12"] > 1 else G.Inverse(a[0], a[1], b[0], b[1])["azi2"]
    else:
        d = math.degrees(math.atan2(math.radians((b[1] - a[1] + 540) % 360 - 180), psi(b[0]) - psi(a[0])))
    side = math.sin(math.radians(inv["azi1"] - d))
    return math.copysign(inv["s12"], side), q[2], pts[-1][2]


def main():
    bad = n = 0
    for line in open(sys.argv[1]):
        if not line.startswith("X,"):
            continue
        f = line.strip().split(",")
        kind = f[1]
        a, b, p = (float(f[2]), float(f[3])), (float(f[4]), float(f[5])), (float(f[6]), float(f[7]))
        xte, atd, leg, dtg, brg = (float(x) for x in f[8:13])
        x, s, length = nearest(kind, a, b, p)
        inv = G.Inverse(p[0], p[1], b[0], b[1])
        # the tolerances: XTE 0.5 % of the distance off (+ 2 m), ATD 0.5 % (+ 0.02 nm), length 1 m, DTG / BRG exact
        inside = 1 < s < length - 1                # past an end: XTE is to the leg extended, the nearest point the end
        checks = [] if not inside or (kind == "R" and abs(x) > 30 * NM) else [("xte", xte * NM, x, 0.005 * abs(x) + 2)]
        checks += [
            ("leg", leg * NM, length, 1.0 + 1e-6 * length),
            ("dtg", dtg * NM, inv["s12"], 0.01),
            ("brg", brg, inv["azi1"] % 360, 1e-6),
        ]
        if inside and abs(x) < 30 * NM:
            checks.append(("atd", atd * NM, s, 0.005 * s + 0.02 * NM))
        for name, got, want, tol in checks:
            n += 1
            if abs(got - want) > tol:
                bad += 1
                print("FAIL %s %s: %.3f, not %.3f (tol %.3f): %s" % (kind, name, got, want, tol, line.strip()))
    print("passage verify: %d checks, %d failed" % (n, bad))
    sys.exit(1 if bad or n == 0 else 0)


if __name__ == "__main__":
    main()
