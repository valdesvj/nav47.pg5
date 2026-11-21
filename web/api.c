/* api.c - NAV47 on the web: the calculator's own engines for the page's Modern tabs (web/app.js), in the same
 * WebAssembly module as the calculator (make web). The results go into OUT (doubles) for JavaScript to read;
 * the waypoints of a plan come in through IN. Nothing here touches the calculator's own state.
 * GPL-3.0-or-later. */
#include <emscripten.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
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
#include "tidefile.h"
#include "dpops.h"

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
/* the stations: the compiled public ones, then the loaded files (NOAA's currents in the page, your own .TID) */
static TRegion *WR;
static int WN, WN_PUBLIC;                 /* loaded regions; the first WN_PUBLIC of them are the page's own (NOAA) */
static const TStation *station(int i, const TRegion **reg) {
  for (int r = 0; r < PUB_NREGIONS; r++) {
    if (i < PUB_REGIONS[r].n) { if (reg) *reg = &PUB_REGIONS[r]; return &PUB_REGIONS[r].st[i]; }
    i -= PUB_REGIONS[r].n;
  }
  for (int r = 0; r < PRIV_NREGIONS; r++) {           /* a private build (make web-private): compiled in */
    if (i < PRIV_REGIONS[r].n) { if (reg) *reg = &PRIV_REGIONS[r]; return &PRIV_REGIONS[r].st[i]; }
    i -= PRIV_REGIONS[r].n;
  }
  for (int r = 0; r < WN; r++) {
    if (i < WR[r].n) { if (reg) *reg = &WR[r]; return &WR[r].st[i]; }
    i -= WR[r].n;
  }
  return NULL;
}
K int api_tide_count(void) {
  int n = 0;
  for (int r = 0; r < PUB_NREGIONS; r++) n += PUB_REGIONS[r].n;
  for (int r = 0; r < PRIV_NREGIONS; r++) n += PRIV_REGIONS[r].n;
  for (int r = 0; r < WN; r++) n += WR[r].n;
  return n;
}
/* the private data compiled in (a private build): its regions, and the FLOW atlas */
K int api_private(void) { return PRIV_NREGIONS + FLOW_ATLAS_PRESENT; }
/* 0 compiled public, 1 the page's NOAA / TICON files, 2 your files, 3 compiled private */
K int api_tide_src(int i) {
  const TRegion *r = NULL;
  station(i, &r);
  if (r && r >= PRIV_REGIONS && r < PRIV_REGIONS + PRIV_NREGIONS) return 3;
  if (!r || (r >= PUB_REGIONS && r < PUB_REGIONS + PUB_NREGIONS)) return 0;
  return (int)(r - WR) < WN_PUBLIC ? 1 : 2;
}
/* a .TID file (its text, copied: the parser works in place); public = 1 for the page's own files.
   Returns the stations read, -1 an error (api_err) */
static char ERR[64];
K const char *api_err(void) { return ERR; }
K int api_tid_load(const char *text, int public) {
  char *t = strdup(text);
  if (!t) return -1;
  if (public && WN != WN_PUBLIC) { free(t); strcpy(ERR, "public files first"); return -1; }
  int r = tid_parse(t, &WR, &WN, ERR, sizeof ERR);
  free(t);
  if (r > 0 && public) WN_PUBLIC = WN;
  return r;
}
K void api_tid_forget(void) { WN = WN_PUBLIC; }            /* your files (their memory stays: small) */
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

/* ZONE47's dot map: MAP_NCOL x MAP_NROW; column j: OUT[0] the low 32 bits, OUT[1] the high (bit i = row i, land) */
K int api_map_ncol(void) { return MAP_NCOL; }
K int api_map_nrow(void) { return MAP_NROW; }
K void api_map_col(int j) { unsigned long long b = MAP_COLS[j]; OUT[0] = (double)(b & 0xFFFFFFFFull); OUT[1] = (double)(b >> 32); }
K int api_zone_col(int i) { return ZONES[i].col; }
/* a place: 0 no summer time, 1 by rule, 2 irregular */
K int api_city_kind(int i, int j) { return ZONES[i].c[j].kind; }
K const char *api_city_tzid(int i, int j) { return ZONES[i].c[j].tzid ? ZONES[i].c[j].tzid : ""; }

