#!/usr/bin/env python3
"""ticon_stations.py - TICON-3's tide gauges worldwide (CC BY 4.0) as NAV47 .TID files.

  python3 tools/ticon_stations.py TIDE47_DIR TICON_3.txt PLACES.geojson OUTDIR [--max N]      every area (make ticon)
  ... --list                          the areas and the regions, with their gauges (fast: nothing written)
  ... --areas GULF,REDSEA,EU          only these (codes of --list), a file (or a few) each
  ... --box S N W E NAME              your own rectangle (degrees, W and E -180..180; W > E crosses 180), repeatable
  ... --pick                          the list, then type the numbers or codes (make ticon-pick)
  ... --zones DIR                     ZONES47 for the time zones (default ~/opt/zones47)

TICON-3 (Hart-Davis, Dettmering, Seitz 2022, CC BY 4.0, doi:10.1594/PANGAEA.951610): the least-squares harmonic
constants of the GESLA-3 gauges (cm, Greenwich lags). Converted as Tide 47 converts its TICON-3 ports
(tools/make_stations.py, read only): the constituents NAV47 knows, SA's phase + the solar perigee (283.1 deg), and the
datum LAT: the lowest tide of the 19 years 2020-2038 (hourly, the lowest refined to the minute), so Z0 = the mean sea
level above LAT. Here the astronomical arguments (V, f) of each day are computed once and shared by every station.

The coastal and river gauges (GESLA calls many harbours "River"), not the lakes, with a tide of 2 cm or more; gauges within 1 km of each other: the one with the most observations.
Each named after the nearest place of Natural Earth's populated places (public domain): "Rotterdam NL 12km W".
The names in plain ASCII (the calculator's font): Male, not Malé.
TICON gives no time zone: the standard offset of the nearest place's country from ZONES47 (the IANA tz database,
~/opt/zones47 or --zones DIR; a country of several zones: the one nearest lon / 15); the US summer time in the US,
Canada and the Bahamas, standard time all year elsewhere. Without ZONES47: the nautical zone (ZD = lon / 15).
The files go by area, about 200 stations each (the DM42n loads a few at a time): OUTDIR/T<area><n>.TID."""
import json, math, os, sys, unicodedata
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import numpy as np
import gen_tides

TICON_NAMES = {'LM2': 'LAM2', 'MI2': 'MU2', 'NI2': 'NU2', 'EP2': 'EPS2', 'MKS': 'MKS2', 'MSQ': 'MSQM'}
P1_SA = 283.1
AREAS = [  # code, title, test(lat, lon)
    ('EU', 'EUROPE', lambda la, lo: 34 <= la < 72 and -32 <= lo < 45),
    ('AF', 'AFRICA, MIDDLE EAST, INDIA', lambda la, lo: -40 <= la < 34 and -32 <= lo < 80 or 34 <= la < 45 and 45 <= lo < 80),
    ('AS', 'ASIA', lambda la, lo: -12 <= la < 72 and 80 <= lo < 180),
    ('OC', 'OCEANIA', lambda la, lo: la < -12 and 80 <= lo < 180 or -12 <= la < 30 and 180 <= lo or la < 30 and lo < -120 and lo >= -180),
    ('NA', 'NORTH AMERICA', lambda la, lo: la >= 7 and -170 <= lo < -32),
    ('SA', 'SOUTH AMERICA', lambda la, lo: la < 7 and -120 <= lo < -32),
    ('XX', 'POLAR AND OTHER', lambda la, lo: True),
]
PER_FILE = 200
# smaller regions to choose (--areas, --pick): a gauge may be in two of them
def _med(la, lo): return 30 <= la < 46 and -6 <= lo < 37 or 40 <= la < 48 and 27 <= lo < 42
def _gulf(la, lo): return 23 <= la < 31 and 47 <= lo < 57
def _camw(la, lo):                                   # the Pacific side: Mexico, Guatemala to Costa Rica, Panama
    return (18 <= la < 33 and -118 <= lo < -100 or 13 <= la < 18 and -106 <= lo < -91
            or 9 <= la < 13 and -92 <= lo < -84.5 or 7 <= la < 9 and -84.5 <= lo < -77)
