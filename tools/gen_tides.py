#!/usr/bin/env python3
"""gen_tides.py - the tide data of NAV47 from a Tide 47 checkout (valdesvj/tide47).

  python3 tools/gen_tides.py cons    TIDE47_DIR               -> src/tidecons.c   (constituents, nodal constants)
  python3 tools/gen_tides.py public  TIDE47_DIR               -> src/tidedata_pub.c (NOAA + TICON-3 stations, streams)
  python3 tools/gen_tides.py private TIDE47_DIR OUT.c         -> the PRIVATE data compiled into YOUR .pg5 only:
        python/stations_private.py, streams_private.py (Aramco, Admiralty, GEOSA) and flow_private.py (the atlas).
        Missing files give empty tables. OUT.c goes to build/ (git-ignored): never commit it.
  python3 tools/gen_tides.py files   TIDE47_DIR OUTDIR [--private-only]
        -> calculator files for the TIDE FILES menu: OUTDIR/<REGION>.TID (stations) and OUTDIR/FLOW.FLW (atlas).
           Copy them to /NAV47/TIDES/ on the DM42n disk.

The engine constants (Doodson numbers, u and f formulas, Schureman's constants) come from python/tide47ref.py so
NAV47 computes exactly as Tide 47. Numbers are written with repr() (bit for bit).
"""
import importlib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TOP = os.path.join(HERE, '..')


def load(t47):
    py = os.path.join(t47, 'python')
    sys.path.insert(0, py)
    R = importlib.import_module('tide47ref')
    return R, py


def num(x):
    r = repr(float(x))
    return r if ('e' in r or '.' in r) else r + '.0'


def cstr(s):
    return '"%s"' % s.replace('\\', '\\\\').replace('"', '\\"')


def gen_cons(R, out):
    names = [c[0] for c in R.CONS]
    L = ['/* tidecons.c - written by tools/gen_tides.py from tide47 python/tide47ref.py: do not edit.',
         ' * The constituents of Tide 47 (Schureman / NOAA): Doodson numbers, phase k x 90, u and f formulas.',
         ' * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */', '#include "tide.h"', '',
         'const char *const CON_NAME[NCONS] = {']
    for i in range(0, len(names), 10):
        L.append('  ' + ' '.join(cstr(n) + ',' for n in names[i:i + 10]))
    L.append('};')
    L.append('const signed char CON_DOOD[NCONS][6] = {')
    for c in R.CONS:
        L.append('  {%s},' % ', '.join(str(x) for x in c[1]))
    L.append('};')
    L.append('const signed char CON_K90[NCONS] = {%s};' % ', '.join(str(c[2]) for c in R.CONS))
    L.append('/* u = sum UC x basics (%s) */' % ' '.join(R.UB))
    L.append('const double CON_U[NCONS][%d] = {' % len(R.UB))
    for c in R.CONS:
        L.append('  {%s},' % ', '.join(num(c[3].get(b, 0)) for b in R.UB))
    L.append('};')
    L.append('/* log f = sum FC x log(basic factors %s) */' % ' '.join(R.FB))
    L.append('const double CON_F[NCONS][%d] = {' % len(R.FB))
    for c in R.CONS:
        L.append('  {%s},' % ', '.join(num(c[4].get(b, 0)) for b in R.FB))
    L.append('};')
    L.append('typedef char ncons_matches_tide_h[(NCONS == %d) ? 1 : -1];' % len(names))
    L.append('const double TIDE_SPEED[6] = {%s};' % ', '.join(num(x) for x in R.SPEED))
    for k in ('CICW', 'SISW', 'K1', 'K2', 'DMM', 'DMF', 'DO1', 'DJ1', 'DOO1', 'DM2', 'DK1', 'DK2'):
        L.append('const double TK_%s = %s;' % (k, num(R.K[k])))
    L.append('')
    open(out, 'w').write('\n'.join(L))
    return len(names)


