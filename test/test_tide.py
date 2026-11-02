#!/usr/bin/env python3
"""test_tide.py - NAV47's C tide engine against Tide 47's python/tide47ref.py.

1. every public station (tides and streams) and Tide 47's synthetic streams, many days: events, curve, stream now;
   with the stations compiled in (src/tidedata_pub.c) AND read back from a .TID file (the TIDE FILES menu);
2. FLOW with Tide 47's synthetic atlas (tests/synth_flow.py) through a .FLW file, Mina Salman as the reference.
Pass: times within 1E-9 h, values within 1E-9 (m or kn), sets within 1E-7 deg.

    python3 test/test_tide.py [path/to/tide47]
"""
import os
import random
import subprocess
import sys
import tempfile

t47 = os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else os.environ.get('TIDE47', '../tide47'))
sys.path.insert(0, os.path.join(t47, 'python'))
sys.path.insert(0, os.path.join(t47, 'tests'))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools'))
import tide47ref as R          # noqa: E402
import stations                # noqa: E402
import streams                 # noqa: E402
import gen_tides as G          # noqa: E402

top = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
exe = os.path.join(top, 'build', 'tide_cli')
os.makedirs(os.path.join(top, 'build', 'gen'), exist_ok=True)
priv = os.path.join(top, 'build', 'gen', 'tidedata_test_priv.c')
G.gen_regions(R, [], 'priv', priv)
subprocess.check_call(['gcc', '-std=gnu11', '-O2', '-ffp-contract=off', '-Isrc', '-o', exe, 'test/tide_cli.c',
                       'src/tide.c', 'src/tidecons.c', 'src/tidedata_pub.c', 'src/tidefile.c', priv, '-lm'], cwd=top)

regions = stations.REGIONS + streams.REGIONS
try:
    import synth_streams
    syn = [('SY', '', 'SYNTHETIC STREAMS', [synth_streams.rect_station(), synth_streams.rot_station()])]
except ImportError:
    syn = []
file_regions = regions + list(syn)
tmp = tempfile.mkdtemp()
tid = os.path.join(tmp, 'ALL.TID')
lines = []
for code, prog, title, ports in file_regions:
    lines += G.tid_lines(R, code, title, ports)
# one region per file in the calculator; here: one file, the parser keeps one region per file
with open(tid, 'w') as fh:
    fh.write('\n'.join(G.tid_lines(R, 'ALL', 'ALL', [p for _, _, _, ports in file_regions for p in ports])) + '\n')
flat = [p for _, _, _, ports in file_regions for p in ports]

flw = None
try:
    import synth_flow
    fl = synth_flow.atlas()
    flw = os.path.join(tmp, 'FLOW.FLW')
    open(flw, 'w').write('\n'.join(G.flow_lines(fl)) + '\n')
except ImportError:
    fl = None

rng = random.Random(47)
cmds, exp = [], []
for ri, (code, prog, title, ports) in enumerate(regions):
    for si, st in enumerate(ports):
        stream = st.get('kind') in ('rect', 'rot')
        for _ in range(6):
            n = R.jdn(rng.randint(2000, 2049), rng.randint(1, 12), rng.randint(1, 28))
            ref = R.sday(st, n) if stream else R.day(st, n)
            if not stream:
                ref = (ref[0], [(t, h, 0.0, k) for t, h, k in ref[1]], ref[2])
            fi = flat.index(st)
            for src, r, s in (('P', ri, si), ('F', 0, fi)):
                cmds.append('DAY %s %d %d %d %d' % (src, r, s, n, 1 if stream else 0))
                exp.append(('DAY', st['name'], ref))
            if stream:
                hr = rng.uniform(0, 24)
                cmds.append('NOW P %d %d %d %r' % (ri, si, n, hr))
                exp.append(('NOW', st['name'], R.stream_now(st, n, hr)))
for st in flat[sum(len(r[3]) for r in regions):]:          # synthetic streams: from the file only
    for _ in range(6):
        n = R.jdn(rng.randint(2000, 2049), rng.randint(1, 12), rng.randint(1, 28))
        cmds.append('DAY F 0 %d %d 1' % (flat.index(st), n))
        exp.append(('DAY', st['name'], R.sday(st, n)))
