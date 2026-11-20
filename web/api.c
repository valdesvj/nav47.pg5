/* api.c - NAV47 on the web: the calculator's own engines for the page's Modern tabs (web/app.js), in the same
 * WebAssembly module as the calculator (make web). The results go into OUT (doubles) for JavaScript to read;
 * the waypoints of a plan come in through IN. Nothing here touches the calculator's own state.
 * GPL-3.0-or-later. */
#include <emscripten.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "astro.h"
#include "almdata.h"
#include "views.h"
#include "moon47.h"
#include "tide.h"
#include "zones.h"
#include "geodesic.h"
#include "geo_rhumb.h"
#include "geo_route.h"
#include "geo_ellipsoids.h"

#define K EMSCRIPTEN_KEEPALIVE
static double OUT[8192], IN[4096];
K double *api_out(void) { return OUT; }
K double *api_in(void) { return IN; }

/* ---------------------------------------------------------------- the ellipsoid, geodesics, rhumb lines */
static struct geod_geodesic G;
static rhumb_t RH;
static int g_ready;
static void ready(void) { if (!g_ready) { geod_init(&G, 6378137.0, 1 / 298.257223563); rhumb_init(&RH, &G); g_ready = 1; } }

K int api_ell_count(void) { return ELL_COUNT; }
K const char *api_ell_name(int i) { return ellipsoids[i].name; }
K double api_ell_a(int i) { return ellipsoids[i].a; }
K double api_ell_rf(int i) { return ellipsoids[i].rf; }
K void api_ell(double a, double rf) { geod_init(&G, a, rf ? 1 / rf : 0); rhumb_init(&RH, &G); g_ready = 1; }

/* OUT: s12 (m), azi1, azi2 */
K void api_inv(double la1, double lo1, double la2, double lo2) { ready(); geod_inverse(&G, la1, lo1, la2, lo2, OUT, OUT + 1, OUT + 2); }
/* OUT: lat2, lon2, azi2 */
K void api_dir(double la1, double lo1, double azi, double s) { ready(); geod_direct(&G, la1, lo1, azi, s, OUT, OUT + 1, OUT + 2); }
/* OUT: s12, azi (rhumb) */
K int api_rinv(double la1, double lo1, double la2, double lo2) { ready(); return rhumb_inverse(&RH, la1, lo1, la2, lo2, OUT, OUT + 1); }
/* OUT: lat2, lon2 (lon unrolled) */
K int api_rdir(double la1, double lo1, double azi, double s) { ready(); return rhumb_direct(&RH, la1, lo1, azi, s, OUT, OUT + 1); }
/* OUT: vertex lat, lon, distance from the start; returns 1 when the vertex is inside the leg */
K int api_vertex(double la1, double lo1, double la2, double lo2) { ready(); return geod_vertex(&G, la1, lo1, la2, lo2, OUT, OUT + 1, OUT + 2); }
/* n + 1 points along the geodesic: OUT lat, lon pairs (lon unrolled from lo1) */
K int api_geod_pts(double la1, double lo1, double la2, double lo2, int n) {
  ready();
  struct geod_geodesicline l;
  geod_inverseline(&l, &G, la1, lo1, la2, lo2, GEOD_LATITUDE | GEOD_LONGITUDE | GEOD_DISTANCE_IN | GEOD_LONG_UNROLL);
  if (n > 2000) n = 2000;
  for (int i = 0; i <= n; i++) {
    double a, b;
    geod_genposition(&l, GEOD_LONG_UNROLL, l.s13 * i / n, &a, &b, NULL, NULL, NULL, NULL, NULL, NULL);
    OUT[2 * i] = a; OUT[2 * i + 1] = b;
  }
  return n + 1;
}

/* the plan: IN holds nwp (lat, lon) pairs; sail 0 RL 1 GC 2 CS; split 0 none 1 every val NM 2 val legs 3 meridians.
 * OUT: 10 a waypoint: lat lon leg_m cum_m crs_in crs_out rl_crs rl_m kind flags. Returns the waypoints, < 0 an error */
static route_t *R;
K int api_plan(int nwp, int sail, double lim, int split, double val) {
  static const sail_t S[3] = { SAIL_RHUMB, SAIL_GEOD, SAIL_COMPOSITE };
  ready();
  if (!R && !(R = (route_t *)calloc(1, sizeof *R))) return -100;
  if (nwp < 2) return 0;
  double v = split == SPLIT_DIST ? val * 1852.0 : val;
  route_init(R, &G);
  int rc0 = ROUTE_OK;
  for (int i = 1; i < nwp; i++) {
    int rc = route_add_leg(R, IN[0], IN[1], IN[2 * i], IN[2 * i + 1], S[sail % 3], lim, (split_t)split, v);
    if (rc == ROUTE_ELIMIT || rc == ROUTE_ECOMP)
      rc = route_add_leg(R, 0, 0, IN[2 * i], IN[2 * i + 1], SAIL_GEOD, 0, (split_t)split, v);
    if (rc != ROUTE_OK && rc0 == ROUTE_OK) rc0 = rc;
  }
  route_finish(R);
  int n = R->n > 800 ? 800 : R->n;
  for (int i = 0; i < n; i++) {
    const wpt_t *w = &R->w[i];
    double *o = OUT + 10 * i;
    o[0] = w->lat; o[1] = w->lon; o[2] = w->leg_m; o[3] = w->cum_m; o[4] = w->crs_in; o[5] = w->crs_out;
    o[6] = w->rl_crs; o[7] = w->rl_m; o[8] = w->kind; o[9] = w->flags;
  }
  OUT[8190] = rc0;
  OUT[8191] = R->full;
  return n;
}