def gen_regions(R, regions, prefix, out, flow=None, header=''):
    L = ['/* %s - written by tools/gen_tides.py: do not edit. %s */' % (os.path.basename(out), header),
         '#include "tide.h"', '']
    regs = []
    for ri, (code, prog, title, ports) in enumerate(regions):
        inits = []
        for pi, st in enumerate(ports):
            tag = '%s_r%d_p%d' % (prefix, ri, pi)
            idx = {c[0]: i for i, c in enumerate(R.CONS)}
            kind = {'rect': 'TK_RECT', 'rot': 'TK_ROT'}.get(st.get('kind'), 'TK_TIDE')
            lists = {}
            for key in ('hc', 'hv'):
                d = st.get(key) or {}
                rows = sorted((idx[n], H, g) for n, (H, g) in d.items() if n in idx)
                missing = [n for n in d if n not in idx]
                assert not missing, (st['name'], missing)
                if rows:
                    L.append('static const TCon %s_%s[%d] = {%s};' % (tag, key, len(rows),
                             ', '.join('{%d, %s, %s}' % (i, num(H), num(g)) for i, H, g in rows)))
                    lists[key] = ('%s_%s' % (tag, key), len(rows))
                else:
                    lists[key] = ('0', 0)
            inits.append('  {%s, %s, %s, %s, %s, %s, %d, %s, %s, %s, %s, %s, %s, %s, %d, %d},' % (
                cstr(st['name']), cstr(st.get('datum', '')), cstr(st.get('src', '')), num(st['lat']),
                num(st['lon']), num(st['tz']), 1 if st.get('dst') == 'US' else 0, kind, num(st.get('z0', 0.0)),
                num(st.get('v0', 0.0)), num(st.get('flood', 0.0)), num(st.get('ebb', 0.0)),
                lists['hc'][0], lists['hv'][0], lists['hc'][1], lists['hv'][1]))
        L.append('static const TStation %s_r%d[%d] = {' % (prefix, ri, len(ports)))
        L += inits
        L.append('};')
        regs.append('  {%s, %s, %d, %s_r%d},' % (cstr(code), cstr(title), len(ports), prefix, ri))
    if regs:
        L.append('const TRegion %s_REGIONS[%d] = {' % (prefix.upper(), len(regs)))
        L += regs
        L.append('};')
    else:
        L.append('const TRegion %s_REGIONS[1] = {{0, 0, 0, 0}};' % prefix.upper())
    L.append('const int %s_NREGIONS = %d;' % (prefix.upper(), len(regs)))
    if flow is not None:
        fl = flow
        cells = []
        for c in fl['cells']:
            if c is None:
                cells.append('0,' * 26)
            else:
                cells.append(''.join('%d,%d,' % (u, v) for u, v in c))
        L.append('static const unsigned char FLOW_CELLS[%d] = {' % (26 * len(fl['cells'])))
        for i in range(0, len(cells), 4):
            L.append('  ' + ''.join(cells[i:i + 4]))
        L.append('};')
        L.append('const FlowAtlas FLOW_ATLAS = {%s, %s, %s, %s, %d, %d, %d, %s, FLOW_CELLS};' % (
            cstr(fl.get('ref', '')), cstr(fl.get('source', '')), num(fl['lat0']), num(fl['lon0']),
            int(fl['step_min']), int(fl['nlat']), int(fl['nlon']), num(fl['q'])))
        L.append('const int FLOW_ATLAS_PRESENT = 1;')
    elif prefix == 'priv':
        L.append('const FlowAtlas FLOW_ATLAS = {0, 0, 0, 0, 0, 0, 0, 0, 0};')
        L.append('const int FLOW_ATLAS_PRESENT = 0;')
    L.append('')
    open(out, 'w').write('\n'.join(L))
    return sum(len(r[3]) for r in regions)


