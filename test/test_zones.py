#!/usr/bin/env python3
"""test_zones.py - NAV47's DST rules against Python's zoneinfo (IANA): every rule city half an hour before and
after each DST change of 2026, 2027 and 2030 (as Zones 47's test), plus random instants.

    python3 test/test_zones.py [path/to/zones47]
"""
import os
import random
import subprocess
import sys
from datetime import datetime, timedelta, timezone
from zoneinfo import ZoneInfo

root = os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else os.environ.get('ZONES47', '../zones47'))
sys.path.insert(0, os.path.join(root, 'python'))
sys.path.insert(0, os.path.join(root, 'tools'))
from zones import ZONES                    # noqa: E402
from make_zones import transitions         # noqa: E402

top = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
exe = os.path.join(top, 'build', 'zones_cli')
os.makedirs(os.path.dirname(exe), exist_ok=True)
subprocess.check_call(['gcc', '-std=gnu11', '-O2', '-Isrc', '-o', exe, 'test/zones_cli.c', 'src/zones.c',
                       'src/zonedata.c', '-lm'], cwd=top)


def jd(dt):
    return (dt - datetime(2000, 1, 1, 12)).total_seconds() / 86400 + 2451545.0


cases = []
rng = random.Random(47)
for zi, (off, col, cities) in enumerate(ZONES):
    for ci, (name, cc, tzid, dst, rule) in enumerate(cities):
        if not rule or rule == 'irregular':
            continue
        for y in (2026, 2027, 2030):
            for u, on in transitions(tzid, y):
                for dm in (-30, 30):
                    cases.append((zi, ci, tzid, (u + timedelta(minutes=dm)).replace(tzinfo=None)))
        for _ in range(20):
            cases.append((zi, ci, tzid, datetime(2026, 1, 1) + timedelta(hours=rng.uniform(0, 5 * 8760))))
inp = ''.join('%d %d %r\n' % (z, c, jd(u)) for z, c, _, u in cases)
out = subprocess.run([exe], input=inp, capture_output=True, text=True, check=True).stdout.split('\n')
bad = []
for (z, c, tzid, u), line in zip(cases, out):
    got = float(line.split()[0])
    want = u.replace(tzinfo=timezone.utc).astimezone(ZoneInfo(tzid)).utcoffset().total_seconds() / 3600
    if abs(got - want) > 1e-9:
        bad.append((tzid, u, got, want))
cities = len(set((z, c) for z, c, _, _ in cases))
print('%d rule cities, %d instants: %s' % (cities, len(cases), 'ALL AGREE with zoneinfo' if not bad else
                                           '%d differ' % len(bad)))
for b in bad[:10]:
    print('  ', b)
sys.exit(1 if bad else 0)
