"""Independent check of test_route output.
Rhumb lines: mpmath at 40 digits (numerical quadrature for the meridian arc).
Geodesics:   Python geographiclib (separate implementation of Karney's method).
usage: python3 verify.py out.txt
"""
import sys, math
import mpmath as mp
from geographiclib.geodesic import Geodesic

mp.mp.dps = 40
ELL = [(6378137.0, 298.257223563), (6378137.0, 298.257222101),
       (6378388.0, 297.0), (6378206.4, 294.978698214),
       (6377397.155, 299.1528128), (6378245.0, 298.3),
       (6371008.7714, 0.0), (6378137.0, 298.257223563)]
CASES = [(40.64, -73.78, 51.47, -0.46), (-33.92, 18.42, -37.82, 144.96),
         (10.0, 20.0, 10.0, 60.0), (10.0, 20.0, 10.0000001, 60.0),
         (0.0, 0.0, 0.0, 179.0), (60.0, 5.0, 61.0, -170.0),
         (-89.0, 0.0, 89.0, 10.0), (25.0, 50.0, 26.5, 56.3)]
NM = 1852.0

def rhumb_ref(a, rf, lat1, lon1, lat2, lon2):
    a = mp.mpf(a); f = 1 / mp.mpf(rf) if rf else mp.mpf(0)
    e2 = f * (2 - f); e = mp.sqrt(e2)
    p1, p2 = mp.radians(lat1), mp.radians(lat2)
    psi = lambda p: mp.asinh(mp.tan(p)) - e * mp.atanh(e * mp.sin(p))
    rho = lambda p: a * (1 - e2) / (1 - e2 * mp.sin(p) ** 2) ** 1.5
    dM = mp.quad(rho, [p1, p2])
    dl = mp.radians(math.remainder(lon2 - lon1, 360.0))
    dpsi = psi(p2) - psi(p1)
    if dpsi == 0:
        s = a * mp.cos(p1) / mp.sqrt(1 - e2 * mp.sin(p1) ** 2) * abs(dl)
        az = mp.mpf(90) if dl > 0 else mp.mpf(-90)
    else:
        az = mp.degrees(mp.atan2(dl, dpsi))
        s = abs(dM / mp.cos(mp.radians(az)))
    return float(s), float(az)

rows = [l.strip().split(",") for l in open(sys.argv[1])]
ok = True
def check(name, cond, info=""):
    global ok
    if not cond: ok = False
    print(("PASS " if cond else "FAIL ") + name, info)

# 1. rhumb
worst_s = worst_a = worst_rt = 0
for r in rows:
    if r[0] != "RH": continue
    e, i = int(r[1]), int(r[2])
    s, az, la, lo = map(float, r[3:7])
    a, rf = ELL[e]
    sr, ar = rhumb_ref(a, rf, *CASES[i])
    worst_s = max(worst_s, abs(s - sr))
    worst_a = max(worst_a, abs(math.remainder(az - ar, 360)))
    lat2, lon2 = CASES[i][2], CASES[i][3]
    worst_rt = max(worst_rt, abs(la - lat2) * 111e3,
                   abs(math.remainder(lo - lon2, 360)) * 111e3 * math.cos(math.radians(lat2)))
check("rhumb distance vs mpmath (8 ellipsoids x 8 cases)", worst_s < 1e-6, f"max err {worst_s:.2e} m")
check("rhumb course vs mpmath", worst_a < 1e-9, f"max err {worst_a:.2e} deg")
check("rhumb direct(inverse) round trip", worst_rt < 1e-6, f"max err {worst_rt:.2e} m")

G = Geodesic.WGS84
inv = lambda p, q: G.Inverse(p[0], p[1], q[0], q[1])