/* ---------------------------------------------------------------- the FLOW atlas (your .FLW), as T&F on the calculator */
static FlowAtlas WF;
static int WF_have;
/* the atlas in use: a loaded file, else the one compiled in (a private build) */
static const FlowAtlas *ATL(void) { return WF_have ? &WF : FLOW_ATLAS_PRESENT ? &FLOW_ATLAS : NULL; }
K int api_flw_load(const char *text) {
  char *t = strdup(text);
  if (!t) return -1;
  int r = flw_parse(t, &WF, ERR, sizeof ERR);
  free(t);
  if (r > 0) WF_have = 1;
  return r;
}
K void api_flw_forget(void) { WF_have = 0; }
K int api_flw_have(void) { return ATL() != NULL; }
K const char *api_flw_ref(void) { const FlowAtlas *a = ATL(); return a && a->ref ? a->ref : ""; }
K const char *api_flw_source(void) { const FlowAtlas *a = ATL(); return a && a->source ? a->source : ""; }
/* OUT: lat0 lon0 step_min nlat nlon */
K void api_flw_grid(void) { const FlowAtlas *a = ATL(); if (!a) return; OUT[0] = a->lat0; OUT[1] = a->lon0; OUT[2] = a->step_min; OUT[3] = a->nlat; OUT[4] = a->nlon; }

/* the station's zone time (with US summer time): its date (jdn) and hour at UT jd */
static double st_local(const TStation *st, double ut, long *n, double *h) {
  int y, m, d;
  double tz = st->tz;
  jd_to_ymd(ut + tz / 24.0, &y, &m, &d, h);
  *n = jdn(y, m, d);
  if (tide_dst(st, *n)) { tz += 1; jd_to_ymd(ut + tz / 24.0, &y, &m, &d, h); *n = jdn(y, m, d); }
  return tz;
}
static const TStation *flow_ref_station(void) {
  int n = api_tide_count();
  const FlowAtlas *a = ATL();
  for (int i = 0; i < n; i++) { const TStation *s = station(i, NULL); if (a && a->ref && strstr(s->name, a->ref)) return s; }
  return NULL;
}
static double F_fac = 1, F_off;
static double F_wdir, F_wkn;
/* the stream at (lat, lon) at UT jd, from the atlas: the reference's High Water (hw_step cycles on), springs scaled,
   the wind added. OUT: [0] in the atlas, [1] the reference's zone, [2] HW's hour (zone time) on [3] its day - today,
   [4] HW height, [5] range, [6] springs factor, [7] now - HW (hours), [8..33] HW-6..HW+6 (set, kn), [34 35] now,
   [36] reference found */
