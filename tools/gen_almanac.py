#!/usr/bin/env python3
"""gen_almanac.py - writes src/almdata.c / .h: the coefficient tables of Almanac 47 as C doubles.

The numbers come from almanac47's python/native/c47data.py (the same numbers as NAVINIT_FULL: MATA,
MATST, MATM, MATP) and the mean elements (KEPLER) and star names. Every number is written with repr(), which
round-trips, so the C doubles are bit for bit the Python ones: NAV47 computes with exactly the same series.

    python3 tools/gen_almanac.py [path/to/almanac47]        (default: $ALMANAC47 or ../almanac47)
"""
import os
import re
import sys

root = sys.argv[1] if len(sys.argv) > 1 else os.environ.get('ALMANAC47', '../almanac47')
nat = os.path.join(root, 'python', 'native')
sys.path.insert(0, nat)
import c47data as D            # noqa: E402
import c47astro as A           # noqa: E402

here = os.path.dirname(os.path.abspath(__file__))
out_c = os.path.join(here, '..', 'src', 'almdata.c')
out_h = os.path.join(here, '..', 'src', 'almdata.h')

SERIES4 = ['VL', 'VB', 'VR', 'EEL', 'EEB', 'EER', 'VNL', 'VNB', 'VNR', 'MAL', 'MAB', 'MAR',
           'JUL', 'JUB', 'JUR', 'SAL', 'SAB', 'SAR']
TABLES = [('NU', 9), ('ST', 4), ('ML', 6), ('MB', 6), ('MCL', 6), ('MCB', 6)]


def num(x):
    r = repr(float(x))
    return r if ('e' in r or '.' in r or 'inf' in r or 'nan' in r) else r + '.0'


# star names: navdata.py (SN), the almanac order 1..58
nd = open(os.path.join(root, 'python', 'navdata.py')).read()
names = re.findall(r'"([^"]+)"', nd[nd.index('SN=('):])[:58]
assert len(names) == 58 and names[-1] == 'Polaris', names[-3:]

c = ['/* almdata.c - written by tools/gen_almanac.py from almanac47 python/native/c47data.py: do not edit.',
     ' * The coefficient tables of Almanac 47 (NAVINIT_FULL: MATA MATST MATM MATP), bit for bit.',
     ' * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later (see LICENSE). */',
     '#include "almdata.h"', '']
h = ['/* almdata.h - written by tools/gen_almanac.py: do not edit. */', '#ifndef NAV47_ALMDATA_H',
     '#define NAV47_ALMDATA_H', '', 'typedef struct { const double (*r)[4]; int n; } Series;', '']
total = 0
for name in SERIES4:
    rows = getattr(D, name)
    total += 4 * len(rows)
    c.append('static const double %s_d[%d][4] = {' % (name, len(rows)))
    for r in rows:
        c.append('  {%s},' % ', '.join(num(x) for x in r))
    c.append('};')
    c.append('const Series %s = { %s_d, %d };' % (name, name, len(rows)))
    h.append('extern const Series %s;' % name)
h.append('')
for name, cols in TABLES:
    rows = getattr(D, name)
    total += cols * len(rows)
    c.append('const double %s[%d][%d] = {' % (name, len(rows), cols))
    for r in rows:
        assert len(r) == cols
        c.append('  {%s},' % ', '.join(num(x) for x in r))
    c.append('};')
    h.append('#define %s_N %d' % (name, len(rows)))
    h.append('extern const double %s[%d][%d];' % (name, len(rows), cols))
# mean elements for the quick planets (PLN3)
c.append('const double KEPLER[5][2][6] = {')
for b in range(5):
    e0, r = A.KEPLER[b]
    c.append('  {{%s}, {%s}},' % (', '.join(num(x) for x in e0), ', '.join(num(x) for x in r)))
c.append('};')
h.append('extern const double KEPLER[5][2][6];')
c.append('const unsigned char BRIGHT[58] = {%s};' % ', '.join(str(x) for x in D.BRIGHT))
h.append('extern const unsigned char BRIGHT[58];      /* star numbers by brightness */')
c.append('const char *const STAR_NAME47[58] = {')
for i in range(0, 58, 6):
    c.append('  ' + ' '.join('"%s",' % D.STAR_NAME[n] for n in range(i + 1, min(i + 7, 59))))
c.append('};')
h.append('extern const char *const STAR_NAME47[58];   /* the names of the C47 views (upper case) */')
c.append('const char *const STAR_NAME[58] = {')
for i in range(0, 58, 6):
    c.append('  ' + ' '.join('"%s",' % n for n in names[i:i + 6]))
c.append('};')
h.append('extern const char *const STAR_NAME[58];')
h += ['', '#endif', '']
c.append('/* %d coefficients */' % total)
c.append('')
open(out_c, 'w').write('\n'.join(c))
open(out_h, 'w').write('\n'.join(h))
print('almdata: %d coefficients (%d bytes as double), %d stars' % (total, total * 8, len(names)))
