#!/usr/bin/env python3
"""patch_nav47_geo.py - connect geo47 to NAV47's ROUTE page and add a PLAN page.
Run from ~/opt/nav47.pg5.  Makes .bak copies; refuses to run twice."""
import shutil, sys, os

def edit(path, pairs):
    s = open(path).read()
    if "geo47 PATCH" in s:
        sys.exit(f"{path}: already patched, nothing done")
    for old, new in pairs:
        if s.count(old) != 1:
            sys.exit(f"{path}: expected text found {s.count(old)} times, nothing changed:\n---\n{old}\n---")
    shutil.copy(path, path + ".bak")
    for old, new in pairs:
        s = s.replace(old, new)
    open(path, "w").write(s)
    print(f"patched {path}  (backup {path}.bak)")

# ---------------------------------------------------------------- route.c
ROUTE_C = r'''/* route.c - NAV47 ROUTE: course and distance of each leg on the ellipsoid (geo47 PATCH):
 * the rhumb line exact on the ellipsoid (geo_rhumb.c) and the great circle as a geodesic
 * (Karney, geographiclib-c).  mer_parts() is kept: Bowditch Table 6 formula, WGS84 spheroid.
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#include <math.h>
#include "route.h"
#include "geo_nav.h"

#define D2R (3.141592653589793238462643383279502884 / 180.0)

double mer_parts(double lat) {
  double s = sin(lat * D2R);
  return 7915.7045 * log10(tan((45.0 + lat / 2.0) * D2R)) - 23.0133 * s - 0.0514 * s * s * s;
}

void leg_rl(double lat1, double lon1, double lat2, double lon2, double *crs, double *nm) {
  double s, a;
  if (rhumb_inverse(&geo_rh, lat1, lon1, lat2, lon2, &s, &a) != RHUMB_OK) { *crs = 0; *nm = 0; return; }
  *nm = s / NM;
  *crs = fmod(a + 360.0, 360.0);
}

void leg_gc(double lat1, double lon1, double lat2, double lon2, double *crs, double *nm) {
  double s, a1, a2;
  geod_inverse(&geo_ell, lat1, lon1, lat2, lon2, &s, &a1, &a2);
  *nm = s / NM;
  *crs = fmod(a1 + 360.0, 360.0);
}
'''

# ---------------------------------------------------------------- geo_nav.c / .h additions
GEO_NAV_C_ADD = r'''
/* geo47 PATCH: one composite leg (geodesic - parallel - geodesic) for the ROUTE page.
 * Uses geo_route as scratch.  Falls back to the geodesic when CS is not possible. */
int geo_leg_cs(double lat1, double lon1, double lat2, double lon2, double lim,
               double *crs, double *nm)
{
  int rc;
  route_init(&geo_route, &geo_ell);
  rc = route_add_leg(&geo_route, lat1, lon1, lat2, lon2, SAIL_COMPOSITE, lim, SPLIT_NONE, 0);
  if (rc != ROUTE_OK || geo_route.n < 2) {
    double s, a1, a2;
    geod_inverse(&geo_ell, lat1, lon1, lat2, lon2, &s, &a1, &a2);
    *nm = s / NM;
    *crs = fmod(a1 + 360.0, 360.0);
    return rc;
  }
  *crs = geo_route.w[0].crs_out;
  *nm = geo_route.w[geo_route.n - 1].cum_m / NM;
  return ROUTE_OK;
}
'''

# ---------------------------------------------------------------- main.c
DECLS = r'''/* geo47 PATCH: sailing RL 0 / GC 1 / CS 2, composite limit, PLAN split, leg cache */
static const char *SAIL_NAME[3] = { "RL", "GC", "CS" };
static double cs_lim = 60.0;                 /* CS limiting latitude, deg N/S */
static int plan_split = SPLIT_DLON;          /* 0 none, 1 every NM, 2 N legs, 3 meridians */
static double plan_val = 10.0;
static int legs_dirty = 1, legs_mode = -1, plan_dirty = 1, plan_cur, plan_rc;
static double leg_crs[MAXWP], leg_nm[MAXWP];
static char plan_msg[48];

static void route_save(void) {
  legs_dirty = plan_dirty = 1;
'''