K int api_flow(double lat, double lon, double jd, double wdir, double wkn, int hw_step) {
  memset(OUT, 0, 40 * sizeof *OUT);
  if (!ATL()) return 0;
  const TStation *st = flow_ref_station();
  OUT[36] = st != NULL;
  if (!st) return 0;
  long n0, on;
  int om;
  double h0, t0, hh, rng, fac, tz = st_local(st, jd, &n0, &h0);
  flow_pick(st, n0, h0, &on, &om);
  for (int k = 0; k < hw_step; k++) flow_step(st, on, om, 1, &on, &om);
  for (int k = 0; k > hw_step; k--) flow_step(st, on, om, -1, &on, &om);
  flow_cycle(st, on, om, &t0, &hh, &rng, &fac);
  F_fac = fac; F_off = (double)(n0 - on) * 24.0 + h0 - t0; F_wdir = wdir; F_wkn = wkn;
  OUT[1] = tz; OUT[2] = t0; OUT[3] = (double)(on - n0); OUT[4] = hh; OUT[5] = rng; OUT[6] = fac; OUT[7] = F_off;
  double FU[13], FV[13], U[13], V[13];
  if (!flow_cell(ATL(), lat, lon, FU, FV)) return 0;
  flow_rows(FU, FV, fac, wdir, wkn, U, V);
  for (int k = 0; k < 13; k++) setkn(U[k], V[k], &OUT[8 + 2 * k], &OUT[9 + 2 * k]);
  if (F_off >= -6 && F_off <= 6) { double u, v; flow_now(U, V, F_off, &u, &v); setkn(u, v, &OUT[34], &OUT[35]); }
  else OUT[34] = OUT[35] = -1;
  OUT[0] = 1;
  return 1;
}
/* after api_flow: the stream at another position, hour k (0..12 = HW-6..HW+6) or k < 0 now. OUT: set, kn */
K int api_flow_at(double lat, double lon, int k) {
  double FU[13], FV[13], U[13], V[13], u, v;
  if (!ATL() || !flow_cell(ATL(), lat, lon, FU, FV)) return 0;
  flow_rows(FU, FV, F_fac, F_wdir, F_wkn, U, V);
  if (k < 0) { if (F_off < -6 || F_off > 6) return 0; flow_now(U, V, F_off, &u, &v); }
  else { u = U[k]; v = V[k]; }
  setkn(u, v, &OUT[0], &OUT[1]);
  return 1;
}
/* a stream station now: OUT rate (kn, + flood), set (deg true) */
K int api_stream_now(int i, double jd) {
  const TStation *s = station(i, NULL);
  if (!s || s->kind == TK_TIDE) return 0;
  long n;
  double h;
  st_local(s, jd, &n, &h);
  stream_now(s, n, h, &OUT[0], &OUT[1]);
  return 1;
}

/* ---------------------------------------------------------------- DP OPS (src/dpops.c, DPCAP47's engine): the
   platform's DPOPS.TXT and the vessel's .DPV as text, the weather, the best heading or a heading's balance */
