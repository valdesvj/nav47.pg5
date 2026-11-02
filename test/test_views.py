#!/usr/bin/env python3
"""test_views.py - NAV47's C47 views against Almanac 47's python/native/c47screen21.py, pixel for pixel:
ALMANAC, CHART, SKY (every frame of the name loop), SPLIT, ANIM (frames), ALLSKY; north and south latitudes,
day and night, 2000-2050.

    python3 test/test_views.py [path/to/almanac47] [cases]
"""
import os
import random
import subprocess
import sys

root = os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else os.environ.get('ALMANAC47', '../almanac47'))
ncases = int(sys.argv[2]) if len(sys.argv) > 2 else 12
sys.path.insert(0, os.path.join(root, 'python', 'native'))
sys.path.insert(0, os.path.join(root, 'python'))
import c47astro as A              # noqa: E402
import c47screen as S             # noqa: E402
import c47screen21 as V           # noqa: E402

top = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
exe = os.path.join(top, 'build', 'view_cli')
os.makedirs(os.path.dirname(exe), exist_ok=True)
subprocess.check_call(['gcc', '-std=gnu11', '-O2', '-ffp-contract=off', '-DNAV47_NO_LCD', '-Isrc', '-o', exe,
                       'test/view_cli.c', 'src/views.c', 'src/c47scr.c', 'src/fonts21.c', 'src/astro.c',
                       'src/almdata.c', '-lm'], cwd=top)

rng = random.Random(47)
cases = [(A.jd(2026, 9, 23, 23.5), 10.0, -75.5), (A.jd(2026, 9, 25, 18.5), 25.3333, 55.2),
         (A.jd(2026, 11, 2, 16.0), 26.5, 50.1667), (A.jd(2026, 6, 21, 2.0), -33.9, 151.2)]
for _ in range(ncases):
    cases.append((A.jd(rng.randint(2000, 2049), rng.randint(1, 12), rng.randint(1, 28), rng.uniform(0, 24)),
                  rng.uniform(-60, 60), rng.uniform(-180, 180)))

cmds, exp = [], []
for j, lat, lon in cases:
    al = S.Almanac(j, lat, lon)
    for name, fn in (('ALMANAC', V.almanac), ('CHART', V.chart), ('SPLIT', V.split), ('ALLSKY', V.allsky)):
        cmds.append('%s %r %r %r 0' % (name, j, lat, lon))
        exp.append((name, j, lat, lon, 0, fn(al)[0]))
    frames = V.sky_frames(al)
    for k, f in enumerate(frames):
        cmds.append('SKY %r %r %r %d' % (j, lat, lon, k))
        exp.append(('SKY', j, lat, lon, k, f, len(frames)))
    anim = V.anim(al)
    for k in (0, 5, 23):
        cmds.append('ANIM %r %r %r %d' % (j, lat, lon, k))
        exp.append(('ANIM', j, lat, lon, k, anim[k]))

out = subprocess.run([exe], input='\n'.join(cmds) + '\n', capture_output=True, text=True, check=True).stdout.split('\n')
bad = {}
tot = {}
worst = []
for e, line in zip(exp, out):
    f = line.split()
    nf = int(f[1])
    got = set(tuple(map(int, p.split(','))) for p in f[2:])
    want = e[5]
    diff = got ^ want
    name = e[0]
    tot[name] = tot.get(name, 0) + 1
    if name == 'SKY' and nf != e[6]:
        diff = diff | {('frames', nf, e[6])}
    if diff:
        bad[name] = bad.get(name, 0) + 1
        worst.append((len(diff), name, e[1], e[2], e[3], e[4], sorted(diff)[:6]))
for name in sorted(tot):
    print('%s %-8s %3d screens  %s' % ('OK ' if name not in bad else 'BAD', name, tot[name],
                                       'identical' if name not in bad else '%d differ' % bad[name]))
for w in sorted(worst, reverse=True)[:8]:
    print('  ', w)
print('%d screens: %s' % (len(exp), 'ALL PIXEL-IDENTICAL' if not bad else 'DIFFERENCES'))
sys.exit(1 if bad else 0)