LEG_OLD = '''static void leg(int i, double *crs, double *nm) {
  (N->cfg.gc ? leg_gc : leg_rl)(N->wlat[i - 1], N->wlon[i - 1], N->wlat[i], N->wlon[i], crs, nm);
}'''
LEG_NEW = '''static void leg(int i, double *crs, double *nm) {   /* geo47: cached, recomputed on change */
  if (legs_dirty || legs_mode != N->cfg.gc) {
    for (int k = 1; k < N->nwp; k++) {
      if (N->cfg.gc == 2)
        geo_leg_cs(N->wlat[k - 1], N->wlon[k - 1], N->wlat[k], N->wlon[k], cs_lim, &leg_crs[k], &leg_nm[k]);
      else
        (N->cfg.gc ? leg_gc : leg_rl)(N->wlat[k - 1], N->wlon[k - 1], N->wlat[k], N->wlon[k], &leg_crs[k], &leg_nm[k]);
    }
    legs_dirty = 0;
    legs_mode = N->cfg.gc;
    plan_dirty = 1;                       /* geo_leg_cs used geo_route as scratch */
  }
  *crs = leg_crs[i];
  *nm = leg_nm[i];
}'''

PLAN_CODE = r'''/* ------------------------------------------------------------------ */
/*  PLAN: the route split into waypoints (geo47)                      */
/* ------------------------------------------------------------------ */
static const double STEP_DIST[] = { 10, 20, 30, 50, 60, 100, 120, 150, 200, 250, 300, 500 };
static const double STEP_LEGS[] = { 2, 3, 4, 5, 6, 8, 10, 12, 15, 20 };
static const double STEP_DLON[] = { 1, 2, 5, 10, 15, 20 };

static void plan_build(void) {
  static const int sail[3] = { SAIL_RHUMB, SAIL_GEOD, SAIL_COMPOSITE };
  double v = plan_split == SPLIT_DIST ? plan_val * NM : plan_val;
  if (legs_dirty) { double c, n; leg(1, &c, &n); }        /* refresh the ROUTE cache first */
  route_init(&geo_route, &geo_ell);
  plan_rc = ROUTE_OK;
  for (int i = 1; i < N->nwp; i++) {
    int rc = route_add_leg(&geo_route, N->wlat[0], N->wlon[0], N->wlat[i], N->wlon[i],
                           (sail_t)sail[N->cfg.gc % 3], cs_lim, (split_t)plan_split, v);
    if (rc == ROUTE_ELIMIT || rc == ROUTE_ECOMP)           /* this leg cannot be CS: sail it GC */
      rc = route_add_leg(&geo_route, 0, 0, N->wlat[i], N->wlon[i], SAIL_GEOD, 0, (split_t)plan_split, v);
    if (rc != ROUTE_OK && plan_rc == ROUTE_OK) plan_rc = rc;
  }
  route_finish(&geo_route);
  plan_dirty = 0;
}

static void draw_plan(void) {
  static const char *SPL[4] = { "", "NM", "LEGS", "DLON" };
  char s[64], r[32];
  route_t *R = &geo_route;
  if (plan_dirty || legs_dirty) plan_build();
  if (plan_split == SPLIT_NONE) sprintf(s, "PLAN %s", SAIL_NAME[N->cfg.gc % 3]);
  else sprintf(s, "PLAN %s %g %s", SAIL_NAME[N->cfg.gc % 3], plan_val, SPL[plan_split]);
  header(s);
  int xw = 6 + nt_w("*000"), xla = xw + 10 + nt_w("00" NT_DEG "00.0N"), xlo = xla + 12 + nt_w("000" NT_DEG "00.0E"),
      xc = xlo + 12 + nt_w("000" NT_DEG), xd = xc + 12 + nt_w("0000.0"), xcu = xd + 12 + nt_w("00000.0"),
      xk = xcu + 8 + nt_w("P");
  int e = (396 - xk) / 6;
  if (e > 0) { xla += e; xlo += 2 * e; xc += 3 * e; xd += 4 * e; xcu += 5 * e; xk += 6 * e; }
  nt_r(xw, 16, "WP"); nt_c(xla - nt_w("00" NT_DEG "00.0N") / 2, 16, "LAT"); nt_c(xlo - nt_w("000" NT_DEG "00.0E") / 2, 16, "LON");
  nt_r(xc, 16, "STEER"); nt_r(xd, 16, "LEG"); nt_r(xcu, 16, "CUM");
  if (R->n < 2) { nt(4, 48, "Needs 2 waypoints on ROUTE."); return; }
  int vis = 10;
  if (plan_cur >= R->n) plan_cur = R->n - 1;
  int top = plan_cur - vis / 2;
  if (top > R->n - vis) top = R->n - vis;
  if (top < 0) top = 0;
  for (int i = top; i < R->n && i < top + vis; i++) {
    const wpt_t *w = &R->w[i];
    int y = 32 + NT_ROW * (i - top);
    sprintf(s, "%s%d", (w->flags & WPT_USER) ? "*" : "", i + 1);
    if (i == plan_cur) nt_inv(xw - nt_w(s), y, s); else nt_r(xw, y, s);
    fmt_dmn(s, w->lat, 2, w->lat < 0 ? 'S' : 'N'); nt_r(xla, y, s);
    fmt_dmn(s, w->lon, 3, w->lon < 0 ? 'W' : 'E'); nt_r(xlo, y, s);
    if (i > 0) {
      sprintf(s, "%03.0f" NT_DEG, fmod(floor(R->w[i - 1].rl_crs + 0.5), 360.0)); nt_r(xc, y, s);
      sprintf(s, "%.1f", w->leg_m / NM); nt_r(xd, y, s);
      sprintf(s, "%.1f", w->cum_m / NM); nt_r(xcu, y, s);
      s[0] = w->kind; s[1] = 0; nt_r(xk, y, s);
    }
  }
  if (!plan_msg[0] && R->full) strcpy(plan_msg, "plan full: larger split");
  if (!plan_msg[0] && plan_rc) strcpy(plan_msg, "some legs sailed GC, not CS");
  if (plan_msg[0]) nt_inv(2, 192, plan_msg);
  else nt(2, 192, "5 split 4 6 step ENTER save");
  sprintf(r, "%d WP %.1f NM", R->n, R->w[R->n - 1].cum_m / NM);
  if (nt_w(r) + nt_w(plan_msg[0] ? plan_msg : "5 split 4 6 step ENTER save") < 388) nt_r(LCD_X - 2, 192, r);
}

static void plan_step(int dir) {
  const double *t; int n, k = 0;
  if (plan_split == SPLIT_DIST) { t = STEP_DIST; n = 12; }
  else if (plan_split == SPLIT_LEGS) { t = STEP_LEGS; n = 10; }
  else if (plan_split == SPLIT_DLON) { t = STEP_DLON; n = 6; }
  else return;
  while (k < n - 1 && t[k] < plan_val - 1e-9) k++;
  k += dir;
  if (k < 0) k = 0;
  if (k > n - 1) k = n - 1;
  plan_val = t[k];
}

static void plan_export(void) {
  if (!sys_disk_ok()) { strcpy(plan_msg, "disk busy (USB?)"); return; }
  sys_disk_write_enable(1);
  check_create_dir("/NAV47");
  check_create_dir("/NAV47/ROUTES");
  sys_disk_write_enable(0);
  strcpy(plan_msg, geo_save_csv("/NAV47/ROUTES/PLAN.CSV") == 0 ? "saved /NAV47/ROUTES/PLAN.CSV" : "save failed");
}

static void plan_key(int key) {
  plan_msg[0] = 0;
  if ((key == KEY_8 || key == KEY_UP) && plan_cur > 0) plan_cur--;
  else if ((key == KEY_2 || key == KEY_DOWN) && plan_cur < geo_route.n - 1) plan_cur++;
  else if (key == KEY_5) {
    plan_split = (plan_split + 1) % 4;
    plan_val = plan_split == SPLIT_DIST ? 100 : plan_split == SPLIT_LEGS ? 4 : plan_split == SPLIT_DLON ? 10 : 0;
    route_save();
  }
  else if (key == KEY_4 || key == KEY_6) { plan_step(key == KEY_6 ? 1 : -1); route_save(); }
  else if (key == KEY_ENTER) plan_export();
  else if (key == KEY_EXIT) { go(PG_ROUTE); menu_page = 3; }
}

static const char *MENUS[4][6] = {'''

