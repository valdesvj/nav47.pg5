#!/usr/bin/env python3
"""patch_nav47_cmp.py - NAV47 COMPARE page (geodesic vs rhumb vs composite for one leg)
and ellipsoid choice.  Run from ~/opt/nav47.pg5 after patch_nav47_geo.py."""
import shutil, sys, os

CMP_CODE = r'''/* ------------------------------------------------------------------ */
/*  COMPARE: one leg as geodesic, rhumb and composite (geo47)         */
/* ------------------------------------------------------------------ */
static int cmp_leg(void) { return wp_cur > 0 ? wp_cur : 1; }   /* the leg that ends at the cursor */

static void draw_cmp(void) {
  char s[64], t[32];
  int i = cmp_leg(), y;
  if (N->nwp < 2 || i >= N->nwp) { header("COMPARE"); nt(4, 48, "Needs 2 waypoints on ROUTE."); return; }
  double la1 = N->wlat[i - 1], lo1 = N->wlon[i - 1], la2 = N->wlat[i], lo2 = N->wlon[i];
  double sg, a1, a2, sr, ar, latv, lonv, sv;
  geod_inverse(&geo_ell, la1, lo1, la2, lo2, &sg, &a1, &a2);
  rhumb_inverse(&geo_rh, la1, lo1, la2, lo2, &sr, &ar);
  int in = geod_vertex(&geo_ell, la1, lo1, la2, lo2, &latv, &lonv, &sv);
  const int xg = 268, xr = 396;
  sprintf(s, "COMPARE LEG %d-%d", i, i + 1);
  header(s);
  y = 32;  nt(4, y, "FROM");
  fmt_dmn(t, la1, 2, la1 < 0 ? 'S' : 'N'); nt_r(xg, y, t); fmt_dmn(t, lo1, 3, lo1 < 0 ? 'W' : 'E'); nt_r(xr, y, t);
  y = 48;  nt(4, y, "TO");
  fmt_dmn(t, la2, 2, la2 < 0 ? 'S' : 'N'); nt_r(xg, y, t); fmt_dmn(t, lo2, 3, lo2 < 0 ? 'W' : 'E'); nt_r(xr, y, t);
  y = 72;  nt_r(xg, y, "GEODESIC"); nt_r(xr, y, "RHUMB");
  y = 88;  nt(4, y, "DIST NM");
  sprintf(t, "%.1f", sg / NM); nt_r(xg, y, t); sprintf(t, "%.1f", sr / NM); nt_r(xr, y, t);
  y = 104; nt(4, y, "INITIAL CRS");
  sprintf(t, "%05.1f" NT_DEG, fmod(a1 + 360.0, 360.0)); nt_r(xg, y, t);
  sprintf(t, "%05.1f" NT_DEG, fmod(ar + 360.0, 360.0)); nt_r(xr, y, t);
  y = 120; nt(4, y, "FINAL CRS");
  sprintf(t, "%05.1f" NT_DEG, fmod(a2 + 360.0, 360.0)); nt_r(xg, y, t);
  sprintf(t, "%05.1f" NT_DEG, fmod(ar + 360.0, 360.0)); nt_r(xr, y, t);
  y = 136; nt(4, y, "GC SAVES");
  sprintf(t, "%.1f NM  %.1f%%", (sr - sg) / NM, sr > 0 ? 100.0 * (sr - sg) / sr : 0.0); nt_r(xr, y, t);
  y = 152; nt(4, y, in ? "VERTEX" : "VERTEX (off leg)");
  fmt_dmn(t, latv, 2, latv < 0 ? 'S' : 'N'); nt_r(xg, y, t); fmt_dmn(t, lonv, 3, lonv < 0 ? 'W' : 'E'); nt_r(xr, y, t);
  y = 168;
  if (in && fabs(latv) > cs_lim) {
    double cc, cn;
    int rc = geo_leg_cs(la1, lo1, la2, lo2, cs_lim, &cc, &cn);
    plan_dirty = 1;                                    /* geo_route was used as scratch */
    sprintf(s, "CS %.0f" NT_DEG, cs_lim); nt(4, y, s);
    if (rc == ROUTE_OK) { sprintf(t, "%.1f NM  %05.1f" NT_DEG, cn, cc); nt_r(xr, y, t); }
    else nt_r(xr, y, "not possible (end beyond)");
  } else {
    sprintf(s, "CS %.0f" NT_DEG, cs_lim); nt(4, y, s); nt_r(xr, y, "limit not reached");
  }
  sprintf(s, "7 9 %s  8 2 leg", ellipsoids[geo_ell_index].name);
  nt(2, 192, s);
  nt_r(LCD_X - 2, 192, "EXIT back");
}

static void cmp_key(int key) {
  if (key == KEY_7 || key == KEY_9) {                  /* ellipsoid: presets only, not CUSTOM */
    int n = ELL_CUSTOM, e = (geo_ell_index + (key == KEY_9 ? 1 : n - 1)) % n;
    geo_set_ellipsoid(e);
    route_save();                                      /* saves ELL, marks legs and plan to redo */
  }
  else if ((key == KEY_8 || key == KEY_UP) && cmp_leg() > 1) wp_cur = cmp_leg() - 1;
  else if ((key == KEY_2 || key == KEY_DOWN) && cmp_leg() < N->nwp - 1) wp_cur = cmp_leg() + 1;
  else if (key == KEY_EXIT) { go(PG_ROUTE); menu_page = 3; }
}

static const char *MENUS[4][6] = {'''