def tid_lines(R, code, title, ports):
    L = ['NAV47 TIDES 1', 'REGION %s %s' % (code, title)]
    for st in ports:
        L.append('STATION %s' % st['name'])
        L.append('KIND %s' % {'rect': 'RECT', 'rot': 'ROT'}.get(st.get('kind'), 'TIDE'))
        L.append('POS %r %r' % (float(st['lat']), float(st['lon'])))
        L.append('TZ %r %s' % (float(st['tz']), 'US' if st.get('dst') == 'US' else '-'))
        L.append('DATUM %s' % (st.get('datum') or '-'))
        L.append('SRC %s' % (st.get('src') or '-'))
        L.append('Z0 %r' % float(st.get('z0', 0.0)))
        if st.get('kind') in ('rect', 'rot'):
            L.append('V0 %r' % float(st.get('v0', 0.0)))
            L.append('FLOOD %r' % float(st.get('flood', 0.0)))
            L.append('EBB %r' % float(st.get('ebb', 0.0)))
        for key, tag in (('hc', 'HC'), ('hv', 'HV')):
            for n, (H, g) in (st.get(key) or {}).items():
                L.append('%s %s %r %r' % (tag, n, float(H), float(g)))
        L.append('END')
    return L


def flow_lines(fl):
    L = ['NAV47 FLOW 1', 'REF %s' % fl.get('ref', ''), 'SOURCE %s' % fl.get('source', ''),
         'GRID %r %r %d %d %d %r' % (float(fl['lat0']), float(fl['lon0']), fl['step_min'], fl['nlat'], fl['nlon'],
                                     float(fl['q']))]
    for k, c in enumerate(fl['cells']):
        if c is not None:
            L.append('CELL %d %s' % (k, ' '.join('%d %d' % (u, v) for u, v in c)))
    L.append('END')
    return L


def private_mods(py):
    regs, flow = [], None
    for mod in ('stations_private', 'streams_private'):
        if os.path.exists(os.path.join(py, mod + '.py')):
            regs += importlib.import_module(mod).REGIONS
    if os.path.exists(os.path.join(py, 'flow_private.py')):
        flow = importlib.import_module('flow_private').FLOW
    return regs, flow


def main():
    a = sys.argv[1:]
    if len(a) < 2:
        print(__doc__)
        sys.exit(2)
    what, t47 = a[0], os.path.expanduser(a[1])
    if what == 'private' and t47 in ('', '-', 'none'):
        out = a[2]
        os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
        gen_regions(None, [], 'priv', out, header='no private data (build without TIDE47=)')
        print('%s: no private data' % out)
        return
    R, py = load(t47)
    if what == 'cons':
        n = gen_cons(R, os.path.join(TOP, 'src', 'tidecons.c'))
        print('tidecons.c: %d constituents' % n)
    elif what == 'public':
        S = importlib.import_module('stations')
        T = importlib.import_module('streams')
        n = gen_regions(R, S.REGIONS + T.REGIONS, 'pub', os.path.join(TOP, 'src', 'tidedata_pub.c'),
                        header='NOAA (public domain) and TICON-3 (CC-BY 4.0) via Tide 47; see NOTICE.')
        print('tidedata_pub.c: %d stations' % n)
    elif what == 'private':
        out = a[2]
        os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
        regs, flow = private_mods(py) if os.path.isdir(py) else ([], None)
        n = gen_regions(R, regs, 'priv', out, flow=flow,
                        header='PRIVATE DATA - for your own calculator only, never commit or share.')
        print('%s: %d private stations, atlas %s' % (out, n, 'yes' if flow else 'no'))
    elif what == 'files':
        outdir = a[2]
        os.makedirs(outdir, exist_ok=True)
        regs, flow = private_mods(py)
        if '--private-only' not in a:
            regs = importlib.import_module('stations').REGIONS + importlib.import_module('streams').REGIONS + regs
        for code, prog, title, ports in regs:
            fn = os.path.join(outdir, '%s.TID' % code)
            open(fn, 'w').write('\n'.join(tid_lines(R, code, title, ports)) + '\n')
            print(fn, len(ports), 'stations')
        if flow:
            fn = os.path.join(outdir, 'FLOW.FLW')
            open(fn, 'w').write('\n'.join(flow_lines(flow)) + '\n')
            print(fn, 'atlas', flow['nlat'], 'x', flow['nlon'])
    else:
        print(__doc__)
        sys.exit(2)


if __name__ == '__main__':
    main()