MAIN_PAIRS = [
    ("PG_INFO, PG_PICK };", "PG_INFO, PG_PICK, PG_PLAN };"),
    ("static void route_save(void) {\n", DECLS),
    ('o += sprintf(o, "NAV47 ROUTE 1\\n%s\\n", N->cfg.gc ? "GC" : "RL");',
     'o += sprintf(o, "NAV47 ROUTE 1\\n%s\\nLIM %.1f\\nSPLIT %d %.3f\\n", SAIL_NAME[N->cfg.gc % 3], cs_lim, plan_split, plan_val);'),
    ('''    double a, b;
    if (!strcmp(l, "GC")) N->cfg.gc = 1;
    else if (!strcmp(l, "RL")) N->cfg.gc = 0;''',
     '''    double a, b, v;
    int m;
    if (!strcmp(l, "GC")) N->cfg.gc = 1;
    else if (!strcmp(l, "RL")) N->cfg.gc = 0;
    else if (!strcmp(l, "CS")) N->cfg.gc = 2;
    else if (sscanf(l, "LIM %lf", &v) == 1) { if (v >= 10 && v <= 85) cs_lim = v; }
    else if (sscanf(l, "SPLIT %d %lf", &m, &v) == 2) { if (m >= 0 && m <= 3) { plan_split = m; plan_val = v; } }'''),
    ("static void route_insert(double lat, double lon) {\n",
     "static void route_insert(double lat, double lon) {\n  legs_dirty = plan_dirty = 1;\n"),
    ("static void route_delete(void) {\n",
     "static void route_delete(void) {\n  legs_dirty = plan_dirty = 1;\n"),
    (LEG_OLD, LEG_NEW),
    ('header(N->cfg.gc ? "ROUTE GC" : "ROUTE RL");',
     'if (N->cfg.gc == 2) sprintf(s, "ROUTE CS %.0f", cs_lim); else strcpy(s, N->cfg.gc ? "ROUTE GC" : "ROUTE RL");\n  header(s);'),
    ('nt(4, 160, "RL: rhumb line, GC: great");', 'nt(4, 160, "RL rhumb, GC great circle,");'),
    ('nt(4, 176, "circle (initial course)");', 'nt(4, 176, "CS composite (4 6 limit)");'),
    ('nt(2, 192, "UP DN select   ENTER edit");', 'nt(2, 192, N->cfg.gc == 2 ? "4 6 limit  R/S plan" : "ENTER edit  R/S plan");'),
    ("static const char *MENUS[4][6] = {", PLAN_CODE),
    ("case PG_ROUTE: draw_route(); break;", "case PG_ROUTE: draw_route(); break;\n    case PG_PLAN: draw_plan(); break;"),
    ("case 21: N->cfg.gc ^= 1; route_save(); break;", "case 21: N->cfg.gc = (N->cfg.gc + 1) % 3; route_save(); break;"),
    ('''    else if (key == KEY_ENTER) wp_start_edit();
    break;
  case PG_SET:''',
     '''    else if (key == KEY_ENTER) wp_start_edit();
    else if (key == KEY_RUN && N->nwp > 1) { plan_cur = 0; go(PG_PLAN); }
    else if (key == KEY_4 && N->cfg.gc == 2 && cs_lim > 10) { cs_lim -= 1; route_save(); }
    else if (key == KEY_6 && N->cfg.gc == 2 && cs_lim < 85) { cs_lim += 1; route_save(); }
    break;
  case PG_PLAN:
    plan_key(key);
    return;
  case PG_SET:'''),
]

