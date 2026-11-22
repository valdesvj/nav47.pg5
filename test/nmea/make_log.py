#!/usr/bin/env python3
"""NAV47 GPS: a synthetic NMEA log for the tests and the simulator (made-up positions, no ship data).

The vessel sails legs at a speed through the water, with a current (set, drift) and the gyro's heading; the log has
RMC GGA GSA GSV VTG HDT VHW GST ZDA (talkers GP GL GA GB GN), a DGPS fix (station 412), a loss of the fix and a few
bad checksums. Positions on the WGS84 ellipsoid (geographiclib), one fix a second.

    make_log.py OUT [--minutes 12] [--start 26.30 50.30] [--legs 045:6,090:6] [--stw 10] [--set 135 --drift 1.2]
                    [--lost 300:90] [--date 2026-11-22T06:00:00]
"""
import argparse
import datetime as dt
import math
from geographiclib.geodesic import Geodesic

G = Geodesic.WGS84


def ck(body):
    x = 0
    for c in body:
        x ^= ord(c)
    return "$%s*%02X" % (body, x)


def dm(v, lat):
    h = ("N" if v >= 0 else "S") if lat else ("E" if v >= 0 else "W")
    v = abs(v)
    d = int(v)
    m = (v - d) * 60
    return ("%02d%07.4f" if lat else "%03d%07.4f") % (d, m), h


# made-up constellation: (talker, prn, el0, az0, snr, used)
SATS = [("GP", p, e, a, s, u) for p, e, a, s, u in [
    (2, 67, 40, 46, 1), (5, 22, 310, 38, 1), (7, 48, 120, 44, 1), (9, 15, 200, 33, 1), (13, 80, 280, 47, 1),
    (15, 34, 60, 41, 1), (18, 8, 165, 26, 0), (20, 55, 225, 43, 1), (29, 27, 345, 39, 1)]] + \
    [("GP", 44, 52, 95, 40, 0), ("GP", 51, 38, 250, 36, 0)] + \
    [("GL", p, e, a, s, u) for p, e, a, s, u in [
        (65, 30, 20, 37, 1), (66, 72, 100, 44, 1), (72, 45, 190, 41, 1), (73, 12, 260, 29, 0), (81, 58, 300, 42, 1),
        (88, 20, 140, 34, 0)]] + \
    [("GA", p, e, a, s, u) for p, e, a, s, u in [
        (3, 60, 75, 45, 1), (8, 25, 150, 39, 1), (11, 41, 215, 42, 1), (24, 18, 330, 33, 1), (26, 70, 10, 46, 1),
        (31, 9, 270, 25, 0), (33, 36, 50, 40, 1)]] + \
    [("GB", p, e, a, s, u) for p, e, a, s, u in [
        (6, 52, 130, 42, 1), (14, 33, 230, 38, 1), (23, 64, 290, 44, 1), (28, 14, 80, 30, 0), (37, 44, 180, 40, 1)]]
SYSID = {"GP": 1, "GL": 2, "GA": 3, "GB": 4}


def gsv(t):
    out = []
    for tk in ("GP", "GL", "GA", "GB"):
        ss = [s for s in SATS if s[0] == tk]
        n = (len(ss) + 3) // 4
        for i in range(n):
            body = "%sGSV,%d,%d,%02d" % (tk, n, i + 1, len(ss))
            for _, prn, e, a, snr, _u in ss[i * 4:i * 4 + 4]:
                el = max(0, min(90, round(e + 3 * math.sin(t / 600 + prn))))
                az = round(a + t / 60) % 360
                body += ",%02d,%02d,%03d,%02d" % (prn, el, az, max(0, snr + round(2 * math.sin(t / 7 + prn))))
            out.append(ck(body + ",1"))
    return out


def gsa(lost):
    out = []
    for tk in ("GP", "GL", "GA", "GB"):
        used = [] if lost else ["%02d" % s[1] for s in SATS if s[0] == tk and s[5]]
        used = (used + [""] * 12)[:12]
        out.append(ck("GNGSA,A,%d,%s,1.6,0.8,1.4,%d" % (1 if lost else 3, ",".join(used), SYSID[tk])))
    return out