# 2. composite
cw = [(float(r[2]), float(r[3]), float(r[4]), r[8], int(r[9])) for r in rows if r[0] == "CW"]
P1, P2 = (-33.92, 18.42), (-37.82, 144.96)
iv = [k for k, w in enumerate(cw) if w[4] & 4]
V1, V2 = cw[iv[0]][:2], cw[iv[1]][:2]
gA, gC = inv(P1, V1), inv(V2, P2)
check("composite: vertices on 45S", abs(V1[0] + 45) < 1e-12 and abs(V2[0] + 45) < 1e-12)
check("composite: leg A tangent to 45S (azi at V1 = 90)", abs(gA["azi2"] - 90) < 1e-7, f'{gA["azi2"]:.10f}')
check("composite: leg C tangent to 45S (azi at V2 = 90)", abs(gC["azi1"] - 90) < 1e-7, f'{gC["azi1"]:.10f}')
on_track = 0
for w in cw:
    if w[3] == "G" and not (w[4] & 4) and w[1] < V1[1]:
        on_track = max(on_track, abs(inv(P1, w)["s12"] + inv(w, V1)["s12"] - gA["s12"]))
    if w[3] == "G" and w[1] > V2[1] + 1e-9:
        on_track = max(on_track, abs(inv(V2, w)["s12"] + inv(w, P2)["s12"] - gC["s12"]))
check("composite: split points lie on the geodesic legs", on_track < 1e-6, f"{on_track:.2e} m")
mer = [w for w in cw if w[4] & 2]
check("composite: split points on whole 10-deg meridians",
      all(abs(w[1] / 10 - round(w[1] / 10)) < 1e-11 for w in mer))
a, f = G.a, G.f; e2 = f * (2 - f)
nu = a / math.sqrt(1 - e2 * math.sin(math.radians(45)) ** 2)
par = nu * math.cos(math.radians(45)) * math.radians(V2[1] - V1[1])
total = cw[-1][2 + 0] if False else float([r for r in rows if r[0] == "CW"][-1][5])
check("composite: total = A + parallel + C", abs(total - (gA["s12"] + par + gC["s12"])) < 1e-5,
      f"{total/NM:.2f} NM")
gc = inv(P1, P2)
rl_s, _ = rhumb_ref(a, 1 / f, *P1, *P2)
print(f"     geodesic {gc['s12']/NM:.1f} NM (vertex goes past 45S), composite {total/NM:.1f} NM, rhumb {rl_s/NM:.1f} NM")

# 3. distance split
dw = [(float(r[2]), float(r[3]), float(r[4])) for r in rows if r[0] == "DW"]
P1, P2 = (40.64, -73.78), (51.47, -0.46)
g12 = inv(P1, P2)["s12"]
legs_ok = all(abs(w[2] - 100 * NM) < 1e-6 for w in dw[1:-1])
on = max(abs(inv(P1, w)["s12"] + inv(w, P2)["s12"] - g12) for w in dw[1:-1])
check("100 NM split: equal legs and on the geodesic", legs_ok and on < 1e-6, f"{len(dw)} wpts, {on:.1e} m")

# 4. multi-leg
mw = [(float(r[2]), float(r[3]), float(r[4]), r[6]) for r in rows if r[0] == "MW"]
r1, _ = rhumb_ref(a, 1 / f, 35, 140, 40, 170)
g2 = inv((40, 170), (48, -125))["s12"]
check("multi-leg: rhumb leg length", abs(mw[1][2] - r1) < 1e-6)
check("multi-leg: 4 equal geodesic legs across 180", all(abs(w[2] - g2 / 4) < 1e-6 for w in mw[2:]),
      " ".join(f"{w[1]:.3f}" for w in mw))

pole = [r for r in rows if r[0] == "POLE"][0]
check("rhumb into the pole refused", pole[1] == "-1")
nm = [",".join(r[1:]) for r in rows if r[0] == "NMEA"]
def ck(sn):
    body, cs = sn[1:].split("*"); x = 0
    for c in body: x ^= ord(c)
    return f"{x:02X}" == cs
check("NMEA checksums and <= 82 chars", all(ck(s) and len(s) + 2 <= 82 for s in nm))
sys.exit(0 if ok else 1)
