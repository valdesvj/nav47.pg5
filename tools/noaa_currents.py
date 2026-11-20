#!/usr/bin/env python3
"""noaa_currents.py - NOAA CO-OPS tidal current stations (public domain) as NAV47 .TID files.

  python3 tools/noaa_currents.py TIDE47_DIR OUTDIR [--max N]

The harmonic current stations (type H) of NOAA's metadata API: for each, the harmonic constants of its default bin
(harcon.json). A station whose constituents have no minor axis is rectilinear (kind RECT: the speed along the flood
direction, NOAA's azimuth, knots); otherwise rotary (kind ROT: the east and the north components, from each
constituent's ellipse). The phases: NOAA's Greenwich phases (majorPhaseGMT) moved to the station's standard zone
(g = gG + speed x tz, as NOAA's own local phases). The mean current (majorMeanSpeed) is the mean, Z0 (or U0, V0).

The files go by area (US NORTHEAST, SOUTHEAST AND GULF, WEST, ALASKA, PACIFIC, CARIBBEAN), written by tools/gen_tides.py's
own writer, so the calculator's TIDE FILES menu and the web page read them as Tide 47's. The API answers are kept in
OUTDIR/cache/ (fetched once, politely). Subordinate stations (type S, offsets) are left out.
Constituent tables and speeds: Tide 47's python/tide47ref.py (read only)."""
import json, math, os, sys, time, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_tides

API = 'https://api.tidesandcurrents.noaa.gov/mdapi/prod/webapi'
ALIAS = {'RHO': 'RHO1', 'LAM2': 'LDA2', 'LAMBDA2': 'LDA2', 'NU2': 'NU2', 'MU2': 'MU2', 'SA': 'SA', 'SSA': 'SSA'}
AREAS = [  # code, title, test (lat, lon)
    ('NNE', 'US NORTHEAST STREAMS: NOAA', lambda la, lo: lo > -82 and la >= 36.5 and la < 50),
    ('NSE', 'US SOUTHEAST, GULF STREAMS: NOAA', lambda la, lo: lo > -100 and la >= 23.5 and la < 36.5),
    ('NWC', 'US WEST STREAMS: NOAA', lambda la, lo: -130 < lo <= -115 and 30 <= la < 50),
    ('NAK', 'ALASKA STREAMS: NOAA', lambda la, lo: la >= 50 or lo <= -130 and la >= 50),
    ('NCB', 'CARIBBEAN STREAMS: NOAA', lambda la, lo: lo > -90 and la < 23.5),
    ('NPA', 'PACIFIC STREAMS: NOAA', lambda la, lo: True),
]


def get(url, cache):
    if os.path.exists(cache):
        return json.load(open(cache))
    for k in range(4):
        try:
            req = urllib.request.Request(url, headers={'User-Agent': 'nav47-noaa-currents/1.0'})
            data = urllib.request.urlopen(req, timeout=60).read()
            open(cache, 'wb').write(data)
            time.sleep(0.25)
            return json.loads(data)
        except Exception as e:
            print('  retry', url, e, file=sys.stderr)
            time.sleep(2 + 3 * k)
    return None


def zone(st):
    """the station's standard zone (hours) and whether US summer time applies"""
    la, lo = st['lat'], st['lng']
    try:
        tz = float(st.get('timezone_offset') or 'x')
    except ValueError:
        tz = None
    if tz is None:
        tz = -10 if (la < 30 and lo < -150) else -9 if (la >= 50 and lo < -130) else -4 if (la < 23.5 and lo > -70) \
            else -5 if lo > -87 else -6 if lo > -102 else -7 if lo > -115 else -8
    if lo > 120 or lo < -170:            # Guam, the Marianas, American Samoa
        tz = 10 if lo > 120 else -11
    dst = 'US' if not (tz in (-10, -11, 10) or (la < 23.5 and lo > -70)) else '-'
    return tz, dst