if not os.path.exists("src/main.c") or not os.path.exists("src/geo_nav.c"):
    sys.exit("run this from ~/opt/nav47.pg5")

edit("src/main.c", MAIN_PAIRS)

s = open("src/route.c").read()
if "geo47 PATCH" in s: print("src/route.c already patched")
else:
    shutil.copy("src/route.c", "src/route.c.bak"); open("src/route.c", "w").write(ROUTE_C)
    print("patched src/route.c  (backup src/route.c.bak)")

s = open("src/geo_nav.c").read()
if "geo_leg_cs" in s: print("src/geo_nav.c already has geo_leg_cs")
else:
    shutil.copy("src/geo_nav.c", "src/geo_nav.c.bak")
    if "#include <math.h>" not in s: s = "#include <math.h>\n" + s
    open("src/geo_nav.c", "w").write(s + GEO_NAV_C_ADD)
    h = open("src/geo_nav.h").read()
    h = h.replace("#endif", "int  geo_leg_cs(double lat1, double lon1, double lat2, double lon2, double lim,\n"
                  "                 double *crs, double *nm);      /* composite leg, 0 = ok */\n#endif")
    open("src/geo_nav.h", "w").write(h)
    print("patched src/geo_nav.c and src/geo_nav.h")
print("done - now: make clean && make")