/* ---------------------------------------------------------------- the almanac */
static Al al;
/* OUT: 64 bodies x (gha dec hc zn): the Sun, the Moon, Venus Mars Jupiter Saturn, stars 1..58;
 * then [256..] times NTWA RISE TRAN SET NTWP (hours of the shown day), illum, age, sun SD, moon HP, moon SD, aries */
K int api_alm(double jd, double lat, double lon, double tz) {
  al_compute(&al, jd, lat, lon, tz);
  double g, d, sha, hc, zn;
  int k = 0;
  #define PUT(G_, D_) do { hcz(lat, lon, (D_), (G_), &hc, &zn); OUT[4 * k] = (G_); OUT[4 * k + 1] = (D_); \
                           OUT[4 * k + 2] = hc; OUT[4 * k + 3] = zn; k++; } while (0)
  PUT(al.sun.gha, al.sun.dec);
  PUT(al.moon[0], al.moon[1]);
  for (int p = 1; p <= 4; p++) PUT(al.planets[p][0], al.planets[p][1]);
  for (int n = 1; n <= 58; n++) { star_pos(&al.sun, n, &g, &d, &sha); PUT(g, d); }
  #undef PUT
  for (int i = 0; i < 5; i++) OUT[256 + i] = al.times[i];
  OUT[261] = al.illum; OUT[262] = al.age; OUT[263] = al.sun.sd; OUT[264] = al.moon[2]; OUT[265] = al.moon[3];
  OUT[266] = al.sun.aries;
  return k;
}
K const char *api_star_name(int n) { return STAR_NAME[n - 1]; }
K int api_bright(int i) { return BRIGHT[i]; }
/* OUT: lit %, age d, HP, SD, phase index, then the next 4 phases (JD, kind) */
K void api_moon(double jd) {
  Moon47 p;
  moon47_page(jd, 0, &p);
  OUT[0] = p.lit; OUT[1] = p.age; OUT[2] = p.hp; OUT[3] = p.sd; OUT[4] = p.index;
  for (int i = 0; i < 4; i++) { OUT[5 + 2 * i] = p.next_t[i]; OUT[6 + 2 * i] = p.next_k[i]; }
}
K const char *api_moon_name(int i) { return M47_NAMES[i]; }

/* ---------------------------------------------------------------- tides (the public stations) */
static const TStation *station(int i, const TRegion **reg) {
  for (int r = 0; r < PUB_NREGIONS; r++) {
    if (i < PUB_REGIONS[r].n) { if (reg) *reg = &PUB_REGIONS[r]; return &PUB_REGIONS[r].st[i]; }
    i -= PUB_REGIONS[r].n;
  }
  return NULL;
}
K int api_tide_count(void) { int n = 0; for (int r = 0; r < PUB_NREGIONS; r++) n += PUB_REGIONS[r].n; return n; }
K const char *api_tide_name(int i) { const TStation *s = station(i, NULL); return s ? s->name : ""; }
K const char *api_tide_region(int i) { const TRegion *r = NULL; station(i, &r); return r ? r->title : ""; }
K const char *api_tide_datum(int i) { const TStation *s = station(i, NULL); return s && s->datum ? s->datum : ""; }
/* OUT: lat lon tz kind */
K void api_tide_info(int i) {
  const TStation *s = station(i, NULL);
  if (!s) return;
  OUT[0] = s->lat; OUT[1] = s->lon; OUT[2] = s->tz; OUT[3] = s->kind;
}
/* the day y-m-d (the station's zone time): OUT tz, nev, 8 x (t val set kind), [34..106] the curve every 20 min */
K int api_tide_day(int i, int y, int m, int d) {
  const TStation *s = station(i, NULL);
  if (!s) return -1;
  static TDay D;
  tide_day(s, jdn(y, m, d), s->kind != TK_TIDE, &D);
  OUT[0] = D.tz; OUT[1] = D.nev;
  for (int k = 0; k < 8; k++) { OUT[2 + 4 * k] = D.ev[k].t; OUT[3 + 4 * k] = D.ev[k].val; OUT[4 + 4 * k] = D.ev[k].set; OUT[5 + 4 * k] = D.ev[k].kind; }
  for (int k = 0; k < 73; k++) OUT[34 + k] = D.curve[k];
  return D.nev;
}

/* ---------------------------------------------------------------- time zones */
K int api_zone_count(void) { return NZONES; }
K double api_zone_off(int i) { return ZONES[i].off; }
K int api_zone_cities(int i) { return ZONES[i].n; }
K const char *api_city_name(int i, int j) { return ZONES[i].c[j].name; }
K const char *api_city_cc(int i, int j) { return ZONES[i].c[j].cc; }
/* OUT: the offset now (h), 1 when summer time */
K void api_city_off(int i, int j, double jd) { int on = 0; OUT[0] = city_offset(&ZONES[i], &ZONES[i].c[j], jd, &on); OUT[1] = on; }
K const char *api_tzdata(void) { return TZDATA_VERSION; }
