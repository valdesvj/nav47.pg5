#!/usr/bin/env python3
"""test_astro.py - NAV47's C astronomy against Almanac 47's Python twin (python/native/c47astro.py).

Builds test/astro_cli.c with the host compiler, runs both on the same dates and places (2000-2050, random
latitudes and longitudes) and compares every output. Pass: all angles agree within 1E-9 degree (3.6E-6").

    python3 test/test_astro.py [path/to/almanac47] [cases]
"""
import os
import random
import subprocess
import sys

root = sys.argv[1] if len(sys.argv) > 1 else os.environ.get('ALMANAC47', '../almanac47')
ncases = int(sys.argv[2]) if len(sys.argv) > 2 else 300
sys.path.insert(0, os.path.join(root, 'python', 'native'))
import c47astro as A          # noqa: E402

here = os.path.dirname(os.path.abspath(__file__))
top = os.path.join(here, '..')
exe = os.path.join(top, 'build', 'astro_cli')
os.makedirs(os.path.dirname(exe), exist_ok=True)
subprocess.check_call(['gcc', '-std=gnu11', '-O2', '-ffp-contract=off', '-o', exe,
                       os.path.join(here, 'astro_cli.c'), os.path.join(top, 'src', 'astro.c'),
                       os.path.join(top, 'src', 'almdata.c'), '-lm'])

rng = random.Random(47)
cases = []
for _ in range(ncases):
    y = rng.randint(2000, 2049)
    j = A.jd(y, rng.randint(1, 12), rng.randint(1, 28), rng.uniform(0, 24))
    cases.append((j, rng.uniform(-70, 70), rng.uniform(-180, 180)))
cases.append((A.jd(2026, 11, 2, 12.0), 26.5, 50.2))           # Gulf, today
cases.append((A.jd(2000, 1, 1, 12.0), 0.0, 0.0))

inp = ''.join('%r %r %r\n' % c for c in cases)
out = subprocess.run([exe], input=inp, capture_output=True, text=True, check=True).stdout.split('\n')


def ang(a, b):
    d = abs(a - b) % 360.0
    return min(d, 360.0 - d)


worst = {}
li = 0
for j, lat, lon in cases:
    s = A.Sun(j)
    exp = {'SUN': (s.gha, s.dec, s.aries, s.sd, s.dpsi, s.deps)}
    exp['MOON'] = A.moon(s)
    for p in range(1, 5):
        exp['PL%d' % p] = A.planet(s, p)
        exp['PQ%d' % p] = A.planet_quick(s, p)
    exp['MQ'] = A.moon_quick(s)
    for n in range(1, 59):
        g, d, sha = A.star(s, n)
        exp['ST%d' % n] = (g, d, sha) + A.hcz(lat, lon, d, g)
    exp['EV'] = tuple(A.event(A.day0(j), lat, lon, k) for k in ('RISE', 'SET', 'NTWA', 'NTWP', 'CTWA', 'CTWP', 'TRAN'))
    exp['PH'] = A.phase(s)
    for _ in exp:
        f = out[li].split()
        li += 1
        key, got = f[0], [float(x) for x in f[1:]]
        e = exp[key]
        assert len(e) == len(got), key
        for i, (a, b) in enumerate(zip(e, got)):
            d = ang(a, b) if key not in ('PH',) else abs(a - b)
            k = key.rstrip('0123456789') if key.startswith('ST') else key
            if k not in worst or d > worst[k][0]:
                worst[k] = (d, j, i)

bad = 0
for k in sorted(worst):
    d = worst[k][0]
    flag = 'OK ' if d <= 1e-9 else 'BAD'
    bad += d > 1e-9
    print('%s %-5s max diff %.3g' % (flag, k, d))
print('%d cases x %d results: %s' % (len(cases), len(exp), 'ALL AGREE (<= 1E-9 deg)' if not bad else '%d FAIL' % bad))
sys.exit(1 if bad else 0)
