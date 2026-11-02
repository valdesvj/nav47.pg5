#!/usr/bin/env python3
"""gen_zones.py - writes src/zonedata.c from a Zones 47 checkout (valdesvj/zones47): the time-zone stops and
cities with their DST rules (python/zones.py, from IANA tzdata) and the world dot map (python/worldmap.py).

    python3 tools/gen_zones.py [path/to/zones47]          (default: $ZONES47 or ../zones47)
"""
import os
import sys

root = os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else os.environ.get('ZONES47', '../zones47'))
sys.path.insert(0, os.path.join(root, 'python'))
import zones as Z       # noqa: E402
import worldmap as W    # noqa: E402

out = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src', 'zonedata.c')


def cstr(s):
    return '"%s"' % (s or '').replace('"', '\\"')


L = ['/* zonedata.c - written by tools/gen_zones.py from zones47 python/zones.py and worldmap.py: do not edit.',
     ' * Time zones: the IANA tz database (https://www.iana.org/time-zones), tzdata %s, standard offsets of %d.' % (
         Z.TZDATA, Z.YEAR),
     ' * Land / sea of the dot map: global-land-mask (GLOBE, NOAA). GPL-3.0-or-later. */',
     '#include "zones.h"', '', 'const char *const TZDATA_VERSION = %s;' % cstr(Z.TZDATA)]
zl = []
ncity = 0
for zi, (off, col, cities) in enumerate(Z.ZONES):
    rows = []
    for name, cc, tzid, dst, rule in cities:
        if rule is None:
            kind, r = 'DST_NONE', '{0, 0, 0, 0}, {0, 0, 0, 0}'
        elif rule == 'irregular':
            kind, r = 'DST_IRREGULAR', '{0, 0, 0, 0}, {0, 0, 0, 0}'
        else:
            kind = 'DST_RULE'
            r = ', '.join('{%d, %d, %d, %r}' % (m, n, wd, float(h)) for m, n, wd, h in rule)
        rows.append('  {%s, %s, %s, %r, %s, %s},' % (cstr(name), cstr(cc), cstr(tzid), float(dst), kind, r))
        ncity += 1
    L.append('static const City z%d[%d] = {' % (zi, len(rows)))
    L += rows
    L.append('};')
    zl.append('  {%r, %d, %d, z%d},' % (float(off), col, len(rows), zi))
L.append('const Zone ZONES[%d] = {' % len(zl))
L += zl
L.append('};')
L.append('const int NZONES = %d;' % len(zl))
L.append('const int MAP_NCOL = %d, MAP_NROW = %d;' % (W.NCOL, W.NROW))
L.append('const unsigned long long MAP_COLS[%d] = {' % W.NCOL)
for i in range(0, W.NCOL, 6):
    L.append('  ' + ' '.join('0x%012xULL,' % c for c in W.COLS[i:i + 6]))
L.append('};')
L.append('')
open(out, 'w').write('\n'.join(L))
print('zonedata.c: %d stops, %d cities, map %dx%d, tzdata %s' % (len(zl), ncity, W.NCOL, W.NROW, Z.TZDATA))