static Vessel DV;
static DpPlat DPP;
static DpWx DW;
static DpRes DR;
static char DERR[64];
K const char *api_dp_err(void) { return DERR; }
K const char *api_dp_example_vessel(void) { return DPO_EXAMPLE_VESSEL; }
K const char *api_dp_example_plat(void) { return DPO_EXAMPLE_PLAT; }
/* 0 ok, else see api_dp_err */
K int api_dp_set(const char *plat, const char *vessel) {
  DERR[0] = 0;
  if (dpo_parse(plat, &DPP, DERR, sizeof DERR)) return 1;
  memset(&DV, 0, sizeof DV);
  if (dp_parse(&DV, vessel && *vessel ? vessel : DPO_EXAMPLE_VESSEL, DERR, sizeof DERR)) return 2;
  dp_prepare(&DV);
  dpo_limits(&DV, &DPP);
  return 0;
}
K const char *api_dp_vname(void) { return DV.name; }
K const char *api_dp_tname(int i) { return i >= 0 && i < DV.nthr ? DV.thr[i].name : ""; }
K const char *api_dp_ttype(int i) { return i >= 0 && i < DV.nthr ? DP_TYPE[DV.thr[i].type] : ""; }
/* OUT: lpp, b, loa, nthr, then a thruster: x, y, group, full kN, limit */
K void api_dp_vessel(void) {
  OUT[0] = DV.lpp; OUT[1] = DV.b; OUT[2] = dpo_loa(&DV, &DPP); OUT[3] = DV.nthr;
  for (int i = 0; i < DV.nthr; i++) { double *o = OUT + 4 + 5 * i; o[0] = DV.thr[i].x; o[1] = DV.thr[i].y; o[2] = DV.thr[i].grp; o[3] = DV.thr[i].tf; o[4] = DPP.lim[i]; }
}
K void api_dp_wx(double wfrom, double wkn, double cset, double ckn, double vfrom, double hs, double tp) {
  DW.wfrom = (float)wfrom; DW.wkn = (float)wkn; DW.cset = (float)cset; DW.ckn = (float)ckn;
  DW.vfrom = (float)vfrom; DW.hs = (float)hs; DW.tp = (float)tp;
}
/* DNV's sea for a wind: OUT hs, tp */
K void api_dp_sea(double wkn) { DpWx w = { 0, (float)wkn, 0, 0, 0, 0, 0 }; dpo_waves_auto(&w); OUT[0] = w.hs; OUT[1] = w.tp; }
static void dp_out(void) {
  DpRes *r = &DR;
  double *o = OUT;
  o[0] = r->hdg; o[1] = r->face; o[2] = r->px; o[3] = r->py; o[4] = r->wleft; o[5] = r->vleft; o[6] = r->fmag; o[7] = r->fto;
  o[8] = r->blow; o[9] = r->held_lim; o[10] = r->held_full; o[11] = r->worst; o[12] = r->worst_thr; o[13] = r->fail_held;
  o[14] = r->fail_grp; o[15] = r->fail_use; o[16] = r->rank; o[17] = dpo_face_normal(&DPP, r->face);
  float h[7][2];
  dpo_hull(&DV, &DPP, r->hdg, r->px, r->py, h);
  for (int i = 0; i < 7; i++) { o[20 + 2 * i] = h[i][0]; o[21 + 2 * i] = h[i][1]; }
  for (int i = 0; i < DV.nthr; i++) { o[40 + i] = r->use[i]; o[60 + i] = r->fail_wuse[i]; }
  o[34] = r->env[0]; o[35] = r->env[1]; o[36] = r->env[2];
  for (int i = 0; i < DV.nthr; i++) { o[80 + 2 * i] = r->f[i][0]; o[81 + 2 * i] = r->f[i][1]; o[104 + 2 * i] = r->ff[i][0]; o[105 + 2 * i] = r->ff[i][1]; }
}
K int api_dp_best(void) { dp_abort = 0; int n = dpo_best(&DV, &DPP, &DW, 5, &DR, NULL); dp_out(); return n; }
K void api_dp_eval(int face, double hdg) { dpo_eval(&DV, &DPP, &DW, face & 3, (float)hdg, &DR); dp_out(); }
K double api_dp_limit(void) { return dpo_limit_wind(&DV, &DPP, &DW, DR.face, DR.hdg); }
/* each heading the side allows on a face (every 5 degrees): OUT [heading, rank, worst] x n; n */
K int api_dp_sweep(int face) {
  int n = 0;
  DpRes r;
  for (int h = 0; h < 360; h += 5) {
    dpo_eval(&DV, &DPP, &DW, face & 3, (float)h, &r);
    OUT[3 * n] = h; OUT[3 * n + 1] = r.rank; OUT[3 * n + 2] = r.worst; n++;
  }
  return n;
}
/* the current at a position, as the calculator's DP OPS: the atlas (springs, the wind added), else a stream station
   within 30 NM. OUT: set, kn; the source's name */
static char DCSRC[48];
K const char *api_dp_csrc(void) { return DCSRC; }
K int api_dp_current(double lat, double lon, double jd, double wdir, double wkn) {
  OUT[0] = OUT[1] = 0;
  if (api_flow(lat, lon, jd, wdir, wkn, 0) && OUT[34] >= 0) {
    double se = OUT[34], kn = OUT[35];
    OUT[0] = se; OUT[1] = kn;
    snprintf(DCSRC, sizeof DCSRC, "your FLOW atlas");
    return 1;
  }
  int n = api_tide_count(), best = -1;
  double bd = 30;
  for (int i = 0; i < n; i++) {
    const TStation *s = station(i, NULL);
    if (!s || s->kind == TK_TIDE) continue;
    double d = 60 * acos(fmin(1, sin(lat * M_PI / 180) * sin(s->lat * M_PI / 180) +
                         cos(lat * M_PI / 180) * cos(s->lat * M_PI / 180) * cos((lon - s->lon) * M_PI / 180))) * 180 / M_PI;
    if (d < bd) { bd = d; best = i; }
  }
  if (best < 0) { snprintf(DCSRC, sizeof DCSRC, "no stream within 30 NM"); return 0; }
  api_stream_now(best, jd);
  double rate = OUT[0], set = OUT[1];
  OUT[0] = set; OUT[1] = fabs(rate);
  snprintf(DCSRC, sizeof DCSRC, "%.28s, %.0f NM", station(best, NULL)->name, bd);
  return 1;
}