REGIONS = [
    ('GULF', 'ARABIAN GULF', _gulf),
    ('REDSEA', 'RED SEA, GULF OF ADEN', lambda la, lo: 12 <= la < 30 and 32 <= lo < 44 or 10 <= la < 16 and 42 <= lo < 52),
    ('ARAB', 'ARABIAN SEA: OMAN, PAKISTAN, W INDIA', lambda la, lo: 5 <= la < 27 and 52 <= lo < 78 and not _gulf(la, lo)),
    ('BENGAL', 'BAY OF BENGAL, E INDIA', lambda la, lo: 0 <= la < 24 and 78 <= lo < 100),
    ('EAFR', 'EAST AND SOUTH AFRICA', lambda la, lo: -40 <= la < 10 and 15 <= lo < 60 and not (lo < 25 and la > -30)),
    ('WAFR', 'WEST AFRICA', lambda la, lo: -30 <= la < 36 and -30 <= lo < 15 and not _med(la, lo)),
    ('MED', 'MEDITERRANEAN, BLACK SEA', _med),
    ('IBERIA', 'ATLANTIC IBERIA, BISCAY', lambda la, lo: 36 <= la < 48.5 and -15 <= lo < -1 and not _med(la, lo)),
    ('NWEUR', 'UK, IRELAND, CHANNEL, NORTH SEA', lambda la, lo: 48.5 <= la < 62 and -15 <= lo < 10),
    ('BALTIC', 'KATTEGAT, BALTIC', lambda la, lo: 53 <= la < 66 and 10 <= lo < 31),
    ('NORWAY', 'NORWAY, ICELAND, ARCTIC EUROPE', lambda la, lo: 62 <= la < 75 and -32 <= lo < 45),
    ('NAEAST', 'US AND CANADA ATLANTIC', lambda la, lo: 24 <= la < 62 and -82 <= lo < -50 and not (la < 31 and lo < -81 and not (la >= 29.5 and lo > -82))),
    ('CARIB', 'GULF OF MEXICO, CARIBBEAN', lambda la, lo: 7 <= la < 31 and -98 <= lo < -59 and not _camw(la, lo)),
    ('NAWEST', 'US AND CANADA PACIFIC', lambda la, lo: 30 <= la < 61 and -140 <= lo < -116),
    ('ALASKA', 'ALASKA, BERING', lambda la, lo: 50 <= la < 72 and (-180 <= lo < -140 or lo >= 170)),
    ('CAMW', 'MEXICO AND CENTRAL AMERICA PACIFIC', _camw),
    ('SAM', 'SOUTH AMERICA', lambda la, lo: -57 <= la < 7 and -92 <= lo < -32),
    ('EASIA', 'CHINA, KOREA, JAPAN, RUSSIA EAST', lambda la, lo: 20 <= la < 62 and 105 <= lo < 165),
    ('SEASIA', 'SOUTH-EAST ASIA', lambda la, lo: -12 <= la < 20 and 95 <= lo < 135),
    ('AUS', 'AUSTRALIA, PAPUA', lambda la, lo: -48 <= la < -12 and 105 <= lo < 160 or -12 <= la < -1 and 135 <= lo < 160),
    ('NZ', 'NEW ZEALAND', lambda la, lo: -53 <= la < -33 and 165 <= lo < 180),
    ('PACIF', 'PACIFIC ISLANDS, HAWAII', lambda la, lo: -30 <= la < 30 and (lo >= 160 or lo < -120)),
]


def box(s, n, w, e):
    """a rectangle: w > e crosses 180"""
    return lambda la, lo: s <= la < n and (w <= lo < e if w <= e else lo >= w or lo < e)


def ticon(fn):
    st = {}
    for ln in open(fn, encoding='latin-1'):
        p = ln.split()
        if len(p) < 14:
            continue
        key = (float(p[0]), float(p[1]), p[12])
        s = st.setdefault(key, {'hc': {}, 'nobs': int(float(p[8])), 'type': p[13]})
        s['hc'][TICON_NAMES.get(p[2], p[2])] = (float(p[3]) / 100, float(p[4]))
    return st


class Astro:
    """V + u and f of every constituent for every day 2020-2038 (from Tide 47's tide47ref.vector), once"""
    def __init__(self, R):
        self.names = [c[0] for c in R.CONS]
        self.w = np.radians(np.array(R.speeds()))
        days = np.arange(int(R.jd(2020, 1, 1)), int(R.jd(2039, 1, 1)) + 1) + 0.5
        V = np.empty((len(days), len(self.names))); F = np.empty_like(V)
        for i, d in enumerate(days):
            vf = R.vector(d)
            V[i] = [v for v, _ in vf]; F[i] = [f for _, f in vf]
        self.days, self.V, self.F, self.R = days, np.radians(V), F, R

    def lat(self, hc):
        """the mean sea level above LAT (m): minus the lowest tide of the 19 years"""
        H = np.array([hc.get(n, (0, 0))[0] for n in self.names]); G = np.radians([hc.get(n, (0, 0))[1] for n in self.names])
        k = H > 0
        H, G, w, V, F = H[k], G[k], self.w[k], self.V[:, k], self.F[:, k]
        t = np.arange(24.0)
        A = F * H                                                    # days x cons
        P = V - G                                                    # days x cons
        h = np.einsum('dc,dct->dt', A, np.cos(P[:, :, None] + w[None, :, None] * t[None, None, :]))
        idx = np.argsort(h.min(1))[:40]                              # the 40 lowest days, refined to the minute
        best = 0.0
        for i in idx:
            t0 = t[h[i].argmin()]
            tt = np.arange(t0 - 1, t0 + 1, 1 / 60)
            best = min(best, (A[i][:, None] * np.cos(P[i][:, None] + w[:, None] * tt[None, :])).sum(0).min())
        return -best


