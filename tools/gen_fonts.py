#!/usr/bin/env python3
"""gen_fonts.py - writes src/fonts.c: the C47 standardFont (ATEXT, 20-row box) and tinyFont (GRFNT 10, 8 rows)
as bitmap tables, from Almanac 47's python/stdfont.py and tinyfont.py (from the C43/C47 source, GPL v3).

ASCII 0x20-0x7E plus symbols on control codes, so NAV47's C strings stay plain:
  \\x01 degree   \\x02 prime (minutes)   \\x03 up arrow   \\x04 down arrow   \\x05 Sun   \\x06 right arrow
  \\x07 left arrow   \\x0e bullet   \\x0f plus-minus

    python3 tools/gen_fonts.py [path/to/almanac47]
"""
import os
import sys

root = os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else os.environ.get('ALMANAC47', '../almanac47'))
sys.path.insert(0, os.path.join(root, 'python'))
import stdfont      # noqa: E402
import tinyfont     # noqa: E402

out = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src', 'fonts.c')
SYM = {1: '°', 2: '′', 3: '↑', 4: '↓', 5: '⊙', 6: '→', 7: '←', 14: '•',
       15: '±'}
FALLBACK = {1: 'o', 2: "'", 3: '^', 4: 'v', 5: 'o', 6: '>', 7: '<', 14: '*', 15: '+'}


def build(F, name, box):
    L = []
    glyphs, rows = [], []
    for c in range(128):
        if 0x20 <= c <= 0x7e:
            g = F.get(c)
        elif c in SYM:
            g = F.get(stdfont.code(SYM[c])) or F.get(ord(FALLBACK[c]))
        else:
            g = None
        if g is None:
            glyphs.append((0, 0, 0, 0, 0, 0, 0))
            continue
        b, w, a, up, h, dn, bits = g
        glyphs.append((b, w, a, up, h, len(rows), 1))
        rows += bits
        assert w <= 16, (name, c, w)
    L.append('static const unsigned short %s_rows[%d] = {' % (name, len(rows)))
    for i in range(0, len(rows), 16):
        L.append('  ' + ' '.join('%d,' % r for r in rows[i:i + 16]))
    L.append('};')
    L.append('static const Glyph %s_g[128] = {' % name)
    for i in range(0, 128, 4):
        L.append('  ' + ' '.join('{%d,%d,%d,%d,%d,%d,%d},' % g for g in glyphs[i:i + 4]))
    L.append('};')
    L.append('const Font %s = { %d, %s_g, %s_rows };' % (name.upper(), box, name, name))
    return L


L = ['/* fonts.c - written by tools/gen_fonts.py from Almanac 47 python/stdfont.py and tinyfont.py: do not edit.',
     ' * The C47 standardFont and tinyFont (C43/C47 source, (c) the C43/C47 authors, GPL v3). */',
     '#include "gfx.h"', '']
L += build(stdfont.STD, 'std', 20)
L += build(tinyfont.TINY, 'tiny', 8)
L.append('')
open(out, 'w').write('\n'.join(L))
print('fonts.c written')