def main():
    a = sys.argv[1:]
    if len(a) < 2:
        print(__doc__); sys.exit(2)
    t47, out = a[0], a[1]
    nmax = int(a[a.index('--max') + 1]) if '--max' in a else 10 ** 6
    R, py = gen_tides.load(t47)
    names = [c[0] for c in R.CONS]
    speed = dict(zip(names, R.speeds()))
    cache = os.path.join(out, 'cache')
    os.makedirs(cache, exist_ok=True)
    lst = get(API + '/stations.json?type=currentpredictions', os.path.join(cache, 'stations.json'))['stations']
    seen, sts = set(), []
    for s in lst:                                            # one entry a station: its default bin
        if s['type'] != 'H' or s['id'] in seen:
            continue
        seen.add(s['id']); sts.append(s)
    sts = sts[:nmax]
    by = {c[0]: [] for c in AREAS}
    skipped = 0
    for i, s in enumerate(sts):
        hc = get(API + '/stations/%s/harcon.json?units=english' % s['id'], os.path.join(cache, s['id'] + '.json'))
        cons = [c for c in (hc or {}).get('HarmonicConstituents', []) if c.get('binNbr') == s.get('currbin', 1)]
        if not cons:
            cons = [c for c in (hc or {}).get('HarmonicConstituents', []) if c.get('binNbr') == 1]
        if not cons:
            skipped += 1; continue
        tz, dst = zone(s)
        rot = any((c.get('minorAmplitude') or 0) > 0 for c in cons)
        azi = float(cons[0].get('azi') or 0)
        mj, mn = float(cons[0].get('majorMeanSpeed') or 0), float(cons[0].get('minorMeanSpeed') or 0)
        H, V = {}, {}
        for c in cons:
            n = c['constituentName'].upper()
            n = ALIAS.get(n, n)
            if n not in speed:
                continue
            loc = lambda g: (float(g) + speed[n] * tz) % 360
            A, g = float(c['majorAmplitude']), loc(c['majorPhaseGMT'])
            if not rot:
                if A > 0:
                    H[n] = (round(A, 4), round(g, 2))
                continue
            az = float(c.get('azi') or azi) * math.pi / 180
            B, h = float(c.get('minorAmplitude') or 0), loc(c.get('minorPhaseGMT') or 0)
            # u (east) = A sin az cos(x - g) + B cos az cos(x - h); v (north) = A cos az cos(x - g) - B sin az cos(x - h)
            for comp, (p, q), D in ((H, (math.sin(az), math.cos(az)), 'u'), (V, (math.cos(az), -math.sin(az)), 'v')):
                re = A * p * math.cos(math.radians(g)) + B * q * math.cos(math.radians(h))
                im = A * p * math.sin(math.radians(g)) + B * q * math.sin(math.radians(h))
                amp = math.hypot(re, im)
                if amp > 1e-4:
                    comp[n] = (round(amp, 4), round(math.degrees(math.atan2(im, re)) % 360, 2))
        if not H:
            skipped += 1; continue
        name = s['name'].strip()
        name = name if len(name) <= 30 else name[:29].rstrip(' ,(') + '.'
        p = dict(name=name, lat=float(s['lat']), lon=float(s['lng']), tz=tz, dst=dst, src='NOAA ' + s['id'], hv={})
        if rot:
            a0 = azi * math.pi / 180
            p.update(kind='rot', hc=H, hv=V, z0=round(mj * math.sin(a0) + mn * math.cos(a0), 3),
                     v0=round(mj * math.cos(a0) - mn * math.sin(a0), 3), flood=0, ebb=0, datum='ROTARY')
        else:
            fl = int(round(azi)) % 360
            p.update(kind='rect', hc=H, z0=round(mj, 3), v0=0.0, flood=fl, ebb=(fl + 180) % 360,
                     datum='FLOOD %03d° EBB %03d°' % (fl, (fl + 180) % 360))
        for code, title, test in AREAS:
            if test(p['lat'], p['lon']):
                by[code].append(p); break
        if i % 100 == 0:
            print(i, '/', len(sts), name, file=sys.stderr)
    for code, title, test in AREAS:
        ports = sorted(by[code], key=lambda p: (-p['lat'], p['lon']))
        if not ports:
            continue
        fn = os.path.join(out, code + '.TID')
        open(fn, 'w').write('\n'.join(gen_tides.tid_lines(R, code, title, ports)) + '\n')
        print(fn, len(ports), 'stations', sum(p['kind'] == 'rot' for p in ports), 'rotary')
    print('skipped (no constants):', skipped)


if __name__ == '__main__':
    main()
