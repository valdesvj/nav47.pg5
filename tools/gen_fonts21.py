#!/usr/bin/env python3
"""gen_fonts21.py - writes src/fonts21.c: the fonts of Almanac 47's C47 views (python/native/c47fonts21.py):
F21 (the C47 standard font as GRFNT 21 draws it), TINY21 (tinyFont), SYMB (glyphs47 table symbols, 12 rows) and
SYMS (glyphs47 chart symbols, 7 rows). Column format, as AGRAPH draws: per glyph the advance, the y offset from the
base line and its columns (x, mask; bit 0 = the row y + offset, y upwards).

    python3 tools/gen_fonts21.py [path/to/almanac47]
"""
import os
import sys

root = os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else os.environ.get('ALMANAC47', '../almanac47'))
sys.path.insert(0, os.path.join(root, 'python', 'native'))
from c47fonts21 import F21, TINY, SYMB, SYMS   # noqa: E402

out = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src', 'fonts21.c')
L = ['/* fonts21.c - written by tools/gen_fonts21.py from Almanac 47 python/native/c47fonts21.py: do not edit.',
     ' * The C47 standard font (GRFNT 21), tinyFont and the glyphs47 symbols (C43/C47 authors and Almanac 47, GPL v3). */',
     '#include "c47scr.h"', '']
for name, F in (('F21', F21), ('TINY21', TINY), ('SYMB', SYMB), ('SYMS', SYMS)):
    cols, glyphs = [], []
    for c in range(128):
        g = F.get(c)
        if g is None:
            glyphs.append('{0,0,0,0}')
            continue
        adv, yoff, cl = g
        glyphs.append('{%d,%d,%d,%d}' % (adv, yoff, len(cl), len(cols)))
        cols += cl
    L.append('static const C47Col %s_c[%d] = {' % (name.lower(), max(1, len(cols))))
    for i in range(0, len(cols), 8):
        L.append('  ' + ' '.join('{%d,%du},' % (x, m) for x, m in cols[i:i + 8]))
    if not cols:
        L.append('  {0,0}')
    L.append('};')
    L.append('static const C47Glyph %s_g[128] = {' % name.lower())
    for i in range(0, 128, 8):
        L.append('  ' + ', '.join(glyphs[i:i + 8]) + ',')
    L.append('};')
    L.append('const C47Font %s = { %s_g, %s_c };' % (name, name.lower(), name.lower()))
    L.append('')
open(out, 'w').write('\n'.join(L))
print('fonts21.c written')