def main():
    a = argparse.ArgumentParser()
    a.add_argument("out")
    a.add_argument("--minutes", type=float, default=12)
    a.add_argument("--start", type=float, nargs=2, default=[26.30, 50.30])
    a.add_argument("--legs", default="045:6,090:6", help="course:minutes,...")
    a.add_argument("--stw", type=float, default=10.0)
    a.add_argument("--set", type=float, default=135.0)
    a.add_argument("--drift", type=float, default=1.2)
    a.add_argument("--lost", default="300:90", help="start s:length s of the loss of the fix ('' none)")
    a.add_argument("--date", default="2026-11-22T06:00:00")
    a.add_argument("--rot", type=float, default=0, help="the rate of turn, degrees a minute (0: the new course at once)")
    o = a.parse_args()
    legs = [(float(c), float(m) * 60) for c, m in (x.split(":") for x in o.legs.split(","))]
    lost0, lostn = (int(x) for x in o.lost.split(":")) if o.lost else (-1, 0)
    t0 = dt.datetime.fromisoformat(o.date)
    lat, lon = o.start
    lines = []
    head = legs[0][0]
    for t in range(int(o.minutes * 60)):
        acc, hdg = 0, legs[-1][0]
        for c, d in legs:
            if t < acc + d:
                hdg = c
                break
            acc += d
        if o.rot > 0:                                   # the ship swings to the new course at the rate of turn
            dh = (hdg - head + 540) % 360 - 180
            head = (head + max(-o.rot / 60, min(o.rot / 60, dh))) % 360
            hdg = head
        # ground vector = water vector + current
        ve = o.stw * math.sin(math.radians(hdg)) + o.drift * math.sin(math.radians(o.set))
        vn = o.stw * math.cos(math.radians(hdg)) + o.drift * math.cos(math.radians(o.set))
        sog = math.hypot(ve, vn)
        cog = math.degrees(math.atan2(ve, vn)) % 360
        now = t0 + dt.timedelta(seconds=t)
        hhmmss = now.strftime("%H%M%S") + ".00"
        la, lh = dm(lat, True)
        lo, oh = dm(lon, False)
        lost = lost0 <= t < lost0 + lostn
        dgps = t < 420
        if lost:
            lines.append(ck("GPRMC,%s,V,,,,,,,%s,,,N" % (hhmmss, now.strftime("%d%m%y"))))
            lines.append(ck("GPGGA,%s,,,,,0,00,,,M,,M,," % hhmmss))
        else:
            lines.append(ck("GPRMC,%s,A,%s,%s,%s,%s,%.1f,%.1f,%s,2.1,E,%s" % (
                hhmmss, la, lh, lo, oh, sog, cog, now.strftime("%d%m%y"), "D" if dgps else "A")))
            lines.append(ck("GPGGA,%s,%s,%s,%s,%s,%d,%02d,0.8,12.4,M,-28.1,M,%s,%s" % (
                hhmmss, la, lh, lo, oh, 2 if dgps else 1, 24, "4.0" if dgps else "", "0412" if dgps else "")))
            lines.append(ck("GPVTG,%.1f,T,%.1f,M,%.1f,N,%.1f,K,%s" % (cog, (cog - 2.1) % 360, sog, sog * 1.852,
                                                                      "D" if dgps else "A")))
            if t % 5 == 0:
                lines.append(ck("GPGST,%s,1.2,1.8,1.1,35.0,%.1f,%.1f,2.5" % (hhmmss, 0.6 if dgps else 1.7,
                                                                            0.5 if dgps else 1.5)))
        lines.append(ck("HEHDT,%.1f,T" % hdg))
        lines.append(ck("VWVHW,%.1f,T,%.1f,M,%.1f,N,%.1f,K" % (hdg, (hdg - 2.1) % 360, o.stw, o.stw * 1.852)))
        if t % 5 == 0:
            lines += gsa(lost)
            lines += gsv(t)
        if t % 60 == 0:
            lines.append(ck("GPZDA,%s,%02d,%02d,%04d,00,00" % (hhmmss, now.day, now.month, now.year)))
        if t in (17, 133, 401):
            lines.append(ck("GPRMC,%s,A,%s,%s,%s,%s,%.1f,%.1f,%s,2.1,E,A" % (
                hhmmss, la, lh, lo, oh, sog, cog, now.strftime("%d%m%y")))[:-2] + "00")   # a bad checksum
        lines.append("")                                # a blank line: the end of the second (for replay)
        g = G.Direct(lat, lon, cog, sog * 1852 / 3600)
        lat, lon = g["lat2"], g["lon2"]
    with open(o.out, "w") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