if fl:
    ref = [p for p in flat if fl['ref'].lower() in p['name'].lower()][0]
    for _ in range(40):
        la, lo = rng.uniform(26.0, 29.0), rng.uniform(48.75, 50.75)
        ladm = int(la) + (la - int(la)) * 0.6
        lodm = int(lo) + (lo - int(lo)) * 0.6
        wd, wk = rng.uniform(0, 360), rng.choice([0, 10, 25])
        n = R.jdn(rng.randint(2020, 2030), rng.randint(1, 12), rng.randint(1, 28))
        target = rng.uniform(0, 24)
        cmds.append('FLOW %r %r %r %r %d %r' % (ladm, lodm, wd, wk, n, target))
        cell = R.flow_cell(fl, R.dm(ladm), R.dm(lodm))
        if cell is None:
            exp.append(('FLOW', 'none', None))
            continue
        on, om = R.flow_pick(ref, n, target)
        t, h, rg, fac = R.flow_cycle(ref, on, om)
        rows = R.flow_rows(cell, fac, wd, wk)
        exp.append(('FLOW', 'cell', (on, om, t, h, rg, fac, rows, R.flow_step(ref, on, om, 1),
                                     R.flow_step(ref, on, om, -1))))

args = [exe, tid] + ([flw] if flw else [])
out = subprocess.run(args, input='\n'.join(cmds) + '\n', capture_output=True, text=True, check=True).stdout
out = out.split('\n')
worst = {'t': 0, 'v': 0, 'set': 0, 'curve': 0, 'now': 0, 'flow': 0}
fails = []
li = 0


def angd(a, b):
    d = abs(a - b) % 360
    return min(d, 360 - d)


for (kind, name, ref), cmd in zip(exp, cmds):
    if kind == 'DAY':
        tz = float(out[li].split()[1])
        ev = out[li + 1].split()[1:]
        cv = [float(x) for x in out[li + 2].split()[1:]]
        li += 3
        n = int(ev[0])
        got = [(float(ev[1 + 4 * i]), float(ev[2 + 4 * i]), float(ev[3 + 4 * i]), int(ev[4 + 4 * i])) for i in range(n)]
        if tz != ref[0] or len(got) != len(ref[1]) or any(a[3] != b[3] for a, b in zip(got, ref[1])):
            fails.append((name, cmd, 'events differ', got, ref[1]))
            continue
        for a, b in zip(got, ref[1]):
            worst['t'] = max(worst['t'], abs(a[0] - b[0]))
            worst['v'] = max(worst['v'], abs(a[1] - b[1]))
            worst['set'] = max(worst['set'], angd(a[2], b[2]))
        worst['curve'] = max([worst['curve']] + [abs(a - b) for a, b in zip(cv, ref[2])])
    elif kind == 'NOW':
        v, d = map(float, out[li].split()[1:])
        li += 1
        worst['now'] = max(worst['now'], abs(v - ref[0]), angd(d, ref[1]) * 1e-2)
    else:
        f = out[li].split()
        li += 1
        if ref is None:
            if f[1] != 'none':
                fails.append(('FLOW', cmd, 'expected none'))
            continue
        on, om, t, h, rg, fac, rows, nx, pv = ref
        if int(f[1]) != on or int(f[2]) != om:
            fails.append(('FLOW', cmd, 'pick differs', f[1:3], (on, om)))
            continue
        vals = [float(x) for x in f[3:7 + 26]]
        refv = [t, h, rg, fac] + [x for uv in rows for x in uv]
        worst['flow'] = max([worst['flow']] + [abs(a - b) for a, b in zip(vals, refv)])
        i = f.index('NEXT')
        if (int(f[i + 1]), int(f[i + 2])) != tuple(nx) or (int(f[i + 4]), int(f[i + 5])) != tuple(pv):
            fails.append(('FLOW', cmd, 'next/prev differ'))

lim = {'t': 1e-9, 'v': 1e-9, 'set': 1e-7, 'curve': 1e-9, 'now': 1e-9, 'flow': 1e-9}
for k, v in worst.items():
    print('%s %-5s max diff %.3g' % ('OK ' if v <= lim[k] else 'BAD', k, v))
    if v > lim[k]:
        fails.append((k, v))
for f in fails[:10]:
    print('FAIL', f)
print('%d stations (%d public + %d synthetic streams), %d checks, FLOW %s: %s' % (
    len(flat), len(flat) - len(syn and [p for r in syn for p in r[3]]), len(syn and [p for r in syn for p in r[3]]),
    len(cmds), 'synthetic atlas' if fl else 'skipped', 'ALL AGREE' if not fails else '%d FAIL' % len(fails)))
sys.exit(1 if fails else 0)