PAIRS = [
    ("PG_PICK, PG_PLAN };", "PG_PICK, PG_PLAN, PG_CMP };"),
    ("    case PG_PLAN: draw_plan(); break;", "    case PG_PLAN: draw_plan(); break;\n    case PG_CMP: draw_cmp(); break;"),
    ("  case PG_PLAN:\n    plan_key(key);\n    return;\n",
     "  case PG_PLAN:\n    plan_key(key);\n    return;\n  case PG_CMP:\n    cmp_key(key);\n    return;\n"),
    ("    else if (key == KEY_RUN && N->nwp > 1) { plan_cur = 0; go(PG_PLAN); }\n",
     "    else if (key == KEY_RUN && N->nwp > 1) { plan_cur = 0; go(PG_PLAN); }\n"
     "    else if (key == KEY_5 && N->nwp > 1) go(PG_CMP);\n"),
    ('"NAV47 ROUTE 1\\n%s\\nLIM %.1f\\nSPLIT %d %.3f\\n", SAIL_NAME[N->cfg.gc % 3], cs_lim, plan_split, plan_val);',
     '"NAV47 ROUTE 1\\n%s\\nLIM %.1f\\nSPLIT %d %.3f\\nELL %d\\n", SAIL_NAME[N->cfg.gc % 3], cs_lim, plan_split, plan_val, geo_ell_index);'),
    ("{ plan_split = m; plan_val = v; } }",
     "{ plan_split = m; plan_val = v; } }\n    else if (sscanf(l, \"ELL %d\", &m) == 1) geo_set_ellipsoid(m);"),
    ('"4 6 limit  R/S plan" : "ENTER edit  R/S plan"', '"4 6 limit 5 cmp R/S plan" : "ENTER edit 5 cmp R/S plan"'),
    ("static const char *MENUS[4][6] = {", CMP_CODE),
]

if not os.path.exists("src/main.c"): sys.exit("run this from ~/opt/nav47.pg5")
s = open("src/main.c").read()
if "draw_cmp" in s: sys.exit("src/main.c already has COMPARE, nothing done")
if "geo47 PATCH" not in s: sys.exit("run patch_nav47_geo.py first")
for a, b in PAIRS:
    if s.count(a) != 1: sys.exit(f"src/main.c: text found {s.count(a)} times, nothing changed:\n---\n{a}\n---")
shutil.copy("src/main.c", "src/main.c.bak2")
for a, b in PAIRS: s = s.replace(a, b)
open("src/main.c", "w").write(s)
print("patched src/main.c (backup src/main.c.bak2) - now: make clean && make")
