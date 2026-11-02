#!/usr/bin/env python3
"""test_moon47.py - NAV47's MOON47 (src/moon47.c) against Almanac 47's python/moon47.py: lit, age, HP, SD,
the phase, the next four phases and the disc, 2000-2050, north and south views."""
import os, random, subprocess, sys
root = os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else '../almanac47')
sys.path.insert(0, os.path.join(root, 'python'))
import moon47 as M
top = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
exe = os.path.join(top, 'build', 'moon_cli')
subprocess.check_call(['gcc', '-std=gnu11', '-O2', '-o', exe, 'test/moon_cli.c', 'src/moon47.c', '-lm'], cwd=top)
rng = random.Random(47)
cases = [(M.julian(rng.randint(2000, 2049), rng.randint(1, 12), rng.randint(1, 28), rng.uniform(0, 24)), rng.randint(0, 1))
         for _ in range(400)]
out = subprocess.run([exe], input=''.join('%r %d\n' % c for c in cases), capture_output=True, text=True,
                     check=True).stdout.split('\n')
names = [n for _, n in M.PHASES]
worst, bad = 0.0, 0
for (j, s), line in zip(cases, out):
    f = line.split()
    p = M.page(j, bool(s))
    got = list(map(float, f[:4]))
    worst = max([worst] + [abs(a - b) for a, b in zip(got, (p['lit'], p['age'], p['hp'], p['sd']))])
    ok = int(f[4]) == p['index']
    for k, (t, n) in enumerate(p['next']):
        worst = max(worst, abs(float(f[5 + 2 * k]) - t))
        ok &= names[int(f[6 + 2 * k])] == n
    h = 0
    for dx in range(-40, 41):
        for a, b in M.disc_runs(40, p['age'], bool(s), dx):
            h = (h * 31 + a * 97 + b + dx) % 1000000007
    ok &= int(f[-1]) == h
    bad += not ok
print('MOON47: %d cases, max diff %.3g, %s' % (len(cases), worst, 'ALL AGREE' if not bad and worst < 1e-9 else '%d FAIL' % bad))
sys.exit(1 if bad or worst >= 1e-9 else 0)