def places(fn):
    f = json.load(open(fn))['features']
    out = []
    for x in f:
        p = x['properties']; lo, la = x['geometry']['coordinates'][:2]
        out.append((math.radians(la), math.radians(lo), p.get('name') or '?', p.get('iso_a2') or ''))
    return out


def country_zones(d):
    """ZONES47's standard offsets by country code; {} without it"""
    try:
        sys.path.insert(0, os.path.join(os.path.expanduser(d), 'python'))
        import zones
    except ImportError:
        return {}
    out = {}
    for off, _col, cities in zones.ZONES:
        for c in cities:
            if c[1]:
                out.setdefault(c[1], set()).add(float(off))
    return out


def zone_of(lo, cc, Z):
    """the time zone (hours) and summer time ('US' or '') of a gauge"""
    zd = float(max(-12, min(12, round(lo / 15))))
    offs = Z.get(cc)
    if not offs:
        return zd, ''
    tz = min(offs, key=lambda o: (abs(o - lo / 15), o))
    return tz, 'US' if cc in ('US', 'CA', 'BS') and tz != -10 else ''   # the US rule: Canada and the Bahamas too


def ascii_name(s):
    return unicodedata.normalize('NFKD', s).encode('ascii', 'ignore').decode()


def name_of(la, lo, P):
    a, b = math.radians(la), math.radians(lo)
    best, bd = None, 9.0
    for pa, pb, nm, cc in P:
        d = math.acos(max(-1, min(1, math.sin(a) * math.sin(pa) + math.cos(a) * math.cos(pa) * math.cos(b - pb))))
        if d < bd:
            bd, best = d, (pa, pb, nm, cc)
    km = bd * 6371
    pa, pb, nm, cc = best
    if cc in ('-9', '-99'):
        cc = ''
    if km < 3:
        s = '%s %s' % (nm, cc)
    else:
        brg = math.degrees(math.atan2(math.sin(b - pb) * math.cos(a), math.cos(pa) * math.sin(a) - math.sin(pa) * math.cos(a) * math.cos(b - pb))) % 360
        s = '%s %s %dkm %s' % (nm, cc, round(km), ['N', 'NE', 'E', 'SE', 'S', 'SW', 'W', 'NW'][int((brg + 22.5) // 45) % 8])
    s = ' '.join(ascii_name(s).split())
    return (s if len(s) <= 30 else s[:29].rstrip() + '.'), cc


def main():
    a = sys.argv[1:]
    if len(a) < 4:
        print(__doc__); sys.exit(2)
    t47, tfile, pfile, out = a[:4]
    nmax = int(a[a.index('--max') + 1]) if '--max' in a else 10 ** 6
    R, py = gen_tides.load(t47)
    names = [c[0] for c in R.CONS]
    st = ticon(tfile)
    # the coastal and the river gauges (GESLA calls many harbours "River": Mina Salman), not the lakes; a tide at least
    # 2 cm (M2 or K1)
    coastal = [(k, v) for k, v in st.items() if not v['type'].lower().startswith('lake')
               and max(v['hc'].get('M2', (0, 0))[0], v['hc'].get('K1', (0, 0))[0]) >= 0.02]
    coastal.sort(key=lambda kv: -kv[1]['nobs'])                     # the longest records first, then dedupe
    keep = []
    for k, v in coastal:
        la, lo = k[0], (k[1] + 180) % 360 - 180
        if any(abs(la - q[0]) < 0.01 and abs(((lo - q[1]) + 180) % 360 - 180) < 0.01 / max(0.1, math.cos(math.radians(la))) for q, _ in keep):
            continue
        keep.append(((la, lo, k[2]), v))
    keep = keep[:nmax]
    print(len(st), 'TICON-3 stations,', len(coastal), 'coastal or river with a tide,', len(keep), 'after merging those within 1 km', file=sys.stderr)
    # the choice: every area (one each), or the regions chosen (a gauge in each that holds it)
    menu = [(c, t, f) for c, t, f in REGIONS] + [(c, t, f) for c, t, f in AREAS if c != 'XX']
    count = lambda f: sum(1 for (la, lo, _), _v in keep if f(la, lo))
    chosen = []
    if '--box' in a:
        for i, x in enumerate(a):
            if x == '--box':
                s_, n_, w_, e_ = (float(y) for y in a[i + 1:i + 5])
                nm = a[i + 5].upper()[:6] if i + 5 < len(a) and not a[i + 5].startswith('--') else 'BOX%d' % (len(chosen) + 1)
                chosen.append((nm, '%s %g..%g N %g..%g E' % (nm, s_, n_, w_, e_), box(s_, n_, w_, e_)))
    want = a[a.index('--areas') + 1] if '--areas' in a else ''
    if '--list' in a or '--pick' in a:
        print('\n  #  CODE    GAUGES  REGION')
        for i, (c, t, f) in enumerate(menu):
            if i == len(REGIONS):
                print('     (the whole areas, as make ticon writes them)')
            print('%3d  %-7s %5d   %s' % (i + 1, c, count(f), t))
        if '--list' in a:
            return
        want = input('\nthe regions (numbers or codes, a comma between; Enter: none): ').strip()
    for w in [x.strip().upper() for x in want.split(',') if x.strip()]:
        hit = [m for i, m in enumerate(menu) if w == m[0] or w == str(i + 1)]
        if not hit:
            sys.exit('no region %s (see --list)' % w)
        if hit[0] not in chosen:
            chosen.append(hit[0])
    if ('--areas' in a or '--pick' in a or '--box' in a) and not chosen:
        sys.exit('no region chosen')
    if chosen:
        kept = [k for k in keep if any(f(k[0][0], k[0][1]) for _c, _t, f in chosen)]
        print(len(kept), 'gauges in', ', '.join(c for c, _t, _f in chosen), file=sys.stderr)
        keep = kept
    astro = Astro(R)
    P = places(pfile)
    Z = country_zones(a[a.index('--zones') + 1] if '--zones' in a else '~/opt/zones47')
    print('time zones:', 'ZONES47, %d countries' % len(Z) if Z else 'none (no ZONES47): lon / 15', file=sys.stderr)
    by = {c[0]: [] for c in AREAS}
    for n, ((la, lo, src), v) in enumerate(keep):
        cons = v['hc']
        hc = {nm: (round(cons[nm][0], 4), round((cons[nm][1] + (P1_SA if nm == 'SA' else 0)) % 360, 2))
              for nm in names if nm in cons and cons[nm][0] > 0}
        if not hc or 'M2' not in hc and 'K1' not in hc:
            continue
        z0 = round(float(astro.lat(hc)), 3)
        nm, cc = name_of(la, lo, P)
        tz, dst = zone_of(lo, cc, Z)
        p = dict(name=nm, lat=round(la, 4), lon=round(lo, 4), tz=tz, dst=dst, datum='LAT', z0=z0,
                 src='TICON-3 %s' % src, hc=hc, kind='tide')
        if chosen:
            for code, title, test in chosen:
                if test(la, lo):
                    by.setdefault(code, []).append(p)
        else:
            for code, title, test in AREAS:
                if test(la, lo):
                    by[code].append(p); break
        if n % 250 == 0:
            print(n, '/', len(keep), p['name'], file=sys.stderr)
    os.makedirs(out, exist_ok=True)
    total = 0
    for code, title, test in chosen or AREAS:
        ps = sorted(by.get(code, []), key=lambda p: (-p['lat'], p['lon']))
        if not ps:
            continue
        k = math.ceil(len(ps) / PER_FILE)
        for i in range(k):
            part = ps[i * PER_FILE:(i + 1) * PER_FILE]
            fc = '%s%d' % (code, i + 1)
            tl = '%s %d/%d: TICON-3' % (title, i + 1, k) if k > 1 else '%s: TICON-3' % title
            fn = os.path.join(out, 'T%s.TID' % (fc if k > 1 or not chosen else code))
            open(fn, 'w').write('\n'.join(gen_tides.tid_lines(R, 'T' + fc, tl, part)) + '\n')
            print(fn, len(part), 'stations, %.1f..%.1f N' % (part[-1]['lat'], part[0]['lat']))
            total += len(part)
    print('total', total)


if __name__ == '__main__':
    main()
