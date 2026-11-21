/*
 * NAV47 for the SwissMicros DM42n (DMCP5 program, .pg5)
 * Almanac 47 + Zones 47 + Tide 47 (with FLOW) in one native program.
 * Copyright (C) 2026 Victor Valdes <valdes.vj@gmail.com> - GPL-3.0-or-later (see LICENSE).
 *
 * DOES NOT REPLACE THE NAUTICAL ALMANAC OR OFFICIAL TIDE TABLES. Not for use as the primary means of navigation.
 */
#include <main.h>
#include <dmcp.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "astro.h"
#include "almdata.h"
#include "gfx.h"
#include "tide.h"
#include "tidefile.h"
#include "zones.h"
#include "c47scr.h"
#include "views.h"
#include "ntext.h"
#include "route.h"
#include "usbview.h"
#include "usbser.h"
#include "moon47.h"
#include "geo_nav.h"
#include "map47.h"
#include "dpops.h"

#define DIR       "/NAV47"
#define TIDEDIR   "/NAV47/TIDES"
#define CFGFILE   "/NAV47/NAV47.CFG"
#define ROUTEFILE "/NAV47/ROUTE.TXT"
#define MENU_Y    (LCD_Y - LCD_MENU_LINES)     /* 208 */
#define LH        9                            /* tiny font line pitch */

/* ------------------------------------------------------------------ */
/*  State (heap: a DMCP5 program has 8 KB of static RAM)              */
/* ------------------------------------------------------------------ */
enum { PG_VALM, PG_VSPLIT, PG_VSKY, PG_VALLSKY, PG_VMOON, PG_VANIM, PG_VSKY2,
       PG_TIDE, PG_STATION, PG_ROUTE, PG_ZONES, PG_SET, PG_FILES, PG_INFO, PG_PICK, PG_PLAN, PG_CMP, PG_CHART, PG_VOY, PG_ETA, PG_ETAT, PG_DP, PG_DPIN };
#define IS_VIEW(p) ((p) <= PG_VSKY2)

typedef struct {
  double lat, lon;          /* degrees, N+ E+ */
  double tz;                /* the calculator clock's offset from UT, hours */
  int    live;              /* 1: time from the clock, 0: the fixed UT below */
  double fixed_jd;          /* UT */
  double wdir, wkn;         /* wind (from), knots, for FLOW */
  int    tsrc, treg, tst;   /* tide station: source (0 public, 1 private, 2 file), region, station */
  int    tauto;             /* 1: TIDE and T&F take the stations nearest the position (AUTO) */
  int    zone, city;        /* ZONES stop and city */
  char   files[4][40];      /* loaded tide files, reloaded at start */
  int    nfiles;
  int    south;             /* MOON47: the view from the south */
  int    gc;                /* ROUTE: great circle instead of rhumb line */
  int    lt;                /* the screens' times: 1 local (the clock TZ; a tide station's own zone), 0 UT */
  int    dcauto, dsauto;    /* DP OPS: the current from FLOW, the sea from the wind (DNV); else typed: */
  double dcset, dckn, dsfrom, dhs, dtp;
} Cfg;

typedef struct { int src, reg, st; } Row;
#define MAXROWS 400
#define MAXWP   100

typedef struct {
  Cfg cfg;
  Al al;                    /* the views' almanac (c47screen.py Almanac) */
  double vjd;               /* its time */
  int valid;
  Row rows[MAXROWS];        /* the station list */
  TDay tday;
  TRegion *fr;              /* regions loaded from files */
  int nfr;
  FlowAtlas ffl;            /* atlas loaded from a file */
  int have_ffl;
  char msg[64];
  double wlat[MAXWP], wlon[MAXWP];   /* ROUTE waypoints */
  int nwp;
  double leg_crs[MAXWP], leg_nm[MAXWP];   /* ROUTE: each leg's course and distance (cache) */
} Nav;

static Nav *N;
static int page = PG_VALM, menu_page, shift, vframe;
static double toff;                      /* hours added to the time by the arrows */
static int edit_field = -1, sel_field, pick_cursor, flow_hw_step, flow_day_step;
static int split_first, st_day_off, pick_back;   /* SPLIT scroll; TIDE (station) day; where STATNS returns */
static int cmp_edit = -1;                 /* COMPARE: editing CUSTOM 0 a, 1 1/f */
static double cmp_a;
static char cmp_msg[32];
static double voy_kn = 12;
static double voy_etd0;                   /* VOYAGE: the departure kept, UT JD (ROUTE.TXT ETD); 0 = now */                /* VOYAGE: the speed, knots (ROUTE.TXT SPEED) */
static int wp_cur, wp_edit = -1, wp_new;   /* wp_new: the edit is of a waypoint just added (EXIT removes it) */          /* ROUTE: the cursor; editing 0 lat, 1 lon */
static double wp_lat;
static char ebuf[16];
static int elen;

static void cfg_save(void);

/* ------------------------------------------------------------------ */
/*  Time                                                              */
/* ------------------------------------------------------------------ */
static double clock_jd_local(void) {
  tm_t tm;
  dt_t dt;
  rtc_read(&tm, &dt);
  return jd_ymd(dt.year, dt.month, dt.day, tm.hour + tm.min / 60.0 + tm.sec / 3600.0);
}

static double ut_now(void) {
  double base = N->cfg.live ? clock_jd_local() - N->cfg.tz / 24.0 : N->cfg.fixed_jd;
  return base + toff / 24.0;
}

/* the zone of the times shown (SET TIMES, +/- on any page): the clock TZ for local time, 0 for UT */
static double show_tz(void) { return N->cfg.lt ? N->cfg.tz : 0.0; }
static const char *show_name(void) { return N->cfg.lt ? "LT" : "UT"; }

/* ------------------------------------------------------------------ */
/*  Formatting                                                        */
/* ------------------------------------------------------------------ */
static void fmt_dm(char *s, double deg, int ddig) {      /* ddd°mm.m' */
  double a = fabs(deg);
  int d = (int)a;
  double m = (a - d) * 60.0;
  if (m >= 59.95) { d++; m = 0.0; }
  sprintf(s, ddig == 3 ? "%03d" SYM_DEG "%04.1f" SYM_MIN : "%02d" SYM_DEG "%04.1f" SYM_MIN, d, m);
}

static void fmt_lat(char *s, double v) { fmt_dm(s, v, 2); strcat(s, v < 0 ? "S" : "N"); }
static void fmt_lon(char *s, double v) { fmt_dm(s, v, 3); strcat(s, v < 0 ? "W" : "E"); }

static void fmt_hm(char *s, double h) {                   /* hours -> hh:mm, 99 -> --:-- */
  if (h >= 98) { strcpy(s, "--:--"); return; }
  h = fmod(h + 48.0, 24.0);
  int m = (int)floor(h * 60.0 + 0.5);
  if (m >= 1440) m -= 1440;
  sprintf(s, "%02d:%02d", m / 60, m % 60);
}

static void fmt_off(char *s, double o) {                  /* +3, -5:30 */
  char sg = o < 0 ? '-' : '+';
  o = fabs(o);
  int h = (int)o, m = (int)floor((o - h) * 60 + 0.5);
  if (m) sprintf(s, "%c%d:%02d", sg, h, m); else sprintf(s, "%c%d", sg, h);
}

/* the top bar, the same on every page (views.c top_bar): the date and time shown (UT or LT) on the left, the
   position on the right, the page's name in the middle; bar_at: at another instant (a tide day stepped) */
static void bar_at(double ut, const char *name) { top_bar(ut + show_tz() / 24.0, N->cfg.lt, N->cfg.lat, N->cfg.lon, name); }
static void header(const char *name) { bar_at(ut_now(), name); }

/* the degree sign of the system font text */
static void fmt_dmn(char *s, double deg, int ddig, char hemi) {     /* 26°30.0N 050°10.0E */
  double a = fabs(deg);
  int d = (int)a;
  double m = (a - d) * 60.0;
  if (m >= 59.95) { d++; m = 0.0; }
  sprintf(s, ddig == 3 ? "%03d" NT_DEG "%04.1f%c" : "%02d" NT_DEG "%04.1f%c", d, m, hemi);
}

static const char *MON[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
static const char *WDAY[7] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };


/* ------------------------------------------------------------------ */
/*  ZONES (as ZONE47)                                                 */
/* ------------------------------------------------------------------ */
static void draw_zones(void) {
  double ut = ut_now();
  const Zone *z = &ZONES[N->cfg.zone];
  if (N->cfg.city >= z->n) N->cfg.city = 0;
  const City *c = &z->c[N->cfg.city];
  int on;
  double off = city_offset(z, c, ut, &on);
  char s[64], t[16], o[12];
  int y, m, d;
  double h;
  jd_to_ymd(ut + off / 24.0, &y, &m, &d, &h);
  long n = jdn(y, m, d);
  header("ZONES");                                       /* the clock (UT or LT) in the bar */
  fmt_off(o, off);                                        /* the zone's own time, and its difference from the bar */
  sprintf(s, "UTC%s", off == 0 ? "" : o);
  nt_inv(2, 17, s);
  fmt_hm(t, h);
  nt(100, 17, t);
  sprintf(s, "%s %d %s", WDAY[(n + 1) % 7], d, MON[m - 1]);
  nt(160, 17, s);
  double dd = off - show_tz();
  if (dd == 0) strcpy(s, "same time"); else { fmt_off(o, dd); sprintf(s, "%s h", o); }
  nt_r(LCD_X - 2, 17, s);
  /* map: 120 x 45 dots, 3 px each, from x 20; zone column c (5 dots, 15 deg) is UTC c - 11 */
  int mx = 20, my = 36;
  for (int j = 0; j < MAP_NCOL; j++) {
    int hl = (j / 5) == z->col;
    if (hl) gfx_rect(mx + 3 * j, my, 3, 3 * MAP_NROW, 1);
    for (int i = 0; i < MAP_NROW; i++) {
      int land = (int)((MAP_COLS[j] >> i) & 1ULL);
      if (land) gfx_rect(mx + 3 * j, my + 3 * i, 2, 2, hl ? 0 : 1);
    }
  }
  int ty = my + 3 * MAP_NROW + 1;
  for (int k = 0; k <= 24; k++) gfx_rect(mx + 15 * k - 1, ty, 1, 2, 1);
  for (int k = -10; k <= 12; k += 2) {
    sprintf(s, "%+d", k);
    if (!k) strcpy(s, "0");
    draw_text(&TINY, mx + 15 * (k + 11) + 7 - text_width(&TINY, s) / 2, ty + 2, s, 0);
  }
  if (c->kind == DST_IRREGULAR) sprintf(s, "%s %s  %d/%d  DST varies", c->name, c->cc, N->cfg.city + 1, z->n);
  else if (c->kind == DST_RULE) {
    fmt_off(o, c->dst);
    sprintf(s, on ? "%s %s  %d/%d  summer %s h" : "%s %s  %d/%d  winter, DST %s h", c->name, c->cc, N->cfg.city + 1, z->n, o);
  } else sprintf(s, "%s %s  %d/%d", c->name, c->cc, N->cfg.city + 1, z->n);
  nt(2, 192, s);
}

/* ------------------------------------------------------------------ */
/*  Tide stations: public, private (compiled), files                  */
/* ------------------------------------------------------------------ */
static int src_nreg(int src) { return src == 0 ? PUB_NREGIONS : src == 1 ? PRIV_NREGIONS : N->nfr; }
static const TRegion *src_reg(int src, int r) { return src == 0 ? &PUB_REGIONS[r] : src == 1 ? &PRIV_REGIONS[r] : &N->fr[r]; }

/* the station nearest (lat, lon) of a kind (want 0: a tide, 1: a stream), every source; its distance (NM) in *nm */
static const TStation *nearest_station(double lat, double lon, int want, double *nm, int *src, int *reg, int *st) {
  const TStation *best = NULL;
  double bd = 1e18;
  for (int sc = 0; sc < 3; sc++)
    for (int r = 0; r < src_nreg(sc); r++) {
      const TRegion *R = src_reg(sc, r);
      for (int i = 0; i < R->n; i++) {
        const TStation *p = &R->st[i];
        if ((p->kind != TK_TIDE) != want) continue;
        double dd = 60 * hypot(p->lat - lat, (p->lon - lon) * dcos(lat));
        if (dd < bd) { bd = dd; best = p; if (src) { *src = sc; *reg = r; *st = i; } }
      }
    }
  if (nm) *nm = bd;
  return best;
}
static const TStation *cur_station(void) {
  Cfg *c = &N->cfg;
  if (c->tauto) {                                        /* AUTO: the tide station nearest the position */
    int sc, r, i;
    if (nearest_station(c->lat, c->lon, 0, NULL, &sc, &r, &i)) { c->tsrc = sc; c->treg = r; c->tst = i; }
  }
  if (c->tsrc < 0 || c->tsrc > 2 || c->treg >= src_nreg(c->tsrc)) { c->tsrc = 0; c->treg = 0; c->tst = 0; }
  const TRegion *r = src_reg(c->tsrc, c->treg);
  if (c->tst >= r->n) c->tst = 0;
  return &r->st[c->tst];
}

/* the station list as rows: region headings and stations */
static int list_rows(Row *rows, int max) {
  static const int ORDER[3] = { 1, 2, 0 };         /* private first, then files, then public */
  int n = 0;
  for (int o = 0; o < 3; o++)
    for (int src = ORDER[o], r = 0; r < src_nreg(src); r++) {
      if (n < max) { rows[n].src = src; rows[n].reg = r; rows[n].st = -1; n++; }
      for (int s = 0; s < src_reg(src, r)->n; s++)
        if (n < max) { rows[n].src = src; rows[n].reg = r; rows[n].st = s; n++; }
    }
  return n;
}

static void draw_pick(void) {
  Row *rows = N->rows;
  int n = list_rows(rows, MAXROWS);
  header("STATIONS");
  draw_text_r(&TINY, LCD_X - 2, 16, "8 2 move  ENTER choose", 0);
  if (pick_cursor >= n) pick_cursor = n - 1;
  if (pick_cursor < 0) pick_cursor = 0;
  if (rows[pick_cursor].st < 0 && pick_cursor + 1 < n) pick_cursor++;
  int vis = (MENU_Y - 24) / LH;
  int top = pick_cursor - vis / 2;
  if (top > n - vis) top = n - vis;
  if (top < 0) top = 0;
  for (int i = top, y = 24; i < n && i < top + vis; i++, y += LH) {
    const TRegion *R = src_reg(rows[i].src, rows[i].reg);
    char s[128];
    if (rows[i].st < 0) {
      sprintf(s, "%s  %s", rows[i].src == 0 ? "PUBLIC" : rows[i].src == 1 ? "PRIVATE (compiled)" : "FILE", R->title);
      draw_text(&TINY, 2, y, s, 0);
      gfx_rect(2, y + 8, LCD_X - 4, 1, 1);
    } else {
      const TStation *st = &R->st[rows[i].st];
      char a[24], b[24];
      fmt_lat(a, st->lat);
      fmt_lon(b, st->lon);
      sprintf(s, "  %-30.30s %-6s %s %s", st->name, st->kind == TK_TIDE ? "tide" : st->kind == TK_RECT ? "stream" : "rotary", a, b);
      draw_text(&TINY, 2, y, s, i == pick_cursor);
    }
  }
}

static void pick_move(int d) {
  Row *rows = N->rows;
  int n = list_rows(rows, MAXROWS), i = pick_cursor;
  do { i += d; } while (i >= 0 && i < n && rows[i].st < 0);
  if (i >= 0 && i < n) pick_cursor = i;
}

static void pick_choose(void) {
  Row *rows = N->rows;
  int n = list_rows(rows, MAXROWS);
  if (pick_cursor < n && rows[pick_cursor].st >= 0) {
    N->cfg.tsrc = rows[pick_cursor].src; N->cfg.treg = rows[pick_cursor].reg; N->cfg.tst = rows[pick_cursor].st;
    N->cfg.tauto = 0;                                    /* a station chosen: no longer AUTO */
    st_day_off = 0;
  }
  page = PG_STATION;
}

static void pick_open(void) {
  Row *rows = N->rows;
  int n = list_rows(rows, MAXROWS);
  for (int i = 0; i < n; i++)
    if (rows[i].src == N->cfg.tsrc && rows[i].reg == N->cfg.treg && rows[i].st == N->cfg.tst) pick_cursor = i;
  pick_back = page == PG_PICK ? pick_back : page;
  page = PG_PICK;
}

/* ------------------------------------------------------------------ */
/*  FLOW (Tide 47's FLOW47 / ARTF47)                                  */
/* ------------------------------------------------------------------ */
static const FlowAtlas *atlas(void) {
  if (N->have_ffl) return &N->ffl;
  if (FLOW_ATLAS_PRESENT) return &FLOW_ATLAS;
  return NULL;
}

static const TStation *find_station(const char *name, int *region_src, int *region) {
  for (int src = 1; src >= 0; src--) {                  /* private first, then public */
    for (int r = 0; r < src_nreg(src); r++)
      for (int s = 0; s < src_reg(src, r)->n; s++)
        if (strstr(src_reg(src, r)->st[s].name, name)) { *region_src = src; *region = r; return &src_reg(src, r)->st[s]; }
  }
  for (int r = 0; r < N->nfr; r++)
    for (int s = 0; s < N->fr[r].n; s++)
      if (strstr(N->fr[r].st[s].name, name)) { *region_src = 2; *region = r; return &N->fr[r].st[s]; }
  return NULL;
}

static void arrow(int cx, int cy, double set, double len) {
  int x1 = cx + (int)lround(len * dsin(set)), y1 = cy - (int)lround(len * dcos(set));
  gfx_line(cx, cy, x1, y1, 1);
  int ax = x1 - (int)lround(3 * dsin(set - 30)), ay = y1 + (int)lround(3 * dcos(set - 30));
  int bx = x1 - (int)lround(3 * dsin(set + 30)), by = y1 + (int)lround(3 * dcos(set + 30));
  gfx_line(x1, y1, ax, ay, 1);
  gfx_line(x1, y1, bx, by, 1);
}

/* the station's local time zone (with US summer time) and its local date and hour at UT jd */
static double st_local(const TStation *st, double ut, long *n, double *h) {
  int y, m, d;
  double tz = st->tz;
  jd_to_ymd(ut + tz / 24.0, &y, &m, &d, h);
  *n = jdn(y, m, d);
  if (tide_dst(st, *n)) { tz += 1; jd_to_ymd(ut + tz / 24.0, &y, &m, &d, h); *n = jdn(y, m, d); }
  return tz;
}
static void norm_day(long *n, double *h) {
  while (*h < 0) { *h += 24; (*n)--; }
  while (*h >= 24) { *h -= 24; (*n)++; }
}

/* the hour axis of a day graph: graph hours 0..24 of the graph's own zone; the ticks and labels at the shown hours u
   (UT or LT, every step, labelled every 6), at graph hour u + shift (shift = the graph's zone - the shown zone) */
static void hour_ticks(int gx, int gw, int y, double shift, int step, int ty, int th, int ly) {
  char s[8];
  for (int u = (int)ceil(-shift / step - 1e-9) * step; u + shift <= 24 + 1e-9; u += step) {
    int x = gx + (int)lround((u + shift) / 24.0 * gw);
    gfx_rect(x, y + ty, 1, th, 1);
    if (((u % 6) + 6) % 6 == 0) {
      sprintf(s, "%02d", ((u % 24) + 24) % 24);
      draw_text(&TINY, x - 5, y + ly, s, 0);
    }
  }
}

/* TIDE: Tide 47's ARTF47 page. Left: the hours HW-6 ... HW+6 of the reference station's High Water, with the
   stream of the FLOW atlas at the position (springs scaled, the wind added), or the station's own height or stream.
   Right: the tide of the port nearest the position (the chosen station without an atlas): the day's curve, the
   table's 13 hours as a bar, now dotted, the high and low waters. */
static void draw_tide(void) {
  char s[96], t[8];
  double ut = ut_now();
  const FlowAtlas *fl = atlas();
  int rs = 0, rr = 0;
  const TStation *ref = fl ? find_station(fl->ref, &rs, &rr) : NULL;
  double FU[13], FV[13];
  int have_flow = ref && flow_cell(fl, N->cfg.lat, N->cfg.lon, FU, FV);
  const TStation *st = ref ? ref : cur_station();
  if (N->cfg.tauto && !have_flow) {                      /* AUTO, out of the atlas: a stream station near, else the tide */
    double dn;
    const TStation *ss = nearest_station(N->cfg.lat, N->cfg.lon, 1, &dn, NULL, NULL, NULL);
    st = ss && dn <= 30 ? ss : cur_station();
  }
  int tidal = st->kind == TK_TIDE;
  long n0;
  double h0, tzl = st_local(st, ut, &n0, &h0);
  /* the table's hours: t0 + k - 6 (local hours of day on) */
  long on = n0 + flow_day_step;
  int om = 0;
  double t0, hh = 0, rng = 0, fac = 1;
  if (tidal) {
    flow_pick(st, on, flow_day_step ? 12.0 : h0, &on, &om);
    for (int k = 0; k < flow_hw_step; k++) flow_step(st, on, om, 1, &on, &om);
    for (int k = 0; k > flow_hw_step; k--) flow_step(st, on, om, -1, &on, &om);
    flow_cycle(st, on, om, &t0, &hh, &rng, &fac);
  } else t0 = floor(flow_day_step ? 12.0 : h0) + 13 * flow_hw_step;
  double off_now = ((double)(n0 - on) * 24.0 + h0) - t0;
  double U[13], V[13], val[13];
  if (have_flow) flow_rows(FU, FV, fac, N->cfg.wdir, N->cfg.wkn, U, V);
  else
    for (int k = 0; k < 13; k++) {
      long nn = on;
      double hk = t0 + k - 6, dir;
      norm_day(&nn, &hk);
      stream_now(st, nn, hk, &val[k], &dir);
      if (!tidal) { U[k] = fabs(val[k]) * dsin(dir); V[k] = fabs(val[k]) * dcos(dir); }
    }
  int vec = have_flow || !tidal;
  bar_at(ut + (double)(on - n0), "TIDE & FLOW");         /* the bar: the table's day */
  for (int y = 17; y < MENU_Y; y += 2) lcd_fill_rect(200, (uint32_t)y, 1, 1, 1);
  /* left: the table */
  int mark = off_now >= -6.5 && off_now < 6.5 ? (int)floor(off_now + 6.5) : -1;
  int first = mark < 0 ? 1 : mark - 5;
  if (first < 0) first = 0;
  if (first > 2) first = 2;
  nt(tidal ? 12 : 4, 16, tidal ? "HW" : "  ");
  nt(46, 16, show_name());
  if (vec) { nt(114, 16, "SET"); nt_r(196, 16, "KN"); } else nt_r(196, 16, "HEIGHT");
  double dclk = show_tz() - tzl;
  for (int r = 0; r < 11; r++) {
    int k = first + r, y = 32 + NT_ROW * r;
    if (k == mark) nt(0, y, ">");
    if (tidal) {
      if (k == 6) strcpy(s, "HW"); else sprintf(s, "%+d", k - 6);
      nt_r(34, y, s);
    }
    fmt_hm(t, t0 + k - 6 + dclk);
    nt(42, y, t);
    if (vec) {
      double set, kn;
      setkn(U[k], V[k], &set, &kn);
      double len = 6 + 6 * fmin(kn, 1.5) / 1.5;               /* centred in its cell */
      arrow(100 - (int)lround(len / 2 * dsin(set)), y + 7 + (int)lround(len / 2 * dcos(set)), set, len);
      sprintf(s, "%03.0f" NT_DEG, set);
      nt(112, y, s);
      sprintf(s, "%.2f", kn);
      nt_r(196, y, s);
    } else {
      sprintf(s, "%.2f m", val[k]);
      nt_r(196, y, s);
    }
  }
  /* right: now */
  int X = 206;
  if (off_now >= -6 && off_now <= 6) {
    fmt_hm(t, h0 + show_tz() - tzl);
    if (vec) {
      double u, v, se, kn;
      flow_now(U, V, off_now, &u, &v);
      setkn(u, v, &se, &kn);
      sprintf(s, "NOW %s %03.0f" NT_DEG " %.2f", t, se, kn);
    } else sprintf(s, "NOW %s %.2f m", t, val[mark < 0 ? 6 : mark]);
    nt(X, 16, s);
  }
  /* the tide station of the right side */
  const TStation *rst = st;
  double dist = -1;
  if (ref) {
    const TRegion *reg = src_reg(rs, rr);
    double bd = 1e9;
    for (int i = 0; i < reg->n; i++) {
      const TStation *p = &reg->st[i];
      if (p->kind != TK_TIDE) continue;
      double dd = 60 * hypot(p->lat - N->cfg.lat, (p->lon - N->cfg.lon) * dcos(N->cfg.lat));
      if (dd < bd) { bd = dd; rst = p; }
    }
    if (N->cfg.tauto) { double dn; const TStation *p = nearest_station(N->cfg.lat, N->cfg.lon, 0, &dn, NULL, NULL, NULL); if (p) { rst = p; bd = dn; } }
    dist = bd;
  }
  /* its local day holding the table's High Water */
  long rn;
  double rh, rtz = st_local(rst, (double)on - 0.5 + t0 / 24.0 - tzl / 24.0, &rn, &rh);
  int stream = rst->kind != TK_TIDE;
  TDay *D = &N->tday;
  tide_day(rst, rn, stream, D);
  {
    char nm[24];
    int room = 192 - (dist >= 0 ? 6 * nt_cw() : 0);
    int nc = room / nt_cw();
    snprintf(nm, sizeof nm, "%.*s", nc > 20 ? 20 : nc, rst->name);
    char *c = strchr(nm, ',');
    if (c) *c = 0;
    if (dist >= 0) sprintf(s, "%s %.0fnm", nm, dist); else strcpy(s, nm);
    nt(X, 32, s);
  }
  int gx = 210, gw = 180, gy = 52, gh = 58;
  double lo = 1e9, hi = -1e9;
  for (int k = 0; k < 73; k++) { if (D->curve[k] < lo) lo = D->curve[k]; if (D->curve[k] > hi) hi = D->curve[k]; }
  if (stream && rst->kind == TK_RECT) { double m = fmax(fabs(lo), fabs(hi)); lo = -m; hi = m; }
  if (hi - lo < 0.2) { hi += 0.1; lo -= 0.1; }
  int px = -1, py = -1;
  for (int k = 0; k < 73; k++) {
    int x = gx + k * gw / 72, y = gy + (int)((hi - D->curve[k]) / (hi - lo) * gh);
    if (px >= 0) gfx_line(px, py, x, y, 1);
    px = x; py = y;
  }
  for (int i = 0; i < D->nev; i++) {                          /* + on the high and low waters */
    int x = gx + (int)(D->ev[i].t / 24.0 * gw), y = gy + (int)((hi - D->ev[i].val) / (hi - lo) * gh);
    if (stream) continue;
    gfx_line(x - 3, y, x + 3, y, 1);
    gfx_line(x, y - 3, x, y + 3, 1);
  }
  int ay = gy + gh + 6;
  gfx_hline_dotted(gx, gx + gw, ay, 2);
  hour_ticks(gx, gw, ay, rtz - show_tz(), 6, -1, 3, 3);
  {                                                             /* the table's 13 hours */
    double b0 = (double)(on - rn) * 24 + t0 - 6 + rtz - tzl, b1 = b0 + 12;
    if (b0 < 0) b0 = 0;
    if (b1 > 24) b1 = 24;
    if (b1 > b0) gfx_rect(gx + (int)(b0 / 24 * gw), ay + 1, (int)((b1 - b0) / 24 * gw) + 1, 2, 1);
    double nh = (ut - ((double)rn - 0.5 - rtz / 24.0)) * 24.0;   /* now, on the station's day */
    if (nh >= 0 && nh <= 24) for (int y = gy - 4; y < ay; y += 2) gfx_pixel(gx + (int)(nh / 24 * gw), y, 1);
  }
  int ey = 128;
  for (int i = 0; i < D->nev && ey <= 192; i++, ey += NT_ROW) {
    const TEvent *e = &D->ev[i];
    fmt_hm(t, e->t + show_tz() - rtz);
    if (!stream) sprintf(s, "%s %s %5.2f m", e->kind > 0 ? "HW" : "LW", t, e->val);
    else if (rst->kind == TK_RECT) {
      if (e->kind < 0) sprintf(s, "SLACK %s", t);
      else sprintf(s, "%s %s %.1fkn", e->val > 0 ? "FLOOD" : "EBB", t, fabs(e->val));
    } else sprintf(s, "%s %s %.1fkn", e->kind > 0 ? "MAX" : "MIN", t, e->val);
    nt(X + 6, ey, s);
  }
  if (ey <= 192) {
    if (have_flow) sprintf(s, "RANGE x%.2f%s", fac, N->cfg.wkn > 0 ? " +WIND" : "");
    else if (fl && ref) strcpy(s, "NO STREAM HERE");
    else strcpy(s, "NO STREAM ATLAS");
    nt(X, 192, s);
  }
}
/* ------------------------------------------------------------------ */
/*  TIDE: the chosen station's day (TIDES of Tide 47)                 */
/* ------------------------------------------------------------------ */
static void station_step(int d) {                /* the next / previous station of the list (8 2) */
  Row *rows = N->rows;
  int n = list_rows(rows, MAXROWS), cur = -1;
  for (int i = 0; i < n; i++)
    if (rows[i].st >= 0 && rows[i].src == N->cfg.tsrc && rows[i].reg == N->cfg.treg && rows[i].st == N->cfg.tst) cur = i;
  int i = cur;
  do { i = (i + d + n) % n; } while (rows[i].st < 0 && i != cur);
  if (rows[i].st >= 0) { N->cfg.tsrc = rows[i].src; N->cfg.treg = rows[i].reg; N->cfg.tst = rows[i].st; N->cfg.tauto = 0; }
}

static void draw_station(void) {
  const TStation *st = cur_station();
  int stream = st->kind != TK_TIDE, rot = st->kind == TK_ROT;
  double ut = ut_now();
  long n0;
  double h0, tz = st_local(st, ut, &n0, &h0);
  long n = n0 + st_day_off;
  TDay *D = &N->tday;
  tide_day(st, n, stream, D);
  char s[96], t[8], nm[40];
  int y, m, d;
  cal(n, &y, &m, &d);
  bar_at(ut + st_day_off, "TIDE");                       /* the bar: the day shown */
  (void)y; (void)m; (void)d;
  /* the times: the station's own zone (LT) or UT; the day and its graph are the station's day */
  char o[12], z[24];
  double sh = N->cfg.lt ? 0.0 : D->tz;                   /* station hours -> shown hours: subtract sh */
  fmt_off(o, D->tz);
  if (N->cfg.lt) sprintf(z, "UTC%s", o); else sprintf(z, "UT (day UTC%s)", o);
  if (!stream) sprintf(s, "m above %s  %s%s", st->datum, z, N->cfg.tsrc == 1 ? "  PRIV" : N->cfg.tsrc == 2 ? "  FILE" : "");
  else if (!rot) sprintf(s, "kn  fl %03.0f" NT_DEG " eb %03.0f" NT_DEG "  %s", st->flood, st->ebb, z);
  else sprintf(s, "kn rotary  %s", z);
  int room = N->cfg.tauto ? 248 - nt_w("  AUTO") : 248;
  if (nt_w(s) > room) { char *c = strstr(s, "  UT"); if (!c) c = strstr(s, "  UTC"); if (c) *c = 0; }
  if (N->cfg.tauto) strcat(s, "  AUTO");                /* the station nearest the position */
  nt(2, 17, s);
  /* the station's name: the first row of the column of the HW / LW times */
  int ex = 254, ey = 36;
  snprintf(nm, sizeof nm, "%s", st->name);
  if (nt_w(nm) > LCD_X - 1 - ex && strchr(nm, ',')) *strchr(nm, ',') = 0;     /* "Mina Salman, Bahrain": the place */
  while (nm[0] && nt_w(nm) > LCD_X - 1 - ex) nm[strlen(nm) - 1] = 0;
  nt(ex, 17, nm);
  /* the day's curve */
  int gx = 24, gy = 38, gw = 222, gh = 104;
  double lo = 1e9, hi = -1e9;
  for (int k = 0; k < 73; k++) { if (D->curve[k] < lo) lo = D->curve[k]; if (D->curve[k] > hi) hi = D->curve[k]; }
  if (stream && !rot) { double a = fmax(fabs(lo), fabs(hi)); lo = -a; hi = a; }
  if (rot) lo = 0;
  if (hi - lo < 0.2) { hi += 0.1; lo -= 0.1; }
  double pad = (hi - lo) * 0.06;
  lo -= pad; hi += pad;
  gfx_frame(gx, gy, gw + 1, gh + 1);
  hour_ticks(gx, gw, gy + gh, sh, 3, 0, 3, 4);
  sprintf(s, "%.1f", hi); draw_text_r(&TINY, gx - 2, gy, s, 0);
  sprintf(s, "%.1f", lo); draw_text_r(&TINY, gx - 2, gy + gh - 8, s, 0);
  if (lo < 0 && hi > 0) gfx_hline_dotted(gx, gx + gw, gy + (int)(hi / (hi - lo) * gh), 3);
  int px = -1, py = -1;
  for (int k = 0; k < 73; k++) {
    int x = gx + k * gw / 72, yy = gy + (int)((hi - D->curve[k]) / (hi - lo) * gh);
    if (px >= 0) { gfx_line(px, py, x, yy, 1); gfx_line(px, py + 1, x, yy + 1, 1); }
    px = x; py = yy;
  }
  double now_h = -1;
  if (st_day_off == 0) {
    now_h = h0;
    int x = gx + (int)(now_h / 24.0 * gw);
    for (int yy = gy; yy < gy + gh; yy += 2) gfx_pixel(x, yy, 1);
  }
  /* the events */
  for (int i = 0; i < D->nev && ey < 120; i++, ey += NT_ROW) {
    const TEvent *e = &D->ev[i];
    fmt_hm(t, e->t - sh);
    if (!stream) sprintf(s, "%s %s %5.2f", e->kind > 0 ? "HW" : "LW", t, e->val);
    else if (!rot) {
      if (e->kind < 0) sprintf(s, "SLACK %s", t);
      else sprintf(s, "%s %s %.1f", e->val > 0 ? "FLOOD" : "EBB", t, fabs(e->val));
    } else sprintf(s, "%s %s %.1f %03.0f", e->kind > 0 ? "MAX" : "MIN", t, e->val, e->set);
    nt(ex, ey, s);
  }
  if (now_h >= 0) {
    double v, dir;
    stream_now(st, n, now_h, &v, &dir);
    fmt_hm(t, now_h - sh);
    if (!stream) sprintf(s, "NOW %s %.2f m", t, v);
    else sprintf(s, "NOW %s %.1f kn %03.0f" NT_DEG, t, fabs(v), dir);
    nt_inv(ex, 132, s);
  }
  /* springs and neaps: the day's range against the spring range (M2 + S2), and the next ones from the Moon */
  if (!stream) {
    double best = 0;
    for (int i = 0; i < D->nev; i++) {
      if (D->ev[i].kind < 0) continue;
      double sum = 0; int nl = 0;
      for (int k = i - 1; k <= i + 1; k += 2)
        if (k >= 0 && k < D->nev && D->ev[k].kind < 0) { sum += D->ev[k].val; nl++; }
      if (nl && D->ev[i].val - sum / nl > best) best = D->ev[i].val - sum / nl;
    }
    double fac = best / flow_spring(st);
    sprintf(s, "RANGE %.2f m  x%.2f of springs  %s", best, fac, fac >= 0.9 ? "SPRINGS" : fac <= 0.6 ? "NEAPS" : "");
    nt(2, 160, s);
  } else {
    double mx = 0;
    for (int i = 0; i < D->nev; i++) if (D->ev[i].kind > 0 && fabs(D->ev[i].val) > mx) mx = fabs(D->ev[i].val);
    sprintf(s, "strongest %.1f kn", mx);
    nt(2, 160, s);
  }
  {
    Moon47 p;
    moon47_page((double)n - 2.0 - tz / 24.0, 0, &p);      /* from 1.5 days before the day: its own springs / neaps count */
    double sp = 0, np = 0;
    for (int k = 0; k < 4; k++) {
      double tt = p.next_t[k] + 1.5;                            /* the tide follows the Moon by about 1-2 days */
      if ((p.next_k[k] == 0 || p.next_k[k] == 2) && !sp) sp = tt;
      if ((p.next_k[k] == 1 || p.next_k[k] == 3) && !np) np = tt;
    }
    int y2, m2, d2, y3, m3, d3;
    double hh;
    jd_to_ymd(sp + tz / 24.0, &y2, &m2, &d2, &hh);
    jd_to_ymd(np + tz / 24.0, &y3, &m3, &d3, &hh);
    sprintf(s, "springs ~%02d-%02d   neaps ~%02d-%02d  (Moon +1.5 d)", d2, m2, d3, m3);
    nt(2, 176, s);
  }
  nt(2, 192, N->cfg.tauto ? "AUTO  8 2 station 4 6 day 5 today ENTER list" : "8 2 station 4 6 day 5 today ENTER list 0 AUTO");
}

/* ------------------------------------------------------------------ */
/*  SET                                                               */
/* ------------------------------------------------------------------ */
enum { F_LAT, F_LON, F_TZ, F_LT, F_LIVE, F_DATE, F_TIME, F_WDIR, F_WKN, F_COUNT };
static const char *FNAME[F_COUNT] = { "LATITUDE", "LONGITUDE", "CLOCK TZ", "SHOW TIMES", "TIME", "DATE (UT)", "TIME (UT)",
                                      "WIND FROM", "WIND KN" };
static const char *FHELP[F_COUNT] = { "DD.MMm, +/- south", "DDD.MMm, +/- west", "clock - UT, hours",
                                      "ENTER, or +/- on any page", "ENTER switches", "YYYY.MMDD", "HH.MM", "degrees true", "knots" };

static void field_text(int f, char *s) {
  Cfg *c = &N->cfg;
  int y, m, d;
  double h;
  jd_to_ymd(c->fixed_jd + 0.5 / 1440, &y, &m, &d, &h);
  switch (f) {
  case F_LAT: fmt_lat(s, c->lat); break;
  case F_LON: fmt_lon(s, c->lon); break;
  case F_TZ: fmt_off(s, c->tz); break;
  case F_LT: strcpy(s, c->lt ? "LOCAL (LT)" : "UT"); break;
  case F_LIVE: strcpy(s, c->live ? "LIVE clock" : "FIXED"); break;
  case F_DATE: sprintf(s, "%04d-%02d-%02d", y, m, d); break;
  case F_TIME: { int mi = (int)floor(h * 60); sprintf(s, "%02d:%02d", mi / 60, mi % 60); break; }
  case F_WDIR: sprintf(s, "%03.0f" SYM_DEG, c->wdir); break;
  case F_WKN: sprintf(s, "%.0f kn", c->wkn); break;
  }
}

static double parse_dm(const char *s) {          /* DD.MMm -> degrees */
  double v = atof(s);
  return flow_dm(v);
}

static void field_commit(int f) {
  Cfg *c = &N->cfg;
  double v = atof(ebuf);
  int y, m, d;
  double h;
  jd_to_ymd(c->fixed_jd + 0.5 / 1440, &y, &m, &d, &h);
  switch (f) {
  case F_LAT: if (fabs(parse_dm(ebuf)) <= 90) c->lat = parse_dm(ebuf); break;
  case F_LON: if (fabs(parse_dm(ebuf)) <= 180) c->lon = parse_dm(ebuf); break;
  case F_TZ: if (fabs(v) <= 14) c->tz = flow_dm(v) ; break;
  case F_DATE: {
    int yy = (int)v, mm = (int)floor((v - yy) * 100 + 1e-6), dd = (int)lround(((v - yy) * 100 - mm) * 100);
    if (yy > 1900 && mm >= 1 && mm <= 12 && dd >= 1 && dd <= 31) c->fixed_jd = jd_ymd(yy, mm, dd, h);
    break;
  }
  case F_TIME: {
    double hh = flow_dm(v);
    if (hh >= 0 && hh < 24) c->fixed_jd = jd_ymd(y, m, d, hh);
    break;
  }
  case F_WDIR: if (v >= 0 && v <= 360) c->wdir = v; break;
  case F_WKN: if (v >= 0 && v < 100) c->wkn = v; break;
  }
  N->valid = 0;
}

static void draw_set(void) {
  header("SET");
  draw_text_r(&TINY, LCD_X - 2, 16, "8 2 field  ENTER edit", 0);
  char v[48];
  int y = 22;
  for (int f = 0; f < F_COUNT; f++) {
    if ((f == F_DATE || f == F_TIME) && N->cfg.live) continue;
    field_text(f, v);
    if (f == F_LAT || f == F_LON) {
      if (f == F_LAT) fmt_dmn(v, N->cfg.lat, 2, N->cfg.lat < 0 ? 'S' : 'N');
      else fmt_dmn(v, N->cfg.lon, 3, N->cfg.lon < 0 ? 'W' : 'E');
    }
    if (f == F_WDIR) sprintf(v, "%03.0f" NT_DEG, N->cfg.wdir);
    if (edit_field == f) sprintf(v, "%s_", ebuf);
    if (f == sel_field) nt_inv(6, y, FNAME[f]); else nt(6, y, FNAME[f]);
    nt(140, y, v);
    draw_text_r(&TINY, LCD_X - 2, y + 5, FHELP[f], 0);
    y += N->cfg.live ? 20 : 19;
  }
  draw_text(&TINY, 2, MENU_Y - LH, "As Almanac 47: 26.30 = 26" SYM_DEG "30'. Fixed time: date and UT (live off)", 0);
}

/* ------------------------------------------------------------------ */
/*  Files                                                             */
/* ------------------------------------------------------------------ */
static char *read_text(const char *path) {
  if (!sys_disk_ok() || !file_exists(path)) return NULL;
  FIL f;
  if (f_open(&f, path, FA_READ) != FR_OK) return NULL;
  UINT sz = (UINT)f_size(&f), br = 0;
  char *b = (char *)malloc(sz + 1);
  if (b) { f_read(&f, b, sz, &br); b[br] = 0; }
  f_close(&f);
  return b;
}

static int load_file(const char *path) {
  char *t = read_text(path);
  if (!t) { snprintf(N->msg, sizeof N->msg, "cannot read %s", path); return 0; }
  char err[48];
  int r;
  if (!strncmp(t, "NAV47 FLOW", 10)) {
    r = flw_parse(t, &N->ffl, err, sizeof err);
    if (r > 0) { N->have_ffl = 1; snprintf(N->msg, sizeof N->msg, "atlas: %d sea cells", r); }
  } else {
    r = tid_parse(t, &N->fr, &N->nfr, err, sizeof err);
    if (r > 0) snprintf(N->msg, sizeof N->msg, "%d stations loaded", r);
  }
  free(t);               /* the parsers copy what they keep */
  if (r <= 0) { snprintf(N->msg, sizeof N->msg, "%s", err); return 0; }
  return 1;
}

static int file_sel_cb(const char *fpath, const char *fname, void *data) {
  (void)fname; (void)data;
  if (load_file(fpath) && N->cfg.nfiles < 4) {
    int dup = 0;
    for (int i = 0; i < N->cfg.nfiles; i++) if (!strcmp(N->cfg.files[i], fpath)) dup = 1;
    if (!dup) { strncpy(N->cfg.files[N->cfg.nfiles], fpath, 39); N->cfg.files[N->cfg.nfiles][39] = 0; N->cfg.nfiles++; }
  }
  return MRET_EXIT;
}

static void draw_files(void) {
  header("FILES");
  char s[96];
  int y = 26;
  draw_text(&TINY, 2, y, "Stations (.TID) and atlas (.FLW) from " TIDEDIR, 0); y += LH;
  draw_text(&TINY, 2, y, "PC: tools/gen_tides.py files ~/opt/tide47 OUT", 0); y += LH + 4;
  sprintf(s, "compiled in: %d public, %d private regions, atlas %s", PUB_NREGIONS, PRIV_NREGIONS,
          FLOW_ATLAS_PRESENT ? "yes" : "no");
  draw_text(&TINY, 2, y, s, 0); y += LH + 4;
  draw_text(&TINY, 2, y, "LOADED FROM FILES (reloaded at start):", 0); y += LH;
  for (int i = 0; i < N->cfg.nfiles; i++, y += LH) draw_text(&TINY, 10, y, N->cfg.files[i], 0);
  for (int r = 0; r < N->nfr; r++, y += LH) {
    sprintf(s, "region %s: %s, %d stations", N->fr[r].code, N->fr[r].title, N->fr[r].n);
    draw_text(&TINY, 10, y, s, 0);
  }
  if (N->have_ffl) { sprintf(s, "atlas: %dx%d, reference %s", N->ffl.nlat, N->ffl.nlon, N->ffl.ref); draw_text(&TINY, 10, y, s, 0); y += LH; }
  if (N->msg[0]) draw_text(&TINY, 2, MENU_Y - LH, N->msg, 1);
}

/* ------------------------------------------------------------------ */
/*  Config                                                            */
/* ------------------------------------------------------------------ */
static void cfg_save(void) {
  if (!sys_disk_ok()) return;
  char *b = (char *)malloc(1024);
  if (!b) return;
  Cfg *c = &N->cfg;
  char *o = b;
  o += sprintf(o, "NAV47 CFG 1\nLAT %.9f\nLON %.9f\nTZ %.4f\nLIVE %d\nFIXED %.9f\nWIND %.1f %.1f\nTIDE %d %d %d\nTAUTO %d\nZONE %d %d\nSOUTH %d\nGC %d\nLT %d\n",
               c->lat, c->lon, c->tz, c->live, c->fixed_jd, c->wdir, c->wkn, c->tsrc, c->treg, c->tst, c->tauto, c->zone, c->city,
               c->south, c->gc, c->lt);
  o += sprintf(o, "DPWX %d %d %.1f %.2f %.1f %.1f %.1f\n", c->dcauto, c->dsauto, c->dcset, c->dckn, c->dsfrom, c->dhs, c->dtp);
  for (int i = 0; i < c->nfiles; i++) o += sprintf(o, "FILE %s\n", c->files[i]);
  sys_disk_write_enable(1);
  check_create_dir(DIR);
  FIL f;
  UINT bw;
  if (f_open(&f, CFGFILE, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) { f_write(&f, b, (UINT)strlen(b), &bw); f_close(&f); }
  sys_disk_write_enable(0);
  free(b);
}

static void cfg_load(void) {
  Cfg *c = &N->cfg;
  c->lat = 26.5; c->lon = 50.1667; c->tz = 3; c->live = 1; c->fixed_jd = jd_ymd(2026, 11, 2, 12);
  c->wdir = 0; c->wkn = 0; c->zone = -1; c->tauto = 1; c->dcauto = 1; c->dsauto = 1;
  char *t = read_text(CFGFILE);
  if (!t) return;
  char *l = strtok(t, "\n");
  c->nfiles = 0;
  while (l) {
    if (!strncmp(l, "LAT ", 4)) c->lat = atof(l + 4);
    else if (!strncmp(l, "LON ", 4)) c->lon = atof(l + 4);
    else if (!strncmp(l, "TZ ", 3)) c->tz = atof(l + 3);
    else if (!strncmp(l, "LIVE ", 5)) c->live = atoi(l + 5);
    else if (!strncmp(l, "FIXED ", 6)) c->fixed_jd = atof(l + 6);
    else if (!strncmp(l, "WIND ", 5)) sscanf(l + 5, "%lf %lf", &c->wdir, &c->wkn);
    else if (!strncmp(l, "TIDE ", 5)) sscanf(l + 5, "%d %d %d", &c->tsrc, &c->treg, &c->tst);
    else if (!strncmp(l, "TAUTO ", 6)) c->tauto = atoi(l + 6) != 0;
    else if (!strncmp(l, "ZONE ", 5)) sscanf(l + 5, "%d %d", &c->zone, &c->city);
    else if (!strncmp(l, "SOUTH ", 6)) c->south = atoi(l + 6);
    else if (!strncmp(l, "GC ", 3)) c->gc = atoi(l + 3);
    else if (!strncmp(l, "LT ", 3)) c->lt = atoi(l + 3) != 0;
    else if (!strncmp(l, "DPWX ", 5)) sscanf(l + 5, "%d %d %lf %lf %lf %lf %lf", &c->dcauto, &c->dsauto, &c->dcset, &c->dckn, &c->dsfrom, &c->dhs, &c->dtp);
    else if (!strncmp(l, "FILE ", 5) && c->nfiles < 4) { strncpy(c->files[c->nfiles], l + 5, 39); c->files[c->nfiles++][39] = 0; }
    l = strtok(NULL, "\n");
  }
  free(t);
}

/* ------------------------------------------------------------------ */
/*  INFO                                                              */
/* ------------------------------------------------------------------ */
/* INFO: paragraphs word-wrapped to the LCD in the tiny font, 8 2 scroll. A paragraph "HEAD:   text" wraps under
   its text (a hanging indent); "" is an empty line. */
static int info_top, info_lines;

/* one paragraph: the lines from *n on; those inside [top, top + vis) drawn from row y0 */
static void info_par(const char *p, int *n, int top, int vis, int y0) {
  char line[100];
  const char *c = strstr(p, ":   ");
  int xt = 2, w = LCD_X - 4 - 6;                       /* 6 px for the scroll bar */
  if (!*p) { (*n)++; return; }
  if (c && c - p < 12) {                               /* "HEAD:   text": the head, the text in its column */
    memcpy(line, p, (size_t)(c - p) + 1); line[c - p + 1] = 0;
    if (*n >= top && *n < top + vis) draw_text(&TINY, 2, y0 + (*n - top) * LH, line, 0);
    xt = 2 + text_width(&TINY, "ALMANAC: ");
    p = c + 1;
    while (*p == ' ') p++;
  }
  while (*p) {
    int len = 0, last = 0;
    while (p[len] && len < 98) {                         /* as many words as fit */
      memcpy(line, p, (size_t)len + 1); line[len + 1] = 0;
      if (xt + text_width(&TINY, line) > 2 + w) break;
      len++;
      if (p[len] == ' ' || !p[len]) last = len;
    }
    if (!p[len]) last = len;
    if (!last) last = len ? len : 1;                     /* one word longer than the line: cut it */
    memcpy(line, p, (size_t)last); line[last] = 0;
    if (*n >= top && *n < top + vis) draw_text(&TINY, xt, y0 + (*n - top) * LH, line, 0);
    (*n)++;
    p += last;
    while (*p == ' ') p++;
  }
}

static void draw_info(void) {
  header("INFO");
  char s1[96], s2[96], s3[64];
  sprintf(s1, "tzdata %s, free heap %d KB, tide stations: %d public regions, %d private, %d files loaded.",
          TZDATA_VERSION, sys_free_mem() / 1024, PUB_NREGIONS, PRIV_NREGIONS, N->nfr);
  sprintf(s2, "CPU %s.", usb_powered() ? "160 MHz (USB power)" : "80 MHz (battery)");
  sprintf(s3, "Version " PROGRAM_VERSION ".");
  const char *P[] = {
    "NAV47: Almanac 47 + Zones 47 + Tide 47 + passage planning for the DM42n, a native DMCP5 program.", s3,
    "(C) 2026 Victor Valdes, Master Mariner. GPL-3.0-or-later. Written with the help of AI (Claude).",
    "",
    "ALMANAC:   VSOP87D Sun and planets, IAU1980 nutation, Meeus Moon with a DE421 fit, the 57 navigational stars "
    "and Polaris (Almanac 47 NAVINIT_FULL series, double precision, no tables).",
    "VIEWS:   ALMAN SPLIT SKY ALLSK MOON ANIM, and SKY2: the dome, the zenith in the centre (SKY again, or 0).",
    "ZONES:   IANA tz database, summer time computed on the calculator. Map: GLOBE (NOAA).",
    "TIDES:   Schureman harmonic method (Tide 47). NOAA (public domain), TICON-3 (CC-BY 4.0). AUTO (0 on TIDE or T&F): "
    "the stations nearest the position (SET or HERE), every source; T&F: the atlas where it covers the position, else a "
    "stream station within 30 NM, else the tide. Choosing a station (ENTER, 8 2) ends AUTO.",
    "ROUTE:   RL rhumb line, GC great circle, CS composite (the limit with 4 6), on the WGS84 ellipsoid or another "
    "(COMPARE, 7 9). Geodesics: GeographicLib (C. F. F. Karney, MIT); rhumb lines exact on the ellipsoid.",
    "PLAN:   the route split every N NM, into N legs or on whole meridians; ENTER PLAN.CSV, shift ENTER "
    "PLAN.NMEA (and USB channel 2), R/S VOYAGE.",
    "CHART:   Mercator or GLOBE (F4) over the coastline, Natural Earth 110 m and 50 m (public domain), "
    "/NAV47/COAST_C.BIN and COAST_L.BIN. 8 2 4 6 pan or turn, 7 9 zoom, 5 fit; ROUTE 0 opens it.",
    "VOYAGE:   the plan sailed or flown on a globe that follows: the ship (from 100 kn a plane), the date and "
    "time on board, DEP ARR, run and to go. R/S play, 4 6 a step, 7 9 zoom, ENTER the speed (typed) or + - (steps), 1 3 the departure, 0 ETA.",
    "DP OPS:   (DP, menu 3) can she keep station off a platform? The platform (a square or a rectangle, its faces, its height) in /NAV47/DPOPS.TXT, made on the web page's DP tab; the vessel a DPCAP47 .DPV (VESSEL line, or /NAV47/VESSEL.DPV; else a made-up 95 m DP2 with three bow tunnels and two azimuths). DPCAP47's engine (DNV-ST-0111 level 1's forces, x 1.25; its thrust allocation). The picture: each thruster's force an arrow, the weather's whole push a thick arrow in tonnes (PUSH: x 1.25, where it pushes, OFF or ON the platform) with the wind, the current and the sea from their own directions; the platform's lee lightens the wind (taller than the vessel: 30 % of it left close in, all of it 5 heights downwind). ENTER (BEST) tries every heading within the tolerance of square to the face (every face with FACE AUTO), stern, starboard, port or bow to it, the hull STANDOFF m off; inside the working limits (LIM: the bow tunnels 30 %); then each redundancy group lost at full thrust; then the wind she holds to with its sea. 4 6 the heading by hand, 7 9 the wind's direction 10 degrees, 8 2 its speed 1 kn, UP DOWN an hour (the current follows), 5 now, 0 the bars and the arrows after the worst failure, 3 the bars in tonnes or %, 1 starboard or port side to: parallel to the face (PAR, the usual set-up) or within the tolerance. FACE, SIDE, INPUT (the wind as SET; the current FLOW's or typed; the sea DNV's or typed; the standoff, the tolerance), LOAD reads the files again. Static, planning only: not the vessel's DP consequence analysis.",
    "ETA TABLE:   (TABLE on ETA) the ETAs at the set speed and three slower ones, 0.5 kn apart: 8 2 the speed, 4 6 the step (0.25 0.5 1 kn), 5 the set speed, ENTER (USE) this line's speed for the voyage.",
    "ETA:   the plan's distance with ETD, ETA and speed: set two, the third follows (ETA typed: the speed it needs). 8 2 the field, ENTER types it (dates YYYY.MMDD, times HH.MM, in UT or LT), 0 or KEEP: a new ETD keeps the speed or the ETA, NOW: the ETD now. Saved in ROUTE.TXT (SPEED, ETD).",
    "FONTS:   C47 standardFont and tinyFont (C43 / C47 authors, GPL v3).",
    "",
    "DOES NOT REPLACE THE NAUTICAL ALMANAC, OFFICIAL CHARTS OR TIDE TABLES. Not for use as the primary "
    "means of navigation.",
    "",
    s1, s2,
  };
  const int y0 = 17, vis = (174 - y0) / LH;
  int n = 0;
  for (unsigned i = 0; i < sizeof P / sizeof *P; i++) info_par(P[i], &n, info_top, vis, y0);
  info_lines = n;
  if (info_top > n - vis) info_top = n - vis > 0 ? n - vis : 0;
  if (n > vis) {                                         /* the scroll bar */
    int h = vis * LH, bh = h * vis / n, by = y0 + (h - bh) * info_top / (n - vis);
    gfx_rect(LCD_X - 4, y0, 1, h, 1);
    gfx_rect(LCD_X - 6, by, 5, bh < 6 ? 6 : bh, 1);
  }
  char s[64], u[64];
  usbview_state(s, u, 64);
  nt_inv(4, 176, s);
  nt(4, 192, u);
  if (n > vis) { sprintf(s, "8 2 %d/%d", info_top + vis < n ? info_top + vis : n, n); nt_r(LCD_X - 2, 192, s); }
}

/* ------------------------------------------------------------------ */
/*  ROUTE: passage planning                                           */
/* ------------------------------------------------------------------ */
/* geo47 PATCH: sailing RL 0 / GC 1 / CS 2, composite limit, PLAN split, leg cache */
static const char *SAIL_NAME[3] = { "RL", "GC", "CS" };
static double cs_lim = 60.0;                 /* CS limiting latitude, deg N/S */
static int plan_split = SPLIT_DLON;          /* 0 none, 1 every NM, 2 N legs, 3 meridians */
static double plan_val = 10.0;
static int legs_dirty = 1, legs_mode = -1, plan_dirty = 1, plan_cur, plan_rc;
static char plan_msg[48];

/* a CUSTOM ellipsoid NAV47 accepts: an Earth-sized a, and a sphere or f < 0.01 */
static int ell_ok(double a, double rf) { return a >= 6.0e6 && a <= 7.0e6 && (rf == 0 || rf > 100); }

static void route_save(void) {
  legs_dirty = plan_dirty = 1;
  if (!sys_disk_ok()) return;
  char *b = (char *)malloc(128 + 48 * MAXWP);
  if (!b) return;
  char *o = b;
  o += sprintf(o, "NAV47 ROUTE 1\n%s\nLIM %.1f\nSPLIT %d %.3f\nELLC %.4f %.9f\nELL %d\nSPEED %.4f\nETD %.6f\n", SAIL_NAME[N->cfg.gc % 3], cs_lim,
               plan_split, plan_val, ellipsoids[ELL_CUSTOM].a, ellipsoids[ELL_CUSTOM].rf, geo_ell_index, voy_kn, voy_etd0);
  for (int i = 0; i < N->nwp; i++) o += sprintf(o, "%.7f %.7f\n", N->wlat[i], N->wlon[i]);
  sys_disk_write_enable(1);
  check_create_dir(DIR);
  FIL f;
  UINT bw;
  if (f_open(&f, ROUTEFILE, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) { f_write(&f, b, (UINT)strlen(b), &bw); f_close(&f); }
  sys_disk_write_enable(0);
  free(b);
}

static void route_load(void) {
  char *t = read_text(ROUTEFILE);
  if (!t) return;
  char *l = strtok(t, "\n");
  N->nwp = 0;
  while (l) {
    double a, b, v;
    int m;
    size_t ln = strlen(l);
    while (ln && (l[ln - 1] == '\r' || l[ln - 1] == ' ' || l[ln - 1] == '\t')) l[--ln] = 0;   /* CR LF files too */
    if (!strcmp(l, "GC")) N->cfg.gc = 1;
    else if (!strcmp(l, "RL")) N->cfg.gc = 0;
    else if (!strcmp(l, "CS")) N->cfg.gc = 2;
    else if (sscanf(l, "LIM %lf", &v) == 1) { if (v >= 10 && v <= 85) cs_lim = v; }
    else if (sscanf(l, "SPLIT %d %lf", &m, &v) == 2) { if (m >= 0 && m <= 3) { plan_split = m; plan_val = v; } }
    else if (sscanf(l, "ELLC %lf %lf", &a, &b) == 2) {     /* CUSTOM's a and 1/f */
      if (ell_ok(a, b)) { ellipsoids[ELL_CUSTOM].a = a; ellipsoids[ELL_CUSTOM].rf = b; }
      if (geo_ell_index == ELL_CUSTOM) geo_set_ellipsoid(ELL_CUSTOM);
    }
    else if (sscanf(l, "ELL %d", &m) == 1) geo_set_ellipsoid(m);
    else if (sscanf(l, "SPEED %lf", &v) == 1) { if (v >= 1 && v <= 1000) voy_kn = v; }
    else if (sscanf(l, "ETD %lf", &v) == 1) { voy_etd0 = v > 2400000 ? v : 0; }
    else if (sscanf(l, "%lf %lf", &a, &b) == 2 && N->nwp < MAXWP && fabs(a) <= 90 && fabs(b) <= 180) {
      N->wlat[N->nwp] = a; N->wlon[N->nwp] = b; N->nwp++;
    }
    l = strtok(NULL, "\n");
  }
  free(t);
}

static void route_insert(double lat, double lon) {
  legs_dirty = plan_dirty = 1;
  if (N->nwp >= MAXWP) return;
  int at = N->nwp ? wp_cur + 1 : 0;
  memmove(&N->wlat[at + 1], &N->wlat[at], sizeof(double) * (size_t)(N->nwp - at));
  memmove(&N->wlon[at + 1], &N->wlon[at], sizeof(double) * (size_t)(N->nwp - at));
  N->wlat[at] = lat; N->wlon[at] = lon;
  N->nwp++;
  wp_cur = at;
}

static void route_delete(void) {
  legs_dirty = plan_dirty = 1;
  if (!N->nwp) return;
  memmove(&N->wlat[wp_cur], &N->wlat[wp_cur + 1], sizeof(double) * (size_t)(N->nwp - wp_cur - 1));
  memmove(&N->wlon[wp_cur], &N->wlon[wp_cur + 1], sizeof(double) * (size_t)(N->nwp - wp_cur - 1));
  N->nwp--;
  if (wp_cur >= N->nwp && wp_cur) wp_cur--;
}

static void leg(int i, double *crs, double *nm) {   /* geo47: cached, recomputed on change */
  if (legs_dirty || legs_mode != N->cfg.gc) {
    for (int k = 1; k < N->nwp; k++) {
      if (N->cfg.gc == 2)
        geo_leg_cs(N->wlat[k - 1], N->wlon[k - 1], N->wlat[k], N->wlon[k], cs_lim, &N->leg_crs[k], &N->leg_nm[k]);
      else
        (N->cfg.gc ? leg_gc : leg_rl)(N->wlat[k - 1], N->wlon[k - 1], N->wlat[k], N->wlon[k], &N->leg_crs[k], &N->leg_nm[k]);
    }
    legs_dirty = 0;
    legs_mode = N->cfg.gc;
    plan_dirty = 1;                       /* geo_leg_cs used geo_route as scratch */
  }
  *crs = N->leg_crs[i];
  *nm = N->leg_nm[i];
}

static void draw_route(void) {
  char s[64];
  double tot = 0, crs, nm;
  for (int i = 1; i < N->nwp; i++) { leg(i, &crs, &nm); tot += nm; }
  if (N->cfg.gc == 2) sprintf(s, "ROUTE CS %.0f", cs_lim); else strcpy(s, N->cfg.gc ? "ROUTE GC" : "ROUTE RL");
  header(s);
  int xw = 4 + nt_w("100"), xla = xw + 10 + nt_w("00" NT_DEG "00.0N"), xlo = xla + 12 + nt_w("000" NT_DEG "00.0E"),
      xc = xlo + 12 + nt_w("000" NT_DEG), xd = xc + 12 + nt_w("0000.0"), xcu = xd + 12 + nt_w("00000.0");
  int e = (396 - xcu) / 5;
  if (e > 0) { xla += e; xlo += 2 * e; xc += 3 * e; xd += 4 * e; xcu += 5 * e; }
  nt_r(xw, 16, "WP"); nt_c(xla - nt_w("00" NT_DEG "00.0N") / 2, 16, "LAT"); nt_c(xlo - nt_w("000" NT_DEG "00.0E") / 2, 16, "LON");
  nt_r(xc, 16, N->cfg.gc ? "ICRS" : "CRS"); nt_r(xd, 16, "DIST"); nt_r(xcu, 16, "CUM");
  if (!N->nwp) {
    nt(4, 48, "No waypoints yet.");
    nt(4, 72, "HERE: the position (SET)");
    nt(4, 88, "ADD: a new one, then type");
    nt(4, 104, "  LAT DD.MMm ENTER");
    nt(4, 120, "  LON DDD.MMm ENTER");
    nt(4, 136, "  (+/- for S and W)");
    nt(4, 160, "RL rhumb, GC great circle,");
    nt(4, 176, "CS composite (4 6 limit)");
    nt(4, 192, "0: CHART and GLOBE (you, the coast)");
    return;
  }
  int vis = 10;
  if (wp_edit >= 0) nt_inv(2, 192, wp_edit ? "LON DDD.MMm  +/- WEST  ENTER" : "LAT DD.MMm  +/- SOUTH  ENTER");
  else nt(2, 192, N->cfg.gc == 2 ? "4 6 limit 5 cmp R/S plan" : "ENTER edit 5 cmp R/S plan");
  sprintf(s, "%d WP  TOTAL %.1f NM", N->nwp, tot);
  if (wp_edit < 0 || nt_w(s) < 150) nt_r(LCD_X - 2, 192, s);
  if (wp_cur >= N->nwp) wp_cur = N->nwp - 1;
  int top = wp_cur - vis / 2;
  if (top > N->nwp - vis) top = N->nwp - vis;
  if (top < 0) top = 0;
  double cum = 0;
  for (int i = 1; i < top; i++) { leg(i, &crs, &nm); cum += nm; }
  for (int i = top; i < N->nwp && i < top + vis; i++) {
    int y = 32 + NT_ROW * (i - top);
    sprintf(s, "%d", i + 1);
    if (i == wp_cur) nt_inv(xw - nt_w(s), y, s); else nt_r(xw, y, s);
    if (i == wp_cur && wp_edit >= 0) {
      if (wp_edit == 0) { sprintf(s, "%s_", ebuf); nt_r(xla, y, s); }
      else {
        fmt_dmn(s, wp_lat, 2, wp_lat < 0 ? 'S' : 'N'); nt_r(xla, y, s);
        sprintf(s, "%s_", ebuf); nt_r(xlo, y, s);
      }
      continue;
    }
    fmt_dmn(s, N->wlat[i], 2, N->wlat[i] < 0 ? 'S' : 'N'); nt_r(xla, y, s);
    fmt_dmn(s, N->wlon[i], 3, N->wlon[i] < 0 ? 'W' : 'E'); nt_r(xlo, y, s);
    if (i > 0) {
      leg(i, &crs, &nm);
      cum += nm;
      sprintf(s, "%03.0f" NT_DEG, fmod(floor(crs + 0.5), 360.0)); nt_r(xc, y, s);
      sprintf(s, "%.1f", nm); nt_r(xd, y, s);
      sprintf(s, "%.1f", cum); nt_r(xcu, y, s);
    }
  }
}

static void wp_start_edit(void) { if (N->nwp) { wp_edit = 0; elen = 0; ebuf[0] = 0; } }

static int digit(int key);
static void key_wp(int key) {
  int d = digit(key);
  if (key == KEY_ENTER) {
    double v = parse_dm(ebuf);
    if (wp_edit == 0) {
      wp_lat = elen && fabs(v) <= 90 ? v : N->wlat[wp_cur];
      wp_edit = 1; elen = 0; ebuf[0] = 0;
    } else {
      N->wlat[wp_cur] = wp_lat;
      if (elen && fabs(v) <= 180) N->wlon[wp_cur] = v;
      wp_edit = -1; wp_new = 0;
      route_save();
    }
    return;
  }
  if (key == KEY_EXIT) { wp_edit = -1; if (wp_new) route_delete(); wp_new = 0; return; }
  if (key == KEY_BSP) { if (elen) ebuf[--elen] = 0; return; }
  if (key == KEY_CHS) { if (ebuf[0] == '-') { memmove(ebuf, ebuf + 1, (size_t)elen); elen--; } else if (elen < 14) { memmove(ebuf + 1, ebuf, (size_t)elen + 1); ebuf[0] = '-'; elen++; } return; }
  if (key == KEY_DOT && elen < 14 && !strchr(ebuf, '.')) { ebuf[elen++] = '.'; ebuf[elen] = 0; return; }
  if (d >= 0 && elen < 14) { ebuf[elen++] = (char)('0' + d); ebuf[elen] = 0; }
}

/* ------------------------------------------------------------------ */
/*  Menus, keys, main loop                                            */
/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */
/*  PLAN: the route split into waypoints (geo47)                      */
/* ------------------------------------------------------------------ */
static void go(int p);   /* defined further down */
static void voy_open(int back);
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

#define PLAN_HINT "5 split 4 6 step ENT csv sh nmea"
static void draw_plan(void) {
  static const char *SPL[4] = { "", "NM", "LEGS", "DLON" };
  char s[64], r[32];
  if (!geo_route_alloc()) { header("PLAN"); nt(4, 48, "Not enough memory for PLAN."); return; }
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
  if (R->n < 2 && N->nwp >= 2) plan_build();             /* once more, from the start */
  if (R->n < 2) {
    if (N->nwp < 2) { sprintf(s, "Needs 2 waypoints on ROUTE (it has %d).", N->nwp); nt(4, 48, s); return; }
    sprintf(s, "PLAN failed: route %d WP, plan %d, code %d", N->nwp, R->n, plan_rc); nt(4, 48, s);
    sprintf(s, "free heap %d KB. EXIT, then R/S again.", sys_free_mem() / 1024); nt(4, 64, s);
    return;
  }
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
  else nt(2, 192, PLAN_HINT);
  sprintf(r, "%d WP %.1f NM", R->n, R->w[R->n - 1].cum_m / NM);
  if (nt_w(r) + nt_w(plan_msg[0] ? plan_msg : PLAN_HINT) < 388) nt_r(LCD_X - 2, 192, r);
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

static int plan_dirs(void) {
  if (!sys_disk_ok()) { strcpy(plan_msg, "disk busy (USB?)"); return 0; }
  sys_disk_write_enable(1);
  check_create_dir("/NAV47");
  check_create_dir("/NAV47/ROUTES");
  sys_disk_write_enable(0);
  return 1;
}

static void plan_export(void) {
  if (!plan_dirs()) return;
  strcpy(plan_msg, geo_save_csv("/NAV47/ROUTES/PLAN.CSV") == 0 ? "saved /NAV47/ROUTES/PLAN.CSV" : "save failed");
}

/* shift ENTER: $GPWPL + $GPRTE to /NAV47/ROUTES/PLAN.NMEA, and to the PC on console channel 2 when the USB is on */
static void nmea_usb(const char *line, void *ctx) { (void)ctx; usbview_line(USBVIEW_CH_NMEA, line); }

static void plan_nmea(void) {
  if (!geo_routep || geo_route.n < 2) return;
  int f = plan_dirs() && geo_save_nmea("/NAV47/ROUTES/PLAN.NMEA") == 0;
  int u = usbview_is_on();
  if (u) route_nmea(&geo_route, GEO_NMEA_ID, nmea_usb, NULL);
  if (f) strcpy(plan_msg, u ? "NMEA: PLAN.NMEA + USB ch 2" : "saved /NAV47/ROUTES/PLAN.NMEA");
  else if (u) strcpy(plan_msg, "NMEA: USB ch 2 (file failed)");
  else if (!plan_msg[0]) strcpy(plan_msg, "NMEA save failed");
}

static void plan_key(int key) {
  plan_msg[0] = 0;
  if ((key == KEY_8 || key == KEY_UP) && plan_cur > 0) plan_cur--;
  else if ((key == KEY_2 || key == KEY_DOWN) && geo_routep && plan_cur < geo_route.n - 1) plan_cur++;
  else if (key == KEY_5) {
    plan_split = (plan_split + 1) % 4;
    plan_val = plan_split == SPLIT_DIST ? 100 : plan_split == SPLIT_LEGS ? 4 : plan_split == SPLIT_DLON ? 10 : 0;
    route_save();
  }
  else if (key == KEY_4 || key == KEY_6) { plan_step(key == KEY_6 ? 1 : -1); route_save(); }
  else if (key == KEY_ENTER) plan_export();
  else if (key == KEY_RUN && N->nwp > 1) voy_open(PG_PLAN);
  else if (key == KEY_EXIT) { go(PG_ROUTE); menu_page = 3; }
}

/* ------------------------------------------------------------------ */
/*  COMPARE: one leg as geodesic, rhumb and composite (geo47)         */
/* ------------------------------------------------------------------ */
static int cmp_leg(void) { return wp_cur > 0 ? wp_cur : 1; }   /* the leg that ends at the cursor */

/* row 16: the ellipsoid in use (while editing CUSTOM: the field being typed) */
static void cmp_ell_line(void) {
  char s[64];
  const ellipsoid_t *e = &ellipsoids[geo_ell_index];
  if (cmp_edit == 0) { sprintf(s, "CUSTOM a m: %s_", ebuf); nt_inv(4, 16, s); return; }
  if (cmp_edit == 1) { sprintf(s, "CUSTOM a %.1f  1/f (0 sphere): %s_", cmp_a, ebuf); nt_inv(4, 16, s); return; }
  if (e->rf) sprintf(s, "%s  a %.1f m  1/f %.6f", e->name, e->a, e->rf);
  else sprintf(s, "%s  R %.1f m", e->name, e->a);
  nt(4, 16, s);
}

static void cmp_footer(void) {
  char s[64];
  if (cmp_msg[0]) { nt_inv(2, 192, cmp_msg); return; }
  if (cmp_edit >= 0) { nt_inv(2, 192, cmp_edit ? "1/f: 0 sphere or over 100  ENTER" : "a: 6000000..7000000 m  ENTER"); return; }
  sprintf(s, "7 9 %s%s  8 2 leg", ellipsoids[geo_ell_index].name, geo_ell_index == ELL_CUSTOM ? " ENTER edit" : "");
  nt(2, 192, s);
  if (nt_w(s) < 300) nt_r(LCD_X - 2, 192, "EXIT back");
}

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
  cmp_ell_line();
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
  cmp_footer();
}

/* CUSTOM: ENTER takes a, then 1/f; an empty field keeps the value; out of range: the field again */
static void cmp_edit_key(int key) {
  int d = digit(key);
  cmp_msg[0] = 0;
  if (key == KEY_ENTER) {
    double v = elen ? atof(ebuf) : (cmp_edit == 0 ? ellipsoids[ELL_CUSTOM].a : ellipsoids[ELL_CUSTOM].rf);
    if (cmp_edit == 0) {
      if (!ell_ok(v, 0)) strcpy(cmp_msg, "a: 6000000..7000000 m");
      else { cmp_a = v; cmp_edit = 1; }
    } else if (!ell_ok(cmp_a, v)) strcpy(cmp_msg, "1/f: 0 (sphere) or over 100");
    else {
      ellipsoids[ELL_CUSTOM].a = cmp_a; ellipsoids[ELL_CUSTOM].rf = v;
      geo_set_ellipsoid(ELL_CUSTOM);
      cmp_edit = -1;
      route_save();
    }
    elen = 0; ebuf[0] = 0;
    return;
  }
  if (key == KEY_EXIT) { cmp_edit = -1; return; }
  if (key == KEY_BSP) { if (elen) ebuf[--elen] = 0; return; }
  if (key == KEY_DOT && elen < 14 && !strchr(ebuf, '.')) { ebuf[elen++] = '.'; ebuf[elen] = 0; return; }
  if (d >= 0 && elen < 14) { ebuf[elen++] = (char)('0' + d); ebuf[elen] = 0; }
}

static void cmp_key(int key) {
  cmp_msg[0] = 0;
  if (key == KEY_ENTER && geo_ell_index == ELL_CUSTOM) { cmp_edit = 0; elen = 0; ebuf[0] = 0; }
  else if (key == KEY_7 || key == KEY_9) {             /* ellipsoid: the presets and CUSTOM */
    int n = ELL_COUNT, e = (geo_ell_index + (key == KEY_9 ? 1 : n - 1)) % n;
    geo_set_ellipsoid(e);
    route_save();                                      /* saves ELL, marks legs and plan to redo */
  }
  else if ((key == KEY_8 || key == KEY_UP) && cmp_leg() > 1) wp_cur = cmp_leg() - 1;
  else if ((key == KEY_2 || key == KEY_DOWN) && cmp_leg() < N->nwp - 1) wp_cur = cmp_leg() + 1;
  else if (key == KEY_EXIT) { go(PG_ROUTE); menu_page = 3; }
}

/* ------------------------------------------------------------------ */
/*  CHART: the plan on Mercator over the coastline (map47)            */
/* ------------------------------------------------------------------ */
#define CHART_Y0 16
#define CHART_H  175
static map_t chart_m;
static int chart_back = PG_PLAN, chart_refit = 1, chart_globe;
static char chart_detail;                                /* 'C' or 'L': the coastline last drawn, 0 none */

static void chart_open(int back) { chart_back = back; chart_refit = 1; go(PG_CHART); }

/* no route: you, and the nearest coast in view */
static void chart_fit_here(void) {
  double d = 20, lat = N->cfg.lat, lon = N->cfg.lon;
  if (sys_disk_ok() && map_coast_nearest("/NAV47/COAST_C.BIN", lat, lon, &d) == 0 && d < 3) {
    double dl;
    if (map_coast_nearest("/NAV47/COAST_L.BIN", lat, lon, &dl) == 0) d = dl;   /* close: the finer coast */
  }
  d = d * 1.3 + 0.3;                                     /* the coast inside, with a margin */
  if (chart_globe) {
    map_center(&chart_m, lat, lon);
    float r = (float)sin((d > 80 ? 80 : d) * M_PI / 180);
    chart_m.k = (chart_m.h * 0.5f - 8) / r;
    map_zoom(&chart_m, 1);
    if (d > 80) chart_m.k = map_kmin(&chart_m);
  } else {
    double c = cos(lat * M_PI / 180);
    if (c < 0.1) c = 0.1;
    map_fit(&chart_m, lat - d, lat + d, lon - d / c, lon + d / c);
  }
}

/* points to see on the globe: the plan, and its geodesics sampled (they bulge) */
static int chart_pt(int i, double *la, double *lo) {
  route_t *R = &geo_route;
  if (i < R->n) { *la = R->w[i].lat; *lo = R->w[i].lon; return 1; }
  i -= R->n;
  int leg = i / 7 + 1;
  if (leg >= N->nwp) return 0;
  struct geod_geodesicline l;
  geod_inverseline(&l, &geo_ell, N->wlat[leg - 1], N->wlon[leg - 1], N->wlat[leg], N->wlon[leg], 0);
  geod_position(&l, l.s13 * (i % 7 + 1) / 8, la, lo, NULL);
  return 1;
}

static void chart_fit_globe(void) {
  double x = 0, y = 0, z = 0, la, lo;
  for (int i = 0; chart_pt(i, &la, &lo); i++) {          /* the centre: the points' mean direction */
    double p = la * M_PI / 180, q = lo * M_PI / 180;
    x += cos(p) * cos(q); y += cos(p) * sin(q); z += sin(p);
  }
  map_center(&chart_m, atan2(z, hypot(x, y)) * 180 / M_PI, atan2(y, x) * 180 / M_PI);
  float k = map_kmax(&chart_m);
  for (int i = 0; chart_pt(i, &la, &lo); i++) {
    float r = map_globe_fitpt(&chart_m, la, lo);
    if (r == 0) { k = 0; break; }                        /* too far round: the whole Earth */
    if (r < k) k = r;
  }
  chart_m.k = k;
  map_zoom(&chart_m, 1);                                 /* clamps: at least the whole disc */
}

/* the plan, the user legs' geodesics (their vertices) and a margin */
static void chart_fit(void) {
  map_init(&chart_m, 0, CHART_Y0, LCD_X, CHART_H, &geo_ell);
  chart_m.globe = chart_globe;
  if (N->nwp < 2 || !geo_routep) { chart_fit_here(); return; }
  if (chart_globe) { chart_fit_globe(); return; }
  route_t *R = &geo_route;
  double s = 90, n = -90, w = 1e9, e = -1e9, lon = R->w[0].lon;
  for (int i = 0; i < R->n; i++) {
    double la = R->w[i].lat;
    if (i) { double d = R->w[i].lon - R->w[i - 1].lon; lon += d > 180 ? d - 360 : d < -180 ? d + 360 : d; }
    if (la < s) s = la;
    if (la > n) n = la;
    if (lon < w) w = lon;
    if (lon > e) e = lon;
  }
  for (int i = 1; i < N->nwp; i++) {
    double vl, vo, sv;
    if (geod_vertex(&geo_ell, N->wlat[i - 1], N->wlon[i - 1], N->wlat[i], N->wlon[i], &vl, &vo, &sv)) {
      if (vl < s) s = vl;
      if (vl > n) n = vl;
    }
  }
  double ml = (n - s) * 0.08 + 0.3, mo = (e - w) * 0.08 + 0.3;
  map_fit(&chart_m, s - ml, n + ml, w - mo, e + mo);
}

static void draw_chart(void) {
  char s[64];
  int route = N->nwp >= 2;
  if (!route) sprintf(s, chart_globe ? "GLOBE" : "CHART");
  else sprintf(s, N->cfg.gc == 2 ? "%s CS %.0f" : N->cfg.gc ? "%s GC" : "%s RL", chart_globe ? "GLOBE" : "CHART", cs_lim);
  header(s);
  if (route) {
    if (!geo_route_alloc()) { nt(4, 48, "Not enough memory for CHART."); return; }
    if (plan_dirty || legs_dirty) plan_build();
  }
  route_t *R = route ? &geo_route : NULL;
  if (R && R->n < 2) { route = 0; R = NULL; }             /* no plan: the chart without it */
  if (chart_refit) { chart_fit(); chart_refit = 0; }
  chart_m.e = (float)sqrt(geo_ell.f * (2 - geo_ell.f));   /* the ellipsoid may have changed */
  int step = map_grid(&chart_m);
  chart_detail = 0;
  if (sys_disk_ok()) {
    if (map_span_lon(&chart_m) < 40 && map_coast(&chart_m, "/NAV47/COAST_L.BIN") >= 0) chart_detail = 'L';
    else if (map_coast(&chart_m, "/NAV47/COAST_C.BIN") >= 0) chart_detail = 'C';
  }
  int gc = N->cfg.gc % 3;
  if (route && gc == 2) {                                /* the limit parallel, on the route's side */
    double m = 0;
    for (int i = 0; i < N->nwp; i++) m += N->wlat[i];
    double l = m < 0 ? -cs_lim : cs_lim;
    if (chart_globe) for (int i = 0; i < 72; i++) map_seg(&chart_m, l, i * 5, l, i * 5 + 5, MAP_DOT);
    else {
      float x, y;
      map_xy(&chart_m, l, chart_m.lon0, &x, &y);
      if (y >= CHART_Y0 && y < CHART_Y0 + CHART_H) gfx_hline_dotted(0, LCD_X - 1, (int)lroundf(y), 8);
    }
  }
  if (route) {
    for (int i = 1; i < N->nwp; i++) {                   /* the other sailings, for comparison */
      if (gc != 0) map_seg(&chart_m, N->wlat[i - 1], N->wlon[i - 1], N->wlat[i], N->wlon[i], MAP_DASH);
      if (gc != 1) map_geod(&chart_m, &geo_ell, N->wlat[i - 1], N->wlon[i - 1], N->wlat[i], N->wlon[i], MAP_DOT);
    }
    for (int i = 1; i < R->n; i++) {                     /* the plan: thick */
      const wpt_t *a = &R->w[i - 1], *b = &R->w[i];
      if (b->kind == SEG_GEOD) map_geod(&chart_m, &geo_ell, a->lat, a->lon, b->lat, b->lon, MAP_THICK);
      else map_seg(&chart_m, a->lat, a->lon, b->lat, b->lon, MAP_THICK);
    }
    for (int i = 0; i < R->n; i++) map_mark(&chart_m, R->w[i].lat, R->w[i].lon, (R->w[i].flags & WPT_USER) ? 1 : 0);
    if (plan_cur < R->n) map_mark(&chart_m, R->w[plan_cur].lat, R->w[plan_cur].lon, 2);
  }
  map_mark(&chart_m, N->cfg.lat, N->cfg.lon, 3);         /* you */
  map_space(&chart_m);
  gfx_rect(0, CHART_Y0 + CHART_H, LCD_X, 1, 1);
  nt(2, 192, chart_globe ? "4 6 turn 8 2 tilt 7 9 zoom" : "8246 pan 7 9 zoom 5 fit");
  if (chart_detail) sprintf(s, "%c grid %d" NT_DEG, chart_detail, step);
  else strcpy(s, "no COAST_C.BIN");
  nt_r(LCD_X - 2, 192, s);
}

static void chart_key(int key) {
  if (key == KEY_4) map_pan(&chart_m, -0.25f, 0);
  else if (key == KEY_6) map_pan(&chart_m, 0.25f, 0);
  else if (key == KEY_8 || key == KEY_UP) map_pan(&chart_m, 0, 0.25f);
  else if (key == KEY_2 || key == KEY_DOWN) map_pan(&chart_m, 0, -0.25f);
  else if (key == KEY_9) map_zoom(&chart_m, 1.5f);
  else if (key == KEY_7) map_zoom(&chart_m, 1 / 1.5f);
  else if (key == KEY_5) chart_refit = 1;
  else if (key == KEY_0) { chart_globe ^= 1; map_set_globe(&chart_m, chart_globe); }
  else if (key == KEY_EXIT) { go(chart_back); if (chart_back == PG_ROUTE) menu_page = 3; }
}

/* ------------------------------------------------------------------ */
/*  VOYAGE: the plan sailed (or flown) step by step on a globe that follows the vessel */
/* ------------------------------------------------------------------ */
#define VOY_STEPS 96
#define VOY_W     220                                  /* the globe's box: x 0..219, rows 16..190 */
static map_t voy_m;
static int voy_step, voy_play, voy_back = PG_PLAN, voy_edit;   /* voy_edit: typing the speed */
static double voy_etd;                                 /* the departure, UT (JD) */
static const double VOY_KN[] = { 4, 6, 8, 10, 12, 14, 16, 18, 20, 25, 30, 40, 250, 300, 400, 450, 480, 500, 550 };

static void voy_open(int back) {
  voy_back = back; voy_step = 0; voy_play = 0; voy_etd = voy_etd0 > 0 ? voy_etd0 : ut_now();
  map_init(&voy_m, 0, CHART_Y0, VOY_W, CHART_H, &geo_ell);
  voy_m.globe = 1;
  voy_m.k = (VOY_W / 2.0f) / sinf(25 * (float)M_PI / 180);   /* about 50 deg across */
  go(PG_VOY);
}

/* the point s metres along the plan, and the course there */
static void voy_pos(double s, double *lat, double *lon, double *crs) {
  route_t *R = &geo_route;
  int i = 1;
  while (i < R->n - 1 && R->w[i].cum_m < s) i++;
  const wpt_t *a = &R->w[i - 1], *b = &R->w[i];
  double ds = s - a->cum_m;
  if (ds < 0) ds = 0;
  if (ds > b->cum_m - a->cum_m) ds = b->cum_m - a->cum_m;
  if (b->kind == SEG_GEOD) {
    struct geod_geodesicline l;
    geod_inverseline(&l, &geo_ell, a->lat, a->lon, b->lat, b->lon, 0);
    geod_position(&l, ds, lat, lon, crs);
  } else {
    *crs = a->rl_crs;
    if (rhumb_direct(&geo_rh, a->lat, a->lon, a->rl_crs, ds, lat, lon) != 0) { *lat = b->lat; *lon = b->lon; }
  }
  *crs = fmod(*crs + 360.0, 360.0);
}

static void fmt_dt(char *s, double ut) {               /* dd-mm hh:mm in the times shown (UT or LT) */
  int y, m, d;
  double h;
  jd_to_ymd(ut + show_tz() / 24.0 + 1.0 / 2880, &y, &m, &d, &h);
  sprintf(s, "%02d-%02d %02d:%02d", d, m, (int)h, (int)((h - (int)h) * 60));
}

static void fmt_dur(char *s, double hours) {           /* 3d 04:30 */
  long mi = (long)floor(hours * 60 + 0.5);
  if (mi >= 1440) sprintf(s, "%ldd %02ld:%02ld", mi / 1440, (mi / 60) % 24, mi % 60);
  else sprintf(s, "%02ld:%02ld", mi / 60, mi % 60);
}

static void draw_voy(void) {
  char s[64], t[32];
  int route = N->nwp >= 2;
  if (!route || !geo_route_alloc()) { header("VOYAGE"); nt(4, 48, "Needs a route (ROUTE, 2 waypoints)."); return; }
  if (plan_dirty || legs_dirty) plan_build();
  route_t *R = &geo_route;
  if (R->n < 2) { header("VOYAGE"); nt(4, 48, "No plan: PLAN first (R/S on ROUTE)."); return; }
  double tot = R->w[R->n - 1].cum_m, sd = tot * voy_step / VOY_STEPS;
  double hours = tot / NM / voy_kn, el = hours * voy_step / VOY_STEPS;
  double lat, lon, crs;
  voy_pos(sd, &lat, &lon, &crs);
  int plane = voy_kn >= 100;
  bar_at(voy_etd + el / 24, plane ? "FLIGHT" : "VOYAGE");          /* the counter: the top bar's date and time */
  map_center(&voy_m, lat, lon);                                     /* the globe follows */
  voy_m.e = (float)sqrt(geo_ell.f * (2 - geo_ell.f));
  map_grid(&voy_m);
  if (sys_disk_ok()) {
    if (!(map_span_lon(&voy_m) < 40 && map_coast(&voy_m, "/NAV47/COAST_L.BIN") >= 0)) map_coast(&voy_m, "/NAV47/COAST_C.BIN");
  }
  for (int i = 1; i < R->n; i++) {                      /* sailed: thick; to go: dashed */
    const wpt_t *a = &R->w[i - 1], *b = &R->w[i];
    int g = b->kind == SEG_GEOD;
    if (b->cum_m <= sd) {
      if (g) map_geod(&voy_m, &geo_ell, a->lat, a->lon, b->lat, b->lon, MAP_THICK);
      else map_seg(&voy_m, a->lat, a->lon, b->lat, b->lon, MAP_THICK);
    } else if (a->cum_m >= sd) {
      if (g) map_geod(&voy_m, &geo_ell, a->lat, a->lon, b->lat, b->lon, MAP_DASH);
      else map_seg(&voy_m, a->lat, a->lon, b->lat, b->lon, MAP_DASH);
    } else {
      if (g) { map_geod(&voy_m, &geo_ell, a->lat, a->lon, lat, lon, MAP_THICK); map_geod(&voy_m, &geo_ell, lat, lon, b->lat, b->lon, MAP_DASH); }
      else { map_seg(&voy_m, a->lat, a->lon, lat, lon, MAP_THICK); map_seg(&voy_m, lat, lon, b->lat, b->lon, MAP_DASH); }
    }
  }
  for (int i = 0; i < R->n; i++) if (R->w[i].flags & WPT_USER) map_mark(&voy_m, R->w[i].lat, R->w[i].lon, 1);
  map_icon(&voy_m, lat, lon, crs, plane, !voy_play && (voy_step == 0 || voy_step >= VOY_STEPS));   /* stopped: its side */
  map_space(&voy_m);
  gfx_rect(0, CHART_Y0 + CHART_H, LCD_X, 1, 1);
  gfx_rect(VOY_W, CHART_Y0, 1, CHART_H, 1);
  /* the panel */
  const int X = VOY_W + 6, XR = LCD_X - 2;
  int y = 18;
  fmt_dt(t, voy_etd); nt(X, y, "DEP"); nt_r(XR, y, t); y += 16;
  fmt_dt(t, voy_etd + hours / 24); nt(X, y, "ARR"); nt_r(XR, y, t); y += 16;
  fmt_dur(t, el); nt(X, y, "GONE"); nt_r(XR, y, t); y += 16;
  fmt_dur(t, hours - el); nt(X, y, "TO GO"); nt_r(XR, y, t); y += 16;
  sprintf(t, "%.1f", sd / NM); nt(X, y, "RUN NM"); nt_r(XR, y, t); y += 16;
  sprintf(t, "%.1f", (tot - sd) / NM); nt(X, y, "DTG NM"); nt_r(XR, y, t); y += 16;
  sprintf(t, "%03.0f" NT_DEG "  %.4g kn", crs, voy_kn); nt(X, y, "CRS"); nt_r(XR, y, t); y += 16;
  fmt_dmn(t, lat, 2, lat < 0 ? 'S' : 'N'); nt_r(XR, y, t); y += 16;
  double lo = fmod(lon + 540.0, 360.0) - 180;
  fmt_dmn(t, lo, 3, lo < 0 ? 'W' : 'E'); nt_r(XR, y, t); y += 16;
  int pw = XR - X, px = (int)((double)pw * voy_step / VOY_STEPS);            /* the progress */
  gfx_frame(X, y + 2, pw, 8); gfx_rect(X, y + 2, px, 8, 1);
  if (voy_step >= VOY_STEPS) strcpy(s, plane ? "LANDED  R/S again" : "ARRIVED  R/S again");
  else sprintf(s, "%s 4 6 step ENT kn 0 ETA", voy_play ? "R/S pause" : "R/S play");
  if (voy_edit) { sprintf(s, "SPEED kn: %s_  ENTER ok  EXIT", ebuf); nt_inv(2, 192, s); return; }
  nt(2, 192, s);
}

static void voy_speed(int dir) {
  int n = (int)(sizeof VOY_KN / sizeof *VOY_KN), k = 0;
  while (k < n - 1 && VOY_KN[k] < voy_kn - 1e-9) k++;
  if (dir > 0 && VOY_KN[k] <= voy_kn + 1e-9 && k < n - 1) k++;
  else if (dir < 0 && k > 0) k--;
  voy_kn = VOY_KN[k];
  route_save();
}

/* the speed typed: digits and a point, ENTER keeps it (1 to 1000 kn) */
static void voy_edit_key(int key) {
  int d = digit(key);
  if (key == KEY_ENTER) {
    double v = atof(ebuf);
    if (elen && v >= 1 && v <= 1000) { voy_kn = v; route_save(); }
    voy_edit = 0;
    return;
  }
  if (key == KEY_EXIT) { voy_edit = 0; return; }
  if (key == KEY_BSP) { if (elen) ebuf[--elen] = 0; return; }
  if (key == KEY_DOT && elen < 6 && !strchr(ebuf, '.')) { ebuf[elen++] = '.'; ebuf[elen] = 0; return; }
  if (d >= 0 && elen < 6) { ebuf[elen++] = (char)('0' + d); ebuf[elen] = 0; }
}

static void voy_key(int key) {
  if (key == KEY_ENTER) { voy_play = 0; voy_edit = 1; elen = 0; ebuf[0] = 0; return; }
  if (key == KEY_RUN) {
    if (voy_step >= VOY_STEPS) voy_step = 0;
    voy_play = !voy_play;
  }
  else if (key == KEY_6 && voy_step < VOY_STEPS) { voy_step++; voy_play = 0; }
  else if (key == KEY_4 && voy_step > 0) { voy_step--; voy_play = 0; }
  else if (key == KEY_9) map_zoom(&voy_m, 1.5f);
  else if (key == KEY_7) map_zoom(&voy_m, 1 / 1.5f);
  else if (key == KEY_ADD) voy_speed(1);
  else if (key == KEY_SUB) voy_speed(-1);
  else if (key == KEY_1 || key == KEY_3) { voy_etd += (key == KEY_3 ? 1.0 : -1.0) / 24; voy_etd0 = voy_etd; route_save(); }
  else if (key == KEY_0) go(PG_ETA);
  else if (key == KEY_5) { voy_step = 0; voy_play = 0; }
  else if (key == KEY_EXIT) { voy_play = 0; go(voy_back); if (voy_back == PG_ROUTE) menu_page = 3; }
}

/* ------------------------------------------------------------------ */
/*  ETA: ETD, ETA and speed for the plan's distance: set two, the third follows */
/* ------------------------------------------------------------------ */
enum { E_ETD_D, E_ETD_T, E_ETA_D, E_ETA_T, E_SPEED, E_COUNT };
static int eta_sel = E_ETA_D, eta_edit = -1, eta_keep_eta;   /* keep_eta: a new ETD keeps the ETA (the speed follows) */
static char eta_msg[40];

static double plan_nm(void) {
  if (!geo_route_alloc()) return 0;
  if (plan_dirty || legs_dirty) plan_build();
  return geo_route.n >= 2 ? geo_route.w[geo_route.n - 1].cum_m / NM : 0;
}
static double etd_now(void) { return voy_etd0 > 0 ? voy_etd0 : ut_now(); }

static void eta_line(int y, int f, const char *label, const char *val, const char *hint) {
  nt(4, y, label);
  if (eta_edit == f) { char s[40]; sprintf(s, "%s_", ebuf); nt_inv(150, y, s); nt(300, y, hint); }
  else if (eta_sel == f) nt_inv(150, y, val); else nt(150, y, val);
}

static void draw_eta(void) {
  char s[64], a[24];
  header("ETA");
  double nm = plan_nm();
  if (nm <= 0) { nt(4, 48, "Needs a plan: ROUTE, 2 waypoints, R/S."); return; }
  double etd = etd_now(), hrs = nm / voy_kn, eta = etd + hrs / 24, tz = show_tz();
  int y, m, d;
  double h;
  sprintf(s, "%.1f NM  (%s %s)", nm, SAIL_NAME[N->cfg.gc % 3], N->cfg.lt ? "times LT" : "times UT");
  nt(4, 20, "DISTANCE"); nt(150, 20, s);
  jd_to_ymd(etd + tz / 24 + 0.5 / 1440, &y, &m, &d, &h);
  sprintf(a, "%04d-%02d-%02d", y, m, d); eta_line(44, E_ETD_D, "ETD date", a, "YYYY.MMDD");
  sprintf(a, "%02d:%02d%s", (int)h, (int)((h - (int)h) * 60), voy_etd0 > 0 ? "" : "  (now)"); eta_line(60, E_ETD_T, "ETD time", a, "HH.MM");
  jd_to_ymd(eta + tz / 24 + 0.5 / 1440, &y, &m, &d, &h);
  sprintf(a, "%04d-%02d-%02d", y, m, d); eta_line(84, E_ETA_D, "ETA date", a, "YYYY.MMDD");
  sprintf(a, "%02d:%02d", (int)h, (int)((h - (int)h) * 60)); eta_line(100, E_ETA_T, "ETA time", a, "HH.MM");
  sprintf(a, "%.2f kn", voy_kn); eta_line(124, E_SPEED, "SPEED", a, "1..1000");
  fmt_dur(a, hrs); nt(4, 148, "TIME"); nt(150, 148, a);
  sprintf(s, "a new ETD keeps the %s", eta_keep_eta ? "ETA (the speed follows)" : "SPEED (the ETA follows)");
  nt(4, 168, s);
  if (eta_msg[0]) nt_inv(2, 192, eta_msg);
  else nt(2, 192, "8 2 field ENTER type 0 keep EXIT");
}

/* the typed value into its field */
static void eta_commit(int f) {
  double v = atof(ebuf), nm = plan_nm(), etd = etd_now(), eta = etd + nm / voy_kn / 24, tz = show_tz();
  int y, m, d;
  double h;
  eta_msg[0] = 0;
  if (!elen) return;
  if (f == E_SPEED) {
    if (v >= 1 && v <= 1000) voy_kn = v; else strcpy(eta_msg, "speed: 1 to 1000 kn");
  } else {
    double old = f <= E_ETD_T ? etd : eta, jd = old;
    jd_to_ymd(old + tz / 24, &y, &m, &d, &h);
    if (f == E_ETD_D || f == E_ETA_D) {                       /* YYYY.MMDD, the time kept */
      int yy = (int)v, mm = (int)floor((v - yy) * 100 + 1e-6), dd = (int)lround(((v - yy) * 100 - mm) * 100);
      if (yy > 1900 && mm >= 1 && mm <= 12 && dd >= 1 && dd <= 31) jd = jd_ymd(yy, mm, dd, h) - tz / 24;
      else { strcpy(eta_msg, "date: YYYY.MMDD"); return; }
    } else {                                                  /* HH.MM, the date kept */
      double hh = flow_dm(v);
      if (hh >= 0 && hh < 24) jd = jd_ymd(y, m, d, hh) - tz / 24;
      else { strcpy(eta_msg, "time: HH.MM"); return; }
    }
    if (f <= E_ETD_T) {                                       /* a new departure */
      if (eta_keep_eta) {
        double hrs = (eta - jd) * 24;
        if (hrs <= 0 || nm / hrs > 1000 || nm / hrs < 1) { strcpy(eta_msg, "ETD after the ETA, or 1..1000 kn"); return; }
        voy_kn = nm / hrs;
      }
      voy_etd0 = jd;
    } else {                                                  /* a new arrival: the speed it needs */
      double hrs = (jd - etd) * 24;
      if (hrs <= 0) { strcpy(eta_msg, "ETA before the ETD"); return; }
      if (nm / hrs > 1000 || nm / hrs < 1) { strcpy(eta_msg, "needs < 1 or > 1000 kn"); return; }
      voy_kn = nm / hrs;
      if (voy_etd0 <= 0) voy_etd0 = etd;                      /* the ETD is now fixed too */
    }
  }
  route_save();
}

static void eta_edit_key(int key) {
  int d = digit(key);
  if (key == KEY_ENTER) { eta_commit(eta_edit); eta_edit = -1; return; }
  if (key == KEY_EXIT) { eta_edit = -1; return; }
  if (key == KEY_BSP) { if (elen) ebuf[--elen] = 0; return; }
  if (key == KEY_DOT && elen < 10 && !strchr(ebuf, '.')) { ebuf[elen++] = '.'; ebuf[elen] = 0; return; }
  if (d >= 0 && elen < 10) { ebuf[elen++] = (char)('0' + d); ebuf[elen] = 0; }
}

static void eta_key(int key) {
  eta_msg[0] = 0;
  if (key == KEY_8 || key == KEY_UP) eta_sel = (eta_sel + E_COUNT - 1) % E_COUNT;
  else if (key == KEY_2 || key == KEY_DOWN) eta_sel = (eta_sel + 1) % E_COUNT;
  else if (key == KEY_ENTER) { eta_edit = eta_sel; elen = 0; ebuf[0] = 0; }
  else if (key == KEY_0) eta_keep_eta ^= 1;
  else if (key == KEY_EXIT) { go(PG_VOY); voy_etd = etd_now(); }
}

/* ------------------------------------------------------------------ */
/*  ETA TABLE: the ETAs at the set speed and three slower ones (0.5 kn apart, or the step chosen) */
/* ------------------------------------------------------------------ */
static double etat_base = -1, etat_step = 0.5;
static int etat_sel;

static void draw_etat(void) {
  char s[64], a[24], d[24];
  header("ETA TABLE");
  double nm = plan_nm();
  if (nm <= 0) { nt(4, 48, "Needs a plan: ROUTE, 2 waypoints, R/S."); return; }
  if (etat_base < 0) etat_base = voy_kn;
  double etd = etd_now(), tz = show_tz();
  int y, m, dd;
  double h;
  jd_to_ymd(etd + tz / 24 + 0.5 / 1440, &y, &m, &dd, &h);
  sprintf(s, "%.1f NM   ETD %02d-%02d %02d:%02d %s", nm, dd, m, (int)h, (int)((h - (int)h) * 60), show_name());
  nt(4, 20, s);
  nt(4, 44, "SPEED"); nt_r(200, 44, "TIME"); nt_r(396, 44, "ETA");
  gfx_rect(2, 61, 396, 1, 1);
  for (int k = 0; k < 4; k++) {
    double kn = etat_base - k * etat_step, yy = 70 + 28 * k;
    if (kn < 0.5) break;
    double hrs = nm / kn;
    jd_to_ymd(etd + hrs / 24 + tz / 24 + 0.5 / 1440, &y, &m, &dd, &h);
    sprintf(s, "%.2f kn%s", kn, fabs(kn - voy_kn) < 1e-6 ? " *" : ""); fmt_dur(a, hrs);
    sprintf(d, "%02d-%02d %02d:%02d", dd, m, (int)h, (int)((h - (int)h) * 60));
    if (k == etat_sel) { gfx_rect(2, (int)yy - 2, 396, 20, 1); nt_inv(6, (int)yy, s); nt_inv(200 - nt_w(a), (int)yy, a); nt_inv(396 - nt_w(d), (int)yy, d); }
    else { nt(6, (int)yy, s); nt_r(200, (int)yy, a); nt_r(396, (int)yy, d); }
  }
  sprintf(s, "* set  8 2 speed 4 6 step %.2g kn 5 ENT use", etat_step);
  nt(2, 192, s);
}

static void etat_key(int key) {
  static const double STEPS[3] = { 0.25, 0.5, 1.0 };
  int k = etat_step < 0.3 ? 0 : etat_step < 0.7 ? 1 : 2;
  if (key == KEY_8 || key == KEY_UP) { if (etat_sel > 0) etat_sel--; else etat_base += etat_step; }
  else if (key == KEY_2 || key == KEY_DOWN) { if (etat_sel < 3 && etat_base - (etat_sel + 1) * etat_step >= 0.5) etat_sel++; else if (etat_base - etat_step >= 0.5 + 3 * etat_step) etat_base -= etat_step; }
  else if (key == KEY_4 && k > 0) etat_step = STEPS[k - 1];
  else if (key == KEY_6 && k < 2) etat_step = STEPS[k + 1];
  else if (key == KEY_5) { etat_base = voy_kn; etat_sel = 0; }
  else if (key == KEY_ENTER) {                                /* this line's speed for the voyage */
    double kn = etat_base - etat_sel * etat_step;
    if (kn >= 1 && kn <= 1000) { voy_kn = kn; route_save(); }
  }
  else if (key == KEY_EXIT) go(PG_ETA);
}

/* the animation: a step every 250 ms while playing */
static void voy_tick(void) {
  if (voy_step < VOY_STEPS) voy_step++;
  if (voy_step >= VOY_STEPS) voy_play = 0;
}

static void step_time(double hours);
static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }
/* ------------------------------------------------------------------ */
/*  DP OPS: station keeping off a platform (src/dpops.c, DPCAP47's engine). The platform comes in /NAV47/DPOPS.TXT  */
/*  (made on the web page), the vessel in a DPCAP47 .DPV (VESSEL line, or /NAV47/VESSEL.DPV; else the example). The */
/*  wind is SET's (FLOW's own), the current the FLOW program's at the platform (or typed), the sea DNV's for the     */
/*  wind (or typed).                                                                                                  */
/* ------------------------------------------------------------------ */
#define DPOPSFILE  "/NAV47/DPOPS.TXT"
#define DPVESSEL   "/NAV47/VESSEL.DPV"
typedef struct {
  Vessel v;
  DpPlat p;
  DpWx w;
  DpRes r;
  int loaded, face, manual, showfail, tonnes, sel, edit;
  float hdg, limw;
  char msg[48], csrc[24], vsrc[24];
} DpS;
static DpS *DP;

/* the current now at a position, as T&F: the FLOW atlas (springs scaled, the wind added), else a stream station
   within 30 NM (AUTO); 0: none */
static int stream_at(double lat, double lon, double ut, double *set, double *kn, char *src, int srcn) {
  const FlowAtlas *fl = atlas();
  int rs = 0, rr = 0;
  const TStation *ref = fl ? find_station(fl->ref, &rs, &rr) : NULL;
  double FU[13], FV[13], U[13], V[13], u, v;
  long n0, on;
  int om = 0;
  double h0, t0, hh, rng, fac;
  if (ref && flow_cell(fl, lat, lon, FU, FV)) {
    st_local(ref, ut, &n0, &h0);
    flow_pick(ref, n0, h0, &on, &om);
    flow_cycle(ref, on, om, &t0, &hh, &rng, &fac);
    flow_rows(FU, FV, fac, N->cfg.wdir, N->cfg.wkn, U, V);
    flow_now(U, V, ((double)(n0 - on) * 24.0 + h0) - t0, &u, &v);
    setkn(u, v, set, kn);
    snprintf(src, (size_t)srcn, "FLOW atlas");
    return 1;
  }
  double dn;
  const TStation *st = nearest_station(lat, lon, 1, &dn, NULL, NULL, NULL);
  if (st && dn <= 30 && st->kind != TK_TIDE) {
    double val, dir;
    st_local(st, ut, &n0, &h0);
    stream_now(st, n0, h0, &val, &dir);
    *kn = fabs(val); *set = dir;
    snprintf(src, (size_t)srcn, "%.12s %.0fNM", st->name, dn);
    return 1;
  }
  *set = 0; *kn = 0;
  snprintf(src, (size_t)srcn, "no stream: type it");
  return 0;
}

static void dp_weather(void) {
  Cfg *c = &N->cfg;
  DpWx *w = &DP->w;
  w->wfrom = (float)c->wdir; w->wkn = (float)c->wkn;
  double se = c->dcset, kn = c->dckn;
  if (c->dcauto) {
    double lat = DP->p.lat || DP->p.lon ? DP->p.lat : c->lat, lon = DP->p.lat || DP->p.lon ? DP->p.lon : c->lon;
    stream_at(lat, lon, ut_now(), &se, &kn, DP->csrc, sizeof DP->csrc);
  } else strcpy(DP->csrc, "typed");
  w->cset = (float)se; w->ckn = (float)kn;
  if (c->dsauto) { w->vfrom = w->wfrom; dpo_waves_auto(w); }
  else { w->vfrom = (float)c->dsfrom; w->hs = (float)c->dhs; w->tp = (float)c->dtp; }
}

static void dp_load(void) {
  if (!DP) { DP = (DpS *)calloc(1, sizeof *DP); if (!DP) return; }
  char err[48];
  char *t = read_text(DPOPSFILE);
  DP->msg[0] = 0;
  if (!t || dpo_parse(t, &DP->p, err, sizeof err)) {
    if (t) snprintf(DP->msg, sizeof DP->msg, "DPOPS.TXT %.30s", err);
    dpo_default(&DP->p);
  }
  free(t);
  t = read_text(DP->p.vessel[0] ? DP->p.vessel : DPVESSEL);
  memset(&DP->v, 0, sizeof DP->v);
  if (!t || dp_parse(&DP->v, t, err, sizeof err)) {
    if (t && !DP->msg[0]) snprintf(DP->msg, sizeof DP->msg, "vessel: %.30s", err);
    memset(&DP->v, 0, sizeof DP->v);
    dp_parse(&DP->v, DPO_EXAMPLE_VESSEL, err, sizeof err);
    strcpy(DP->vsrc, "example");
  } else strcpy(DP->vsrc, "file");
  free(t);
  dp_prepare(&DP->v);
  dpo_limits(&DP->v, &DP->p);
  DP->loaded = 1; DP->manual = 0; DP->limw = -1;
  DP->face = DP->p.face < 0 ? 0 : DP->p.face;
}

static void dp_save_plat(void) {
  if (!sys_disk_ok()) return;
  char *b = (char *)malloc(1024);
  if (!b) return;
  dpo_write(&DP->p, b, 1024);
  sys_disk_write_enable(1);
  check_create_dir(DIR);
  FIL f;
  UINT bw;
  if (f_open(&f, DPOPSFILE, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) { f_write(&f, b, (UINT)strlen(b), &bw); f_close(&f); }
  sys_disk_write_enable(0);
  free(b);
}

static void dp_busy(int n, int tot) {
  if (n % 8 && n != tot) return;
  char s[32];
  sprintf(s, "computing %d / %d", n, tot);
  gfx_rect(206, 150, 194, 16, 0);
  nt(206, 150, s);
  lcd_refresh();
}

/* BEST: every heading the side and the tolerance allow (every face with FACE AUTO), then the wind she holds to */
static void dp_best(void) {
  dp_weather();
  dp_abort = 0;
  dpo_best(&DP->v, &DP->p, &DP->w, 5, &DP->r, dp_busy);
  DP->face = DP->r.face; DP->hdg = DP->r.hdg; DP->manual = 0;
  DP->limw = dpo_limit_wind(&DP->v, &DP->p, &DP->w, DP->face, DP->hdg);
}

static void dp_eval_here(void) {
  dp_weather();
  dpo_eval(&DP->v, &DP->p, &DP->w, DP->face, DP->hdg, &DP->r);
  DP->limw = -1;
}

static void dp_enter(void) {
  if (!DP || !DP->loaded) dp_load();
  if (DP) dp_best();
}

/* the picture: north up, framed on the platform and the vessel; the platform hatched, its working face thick, the
   standoff line dashed, the face's centre line dotted (the vessel centred on it); the vessel, its thrusters' forces
   (arrows), the weather's whole push (a thick arrow, tonnes); the weather's directions bottom left */
#define DP_T 9.80665f                                     /* kN a tonne-force */
static void dp_arrow_w(int x, int y, float to, int len, int thick) {   /* an arrow towards true `to` */
  for (int k = -thick; k <= thick; k++) {
    int ox = (int)lroundf(k * cosf(to * (float)(M_PI / 180))), oy = (int)lroundf(k * sinf(to * (float)(M_PI / 180)));
    gfx_line(x + ox, y + oy, x + ox + (int)lroundf(len * sinf(to * (float)(M_PI / 180))), y + oy - (int)lroundf(len * cosf(to * (float)(M_PI / 180))), 1);
  }
  arrow(x + (int)lroundf((len - 1) * sinf(to * (float)(M_PI / 180))), y - (int)lroundf((len - 1) * cosf(to * (float)(M_PI / 180))), to, 1 + thick);
}

static void dp_picture(void) {
  DpS *d = DP;
  DpRes *r = &d->r;
  float hull[7][2], pc[4][2], o = d->p.orient * (float)(M_PI / 180);
  float a0x = sinf(o), a0y = cosf(o), a1x = cosf(o), a1y = -sinf(o);
  static const float sx[4] = { 1, 1, -1, -1 }, sy[4] = { 1, -1, -1, 1 };
  for (int k = 0; k < 4; k++) {
    pc[k][0] = sx[k] * d->p.len / 2 * a0x + sy[k] * d->p.wid / 2 * a1x;
    pc[k][1] = sx[k] * d->p.len / 2 * a0y + sy[k] * d->p.wid / 2 * a1y;
  }
  dpo_hull(&d->v, &d->p, r->hdg, r->px, r->py, hull);
  /* the frame: the box round the platform and the vessel, a margin for the arrows */
  float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
  for (int k = 0; k < 4; k++) { x0 = fminf(x0, pc[k][0]); x1 = fmaxf(x1, pc[k][0]); y0 = fminf(y0, pc[k][1]); y1 = fmaxf(y1, pc[k][1]); }
  for (int k = 0; k < 7; k++) { x0 = fminf(x0, hull[k][0]); x1 = fmaxf(x1, hull[k][0]); y0 = fminf(y0, hull[k][1]); y1 = fmaxf(y1, hull[k][1]); }
  float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, R = 1;
  for (int k = 0; k < 4; k++) R = fmaxf(R, hypotf(pc[k][0] - cx, pc[k][1] - cy));
  for (int k = 0; k < 7; k++) R = fmaxf(R, hypotf(hull[k][0] - cx, hull[k][1] - cy));
  const int X0 = 100, Y0 = 110, RING = 84;                    /* the drawing inside a compass ring, as a DP console */
  float sc = 64 / R;
#define PX(x) (X0 + (int)lroundf(((x) - cx) * sc))
#define PY(y) (Y0 - (int)lroundf(((y) - cy) * sc))
  /* the platform: hatched, its outline */
  int bx0 = 999, bx1 = -999, by0 = 999, by1 = -999;
  for (int k = 0; k < 4; k++) { bx0 = imin(bx0, PX(pc[k][0])); bx1 = imax(bx1, PX(pc[k][0])); by0 = imin(by0, PY(pc[k][1])); by1 = imax(by1, PY(pc[k][1])); }
  for (int y = imax(by0, 17); y <= imin(by1, MENU_Y - 1); y++)
    for (int x = imax(bx0, 0); x <= imin(bx1, 199); x++) {
      if ((x + y) & 1) continue;
      float wx = cx + (x - X0) / sc, wy = cy - (y - Y0) / sc;
      if (fabsf(wx * a0x + wy * a0y) <= d->p.len / 2 && fabsf(wx * a1x + wy * a1y) <= d->p.wid / 2) gfx_pixel(x, y, 1);
    }
  for (int k = 0; k < 4; k++) gfx_line(PX(pc[k][0]), PY(pc[k][1]), PX(pc[(k + 1) % 4][0]), PY(pc[(k + 1) % 4][1]), 1);
  static const int fk[4][2] = { { 0, 1 }, { 3, 0 }, { 2, 3 }, { 1, 2 } };   /* face 0 +a0 (corners 0 1), 1 +a1 (3 0), 2 -a0, 3 -a1 */
  int f = d->face & 3;
  float nb = dpo_face_normal(&d->p, f) * (float)(M_PI / 180), nx = sinf(nb), ny = cosf(nb);
  for (int t = 1; t <= 3; t++) {                              /* the working face: 3 px, just outside */
    int ox = (int)lroundf(t * nx), oy = -(int)lroundf(t * ny);
    gfx_line(PX(pc[fk[f][0]][0]) + ox, PY(pc[fk[f][0]][1]) + oy, PX(pc[fk[f][1]][0]) + ox, PY(pc[fk[f][1]][1]) + oy, 1);
  }
  /* the standoff line (dashed, along the face) and the face's centre line (dotted, the vessel centred on it) */
  float dd = ((f & 1) ? d->p.wid : d->p.len) / 2 + d->p.standoff, span = dpo_loa(&d->v, &d->p) * 0.6f;
  for (float t = -span; t <= span; t += 2.5f / sc) {
    if (((int)((t + span) * sc / 3)) & 1) continue;
    int x = PX(nx * dd + ny * t), y = PY(ny * dd - nx * t);
    if ((x - X0) * (x - X0) + (y - Y0) * (y - Y0) < (RING - 3) * (RING - 3)) gfx_pixel(x, y, 1);
  }
  float far = dd + dpo_loa(&d->v, &d->p) + d->v.b;
  for (float t = ((f & 1) ? d->p.wid : d->p.len) / 2; t <= far; t += 4 / sc) {
    int x = PX(nx * t), y = PY(ny * t);
    if ((x - X0) * (x - X0) + (y - Y0) * (y - Y0) < (RING - 3) * (RING - 3)) gfx_pixel(x, y, 1);
  }
  /* the vessel: white, its outline, the helideck at the bow */
  for (int y = 17; y < MENU_Y; y++)
    for (int x = 0; x < 200; x++) {
      float wx = cx + (x - X0) / sc, wy = cy - (y - Y0) / sc;
      int in = 0;
      for (int i = 0, j = 6; i < 7; j = i++)
        if ((hull[i][1] > wy) != (hull[j][1] > wy) &&
            wx < (hull[j][0] - hull[i][0]) * (wy - hull[i][1]) / (hull[j][1] - hull[i][1]) + hull[i][0]) in = !in;
      if (in) gfx_pixel(x, y, 0);
    }
  for (int i = 0; i < 7; i++) gfx_line(PX(hull[i][0]), PY(hull[i][1]), PX(hull[(i + 1) % 7][0]), PY(hull[(i + 1) % 7][1]), 1);
  float L = dpo_loa(&d->v, &d->p), h = r->hdg * (float)(M_PI / 180), fx = sinf(h), fy = cosf(h), sbx = cosf(h), sby = -sinf(h);
  gfx_circle(PX(r->px + fx * L * 0.33f), PY(r->py + fy * L * 0.33f), imax(2, (int)lroundf(d->v.b * 0.32f * sc)), 1);
  /* the thrusters' forces: an arrow each, from the thruster, the longest 26 px; the weather's push, thick, at midships */
  float (*F)[2] = d->showfail ? r->ff : r->f, fmax = r->fmag;
  for (int i = 0; i < d->v.nthr; i++) fmax = fmaxf(fmax, hypotf(F[i][0], F[i][1]));
  for (int i = 0; i < d->v.nthr; i++) {
    const Thruster *t = &d->v.thr[i];
    float wx = r->px + t->x * fx + t->y * sbx, wy = r->py + t->x * fy + t->y * sby;
    int tx = PX(wx), ty = PY(wy);
    gfx_rect(tx - 1, ty - 1, 3, 3, 1);
    float m = hypotf(F[i][0], F[i][1]);
    if (m < 0.01f * fmax || m < 0.5f) continue;
    float ex = F[i][0] * fx + F[i][1] * sbx, ey = F[i][0] * fy + F[i][1] * sby;      /* the thrust in the world */
    float to = atan2f(ex, ey) * (float)(180 / M_PI);
    dp_arrow_w(tx, ty, to, 4 + (int)lroundf(22 * m / fmax), 0);
  }
#undef PX
#undef PY
  /* the weather's whole push (x 1.25): a circle in the lower right corner, its direction above, tonnes below */
  {
    const int CX = 181, CY = 183, CR = 11;
    char s[16];
    gfx_rect(CX - 14, CY - CR - 2, 29, 2 * CR + 5, 0);
    gfx_circle(CX, CY, CR, 1);
    if (r->fmag > 0.5f) {
      arrow(CX - (int)lround((CR - 2) * dsin(r->fto)), CY + (int)lround((CR - 2) * dcos(r->fto)), r->fto, 2 * CR - 4);
      sprintf(s, "%03.0f", r->fto);
    } else strcpy(s, "-");
    draw_text(&TINY, CX - 3 * (int)strlen(s), CY - CR - 11, s, 0);
    sprintf(s, "%.1ft", r->fmag / DP_T);
    draw_text(&TINY, CX - 3 * (int)strlen(s), CY + CR + 3, s, 0);
  }
  /* the compass ring; the wind (W), the current (C) and the sea (S) as arrows on it pointing in, from where they come
     (the current's from where it sets from); arrows close together set apart */
  for (int a = 0; a < 360; a += 3) gfx_pixel(X0 + (int)lround(RING * dsin(a)), Y0 - (int)lround(RING * dcos(a)), 1);
  for (int a = 0; a < 360; a += 90) {
    gfx_line(X0 + (int)lround((RING - 4) * dsin(a)), Y0 - (int)lround((RING - 4) * dcos(a)),
             X0 + (int)lround((RING + 3) * dsin(a)), Y0 - (int)lround((RING + 3) * dcos(a)), 1);
  }
  struct { float from; const char *l; float v; } wx[3] = { { d->w.wfrom, "W", d->w.wkn }, { d->w.cset + 180, "C", d->w.ckn },
                                                       { d->w.vfrom, "S", d->w.hs } };
  for (int k = 0; k < 3; k++) {
    if (wx[k].v <= 0) continue;
    double a = wx[k].from;
    int shift = 0;
    for (int j = 0; j < k; j++) {
      double dd = fabs(fmod(wx[j].from - a + 540, 360) - 180);
      if (wx[j].v > 0 && dd < 14) shift++;
    }
    int px0 = (int)lround(shift * 9 * dcos(a)), py0 = (int)lround(shift * 9 * dsin(a));   /* sideways */
    int tx = X0 + (int)lround((RING + 8) * dsin(a)) + px0, ty = Y0 - (int)lround((RING + 8) * dcos(a)) + py0;
    arrow(tx, ty, a + 180, 18);
    int lx = X0 + (int)lround((RING - 19) * dsin(a)) + px0 - 2, ly = Y0 - (int)lround((RING - 19) * dcos(a)) + py0 - 4;   /* inside, at its tip */
    lx = imax(1, imin(194, lx)); ly = imax(18, imin(198, ly));
    gfx_rect(lx - 1, ly - 1, 7, 9, 0);
    draw_text(&TINY, lx, ly, wx[k].l, 0);
  }
  if (r->mid > d->p.standoff + 0.05f || r->clear < d->p.standoff - 0.05f) {   /* turned: moved out for a corner */
    char s[32];
    sprintf(s, "MID %.1fm CORNER %.1fm", r->mid, r->clear);
    gfx_rect(0, 198, 120, 9, 0); draw_text(&TINY, 1, 199, s, 0);
  }
  gfx_rect(0, 17, 22, 24, 0);
  arrow(8, 36, 0, 14); draw_text(&TINY, 12, 20, "N", 0);
  for (int y = 17; y < MENU_Y; y += 2) lcd_fill_rect(201, (uint32_t)y, 1, 1, 1);
}

static void draw_dp(void) {
  header("DP OPS");
  if (!DP) { nt(4, 40, "No memory for DP OPS."); return; }
  DpS *d = DP;
  DpRes *r = &d->r;
  char s[64];
  dp_picture();
  const int X = 206;
  sprintf(s, "FACE %03.0f%s %s%s", dpo_face_normal(&d->p, d->face), d->p.face < 0 ? "*" : "", DPO_SIDE[d->p.side],
          dpo_tol(&d->p) == 0 && d->p.side != DPS_STERN && d->p.side != DPS_BOW ? " PAR" : "");
  nt(X, 18, s);
  sprintf(s, "HDG %03.0f%s  %.0fm OFF", r->hdg, d->manual ? " SET" : " BEST", r->mid);
  nt(X, 34, s);
  sprintf(s, "WIND %03.0f %.0fkn", d->w.wfrom, d->w.wkn);
  nt(X, 50, s);
  if (r->wleft < 0.995f) { sprintf(s, "LEE %.0f%%", (1 - r->wleft) * 100); draw_text(&TINY, X + 138, 54, s, 0); }
  sprintf(s, "CUR %03.0f %.1fkn", d->w.cset, d->w.ckn);
  nt(X, 66, s);
  draw_text(&TINY, X + 138, 70, N->cfg.dcauto ? "FLOW" : "TYPED", 0);
  if (d->w.hs > 0) sprintf(s, "SEA %03.0f %.1fm %.0fs", d->w.vfrom, d->w.hs, d->w.tp); else strcpy(s, "SEA none");
  nt(X, 82, s);
  sprintf(s, "PUSH %.1ft >%03.0f %s", r->fmag / DP_T, r->fto, r->blow >= 0 ? "OFF" : "ON");   /* the weather's, x 1.25 */
  nt(X, 98, s);
  const char *st = r->held_lim ? "HOLDS IN LIMITS" : r->held_full ? "OVER THE LIMITS" : "NO HOLD";
  gfx_rect(X - 2, 114, 196, 16, 1);
  nt_inv(X, 114, st);
  sprintf(s, "FAIL G%d %s", r->fail_grp, r->fail_held ? "HOLDS" : "NO HOLD");
  nt(X, 130, s);
  if (d->limw >= 0) sprintf(s, "WIND LIMIT %s%.0fkn", d->limw >= DPO_WIND_TOP ? ">" : "", d->limw);
  else strcpy(s, "ENTER: BEST, LIMIT");
  nt(X, 146, s);
  /* the thrusters' bars: 100 % = 24 px, the limit a tick; intact or after the worst failure (0); % or tonnes (3) */
  int n = d->v.nthr, cw = n ? 192 / n : 192, top = 172, H = 24;
  if (d->showfail) sprintf(s, "GROUP %d LOST, %s", r->fail_grp, d->tonnes ? "TONNES" : "% OF FULL");
  else sprintf(s, "INTACT, %s", d->tonnes ? "TONNES" : "% OF FULL (- THE LIMIT)");
  draw_text(&TINY, X, 163, s, 0);
  for (int i = 0; i < n; i++) {
    float u = d->showfail ? r->fail_wuse[i] : r->use[i], lim = d->p.lim[i] > 0 ? d->p.lim[i] : 1;
    float (*F)[2] = d->showfail ? r->ff : r->f;
    int x = X + i * cw + 3, w = imin(cw - 8, 18), hgt = (int)lroundf(fminf(u, 1) * H);
    gfx_frame(x, top, w, H + 1);
    gfx_rect(x, top + H + 1 - hgt, w, hgt, 1);
    if (lim < 1 && !d->showfail) { int ly = top + H - (int)lroundf(lim * H); gfx_rect(x - 2, ly, w + 4, 1, 1); gfx_rect(x - 2, ly, 1, 1, 0); }
    if (d->tonnes) sprintf(s, "%.1f", hypotf(F[i][0], F[i][1]) / DP_T); else sprintf(s, "%.0f", u * 100);
    draw_text(&TINY, x + w + 1, top + H - 6, s, 0);
    draw_text(&TINY, x, top + H + 3, d->v.thr[i].name, 0);
  }
  if (d->msg[0]) draw_text(&TINY, 80, MENU_Y - LH, d->msg, 0);
}

static void dp_key(int key) {
  DpS *d = DP;
  if (!d) { if (key == KEY_EXIT) go(PG_VALM); return; }
  if (key == KEY_ENTER) dp_best();
  else if (key == KEY_4 || key == KEY_6) { d->hdg = fmodf(d->r.hdg + (key == KEY_4 ? 355 : 5), 360); d->manual = 1; dp_eval_here(); }
  else if (key == KEY_UP || key == KEY_DOWN) { step_time(key == KEY_UP ? 1 : -1); d->manual ? dp_eval_here() : dp_best(); }
  else if (key == KEY_5) { toff = 0; dp_best(); }
  else if (key == KEY_7 || key == KEY_9 || key == KEY_8 || key == KEY_2) {   /* the wind: 7 9 its direction, 8 2 its speed */
    Cfg *c = &N->cfg;
    if (key == KEY_7) c->wdir = fmod(c->wdir + 350, 360);
    else if (key == KEY_9) c->wdir = fmod(c->wdir + 10, 360);
    else if (key == KEY_8) c->wkn = fmin(99, c->wkn + 1);
    else c->wkn = fmax(0, c->wkn - 1);
    c->wdir = 10 * floor(c->wdir / 10 + 0.5); if (c->wdir >= 360) c->wdir -= 360;
    c->wkn = floor(c->wkn + 0.5);
    cfg_save();
    d->manual ? dp_eval_here() : dp_best();
  }
  else if (key == KEY_0) d->showfail ^= 1;
  else if (key == KEY_3) d->tonnes ^= 1;                       /* the bars: % of full thrust or tonnes */
  else if (key == KEY_1) { d->p.par ^= 1; dp_save_plat(); dp_best(); }   /* side to: parallel to the face, or within TOL */
  else if (key == KEY_EXIT) go(PG_VALM);
}

static void dp_soft(int fn) {
  DpS *d = DP;
  if (!d) { go(PG_VALM); return; }
  if (fn == 0) dp_best();
  else if (fn == 1) { d->p.face = d->p.face >= 3 ? -1 : d->p.face + 1; dp_save_plat(); if (d->p.face >= 0) d->face = d->p.face; dp_best(); }
  else if (fn == 2) { d->p.side = (d->p.side + 1) % DPS_N; dp_save_plat(); dp_best(); }
  else if (fn == 3) { go(PG_DPIN); d->sel = 0; d->edit = -1; }
  else if (fn == 4) { dp_load(); dp_best(); if (!d->msg[0]) snprintf(d->msg, sizeof d->msg, "%.14s, %.20s (%.7s)", d->p.name, d->v.name, d->vsrc); }
  else go(PG_VALM);
}

/* INPUT: the weather (the wind is SET's / FLOW's own), the current (FLOW or typed), the sea (DNV or typed),
   the standoff and the tolerance (saved to DPOPS.TXT) */
enum { DI_WDIR, DI_WKN, DI_CAUTO, DI_CSET, DI_CKN, DI_SAUTO, DI_SDIR, DI_HS, DI_TP, DI_OFF, DI_TOL, DI_N };
static const char *const DI_NAME[DI_N] = { "WIND FROM", "WIND KN", "CURRENT", "CUR SET TO", "CUR KN", "SEA", "SEA FROM",
                                           "HS m", "TP s", "STANDOFF m", "HEADING +/-" };

static void draw_dpin(void) {
  header("DP INPUT");
  if (!DP) return;
  dp_weather();
  Cfg *c = &N->cfg;
  DpWx *w = &DP->w;
  char s[48];
  for (int i = 0; i < DI_N; i++) {
    int y = 18 + 16 * i;
    switch (i) {
    case DI_WDIR: sprintf(s, "%03.0f" NT_DEG, w->wfrom); break;
    case DI_WKN: sprintf(s, "%.1f kn", w->wkn); break;
    case DI_CAUTO: sprintf(s, "%s", c->dcauto ? "FLOW" : "TYPED"); break;
    case DI_CSET: sprintf(s, "%03.0f" NT_DEG "%s", w->cset, c->dcauto ? "  (FLOW)" : ""); break;
    case DI_CKN: sprintf(s, "%.2f kn%s", w->ckn, c->dcauto ? "  (FLOW)" : ""); break;
    case DI_SAUTO: sprintf(s, "%s", c->dsauto ? "DNV, FROM THE WIND" : "TYPED"); break;
    case DI_SDIR: sprintf(s, "%03.0f" NT_DEG, w->vfrom); break;
    case DI_HS: sprintf(s, "%.1f", w->hs); break;
    case DI_TP: sprintf(s, "%.1f", w->tp); break;
    case DI_OFF: sprintf(s, "%.0f", DP->p.standoff); break;
    default: sprintf(s, "%.0f" NT_DEG, DP->p.tol); break;
    }
    if (i == DP->sel) { gfx_rect(0, y - 1, 140, 16, 1); nt_inv(4, y, DI_NAME[i]); } else nt(4, y, DI_NAME[i]);
    if (DP->edit == i) { char e[24]; sprintf(e, "%s_", ebuf); nt_inv(150, y, e); } else nt(150, y, s);
  }
  draw_text(&TINY, 260, 70, DP->csrc, 0);
  draw_text(&TINY, 2, MENU_Y - LH, "8 2 field  ENTER type  0 FLOW / DNV or typed  EXIT DP OPS", 0);
}

static void dpin_commit(int f) {
  if (!elen) return;
  double v = atof(ebuf);
  Cfg *c = &N->cfg;
  switch (f) {
  case DI_WDIR: if (v >= 0 && v <= 360) c->wdir = fmod(v, 360); break;
  case DI_WKN: if (v >= 0 && v < 100) c->wkn = v; break;
  case DI_CSET: if (v >= 0 && v <= 360) { c->dcset = fmod(v, 360); c->dcauto = 0; } break;
  case DI_CKN: if (v >= 0 && v < 10) { c->dckn = v; c->dcauto = 0; } break;
  case DI_SDIR: if (v >= 0 && v <= 360) { c->dsfrom = fmod(v, 360); c->dsauto = 0; } break;
  case DI_HS: if (v >= 0 && v < 20) { c->dhs = v; c->dsauto = 0; } break;
  case DI_TP: if (v >= 0 && v < 25) { c->dtp = v; c->dsauto = 0; } break;
  case DI_OFF: if (v >= 1 && v <= 500) { DP->p.standoff = (float)v; dp_save_plat(); } break;
  case DI_TOL: if (v >= 0 && v <= 90) { DP->p.tol = (float)v; dp_save_plat(); } break;
  }
  cfg_save();
}

static void dpin_key(int key) {
  DpS *d = DP;
  if (!d) return;
  Cfg *c = &N->cfg;
  if (d->edit >= 0) {
    int dg = digit(key);
    if (key == KEY_ENTER) { dpin_commit(d->edit); d->edit = -1; }
    else if (key == KEY_EXIT) d->edit = -1;
    else if (key == KEY_BSP) { if (elen) ebuf[--elen] = 0; }
    else if (key == KEY_DOT && elen < 10 && !strchr(ebuf, '.')) { ebuf[elen++] = '.'; ebuf[elen] = 0; }
    else if (dg >= 0 && elen < 10) { ebuf[elen++] = (char)('0' + dg); ebuf[elen] = 0; }
    return;
  }
  if (key == KEY_8 || key == KEY_UP) d->sel = (d->sel + DI_N - 1) % DI_N;
  else if (key == KEY_2 || key == KEY_DOWN) d->sel = (d->sel + 1) % DI_N;
  else if (key == KEY_0 || (key == KEY_ENTER && (d->sel == DI_CAUTO || d->sel == DI_SAUTO))) {
    if (d->sel >= DI_CAUTO && d->sel <= DI_CKN) {             /* typed: start from what FLOW gave */
      if (c->dcauto) { c->dcset = d->w.cset; c->dckn = d->w.ckn; }
      c->dcauto ^= 1;
    } else if (d->sel >= DI_SAUTO && d->sel <= DI_TP) {
      if (c->dsauto) { c->dsfrom = d->w.vfrom; c->dhs = d->w.hs; c->dtp = d->w.tp; }
      c->dsauto ^= 1;
    }
    cfg_save();
  } else if (key == KEY_ENTER) { d->edit = d->sel; elen = 0; ebuf[0] = 0; }
  else if (key == KEY_EXIT) { go(PG_DP); dp_best(); }
}

static const char *MENUS[4][6] = {
  { "ALMAN", "SPLIT", "SKY", "ALLSK", "MOON", ">>" },
  { "ANIM", "T&F", "TIDE", "ROUTE", "ZONES", ">>" },
  { "SET", "FILES", "INFO", "DP", "USB", ">>" },
  { "ADD", "EDIT", "DEL", "RL/GC", "HERE", ">>" },
};
static const char *FILE_MENU[6] = { "TID", "FLW", "CLEAR", "", "", "BACK" };
static const char *EDIT_MENU[6] = { "", "", "", "", "", "CANCEL" };
static const char *PLAN_MENU[6] = { "CSV", "NMEA", "SPLIT", "CMP", "CHART", "BACK" };
static const char *CMP_MENU[6] = { "ELL-", "ELL+", "EDIT", "PLAN", "CHART", "BACK" };
static const char *CHART_MENU[6] = { "ZOOM+", "ZOOM-", "FIT", "GLOBE", "PLAN", "BACK" };
static const char *VOY_MENU[6] = { "PLAY", "SLOWER", "FASTER", "ETA", "CHART", "BACK" };
static const char *ETA_MENU[6] = { "NOW", "TABLE", "", "KEEP", "VOYAGE", "BACK" };
static const char *ETAT_MENU[6] = { "SPEED+", "SPEED-", "SET", "USE", "ETA", "BACK" };
static const char *DP_MENU[6] = { "BEST", "FACE", "SIDE", "INPUT", "LOAD", "BACK" };
static const char *DPIN_MENU[6] = { "", "", "", "", "", "BACK" };

#define ANIM_FRAMES 24
#define ANIM_STEP   0.5

/* the views' almanac at the current time (computed again only when the time or the position changes) */
static void view_almanac_now(void) {
  double j = ut_now();
  if (!N->valid || N->vjd != j || N->al.lat != N->cfg.lat || N->al.lon != N->cfg.lon || N->al.tz != show_tz()) {
    al_compute(&N->al, j, N->cfg.lat, N->cfg.lon, show_tz());
    N->vjd = j;
    N->valid = 1;
  }
  N->al.local = N->cfg.lt;
}

static void redraw(void) {
  lcd_clear_buf();
  if (IS_VIEW(page)) {
    view_almanac_now();
    switch (page) {
    case PG_VALM: view_almanac(&N->al); break;
    case PG_VSPLIT: view_split(&N->al, &split_first); break;
    case PG_VSKY: view_sky(&N->al, vframe); break;
    case PG_VALLSKY: view_allsky(&N->al); break;
    case PG_VMOON: view_moon(&N->al, N->cfg.south); break;
    case PG_VANIM: view_anim(&N->al, ANIM_FRAMES, ANIM_STEP, vframe < ANIM_FRAMES ? vframe : ANIM_FRAMES - 1); break;
    case PG_VSKY2: view_sky2(&N->al); break;
    }
  } else
    switch (page) {
    case PG_TIDE: draw_tide(); break;
    case PG_STATION: draw_station(); break;
    case PG_ROUTE: draw_route(); break;
    case PG_PLAN: draw_plan(); break;
    case PG_CMP: draw_cmp(); break;
    case PG_CHART: draw_chart(); break;
    case PG_VOY: draw_voy(); break;
    case PG_ETA: draw_eta(); break;
    case PG_ETAT: draw_etat(); break;
    case PG_DP: draw_dp(); break;
    case PG_DPIN: draw_dpin(); break;
    case PG_ZONES: draw_zones(); break;
    case PG_SET: draw_set(); break;
    case PG_FILES: draw_files(); break;
    case PG_INFO: draw_info(); break;
    case PG_PICK: draw_pick(); break;
    }
  if (page != PG_ROUTE && menu_page == 3) menu_page = 0;
  MENUS[2][4] = usbview_is_on() ? "USB*" : "USB";
  MENUS[0][2] = page == PG_VSKY ? "SKY2" : "SKY";
  CHART_MENU[3] = chart_globe ? "MERC" : "GLOBE";
  VOY_MENU[0] = voy_play ? "PAUSE" : "PLAY";
  lcd_draw_menu_keys(page == PG_FILES ? FILE_MENU : (edit_field >= 0 || wp_edit >= 0 || cmp_edit >= 0 || voy_edit || eta_edit >= 0 || (page == PG_DPIN && DP && DP->edit >= 0)) ? EDIT_MENU :
                     page == PG_PLAN ? PLAN_MENU : page == PG_CMP ? CMP_MENU : page == PG_CHART ? CHART_MENU : page == PG_VOY ? VOY_MENU : page == PG_ETA ? ETA_MENU : page == PG_ETAT ? ETAT_MENU : page == PG_DP ? DP_MENU : page == PG_DPIN ? DPIN_MENU : MENUS[menu_page]);
  lcd_refresh();
  lcd_refresh_wait();        /* the LCD goes out by DMA: done before the next frame clears the buffer (no flicker) */
  usbview_send();
}

static int digit(int key) {
  static const int dk[10] = { KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9 };
  for (int i = 0; i < 10; i++) if (key == dk[i]) return i;
  return -1;
}

static void key_edit(int key) {
  int d = digit(key);
  if (key == KEY_ENTER) { field_commit(edit_field); edit_field = -1; cfg_save(); return; }
  if (key == KEY_EXIT) { edit_field = -1; return; }
  if (key == KEY_BSP) { if (elen) ebuf[--elen] = 0; return; }
  if (key == KEY_CHS) { if (ebuf[0] == '-') { memmove(ebuf, ebuf + 1, (size_t)elen); elen--; } else if (elen < 14) { memmove(ebuf + 1, ebuf, (size_t)elen + 1); ebuf[0] = '-'; elen++; } return; }
  if (key == KEY_DOT && elen < 14 && !strchr(ebuf, '.')) { ebuf[elen++] = '.'; ebuf[elen] = 0; return; }
  if (d >= 0 && elen < 14) { ebuf[elen++] = (char)('0' + d); ebuf[elen] = 0; }
}

static void go(int p) { page = p; vframe = 0; }

static void softkey(int fn) {
  if (page == PG_FILES) {
    if (fn == 0 || fn == 1) {
      check_create_dir(TIDEDIR);
      file_selection_screen(fn ? "Load FLOW atlas" : "Load tide stations", TIDEDIR, fn ? ".flw" : ".tid",
                            file_sel_cb, 0, 0, NULL);
      wait_for_key_release(-1);
      cfg_save();
    } else if (fn == 2) {
      N->cfg.nfiles = 0; N->nfr = 0; N->have_ffl = 0;
      if (N->cfg.tsrc == 2) N->cfg.tsrc = 0;
      strcpy(N->msg, "files cleared (memory is freed at the next start)");
      cfg_save();
    } else if (fn == 5) go(PG_SET);
    return;
  }
  if (page == PG_PLAN) {
    static const int k[6] = { KEY_ENTER, 0, KEY_5, 0, 0, KEY_EXIT };
    if (fn == 1) { plan_msg[0] = 0; plan_nmea(); }
    else if (fn == 3) go(PG_CMP);
    else if (fn == 4) chart_open(PG_PLAN);
    else plan_key(k[fn]);
    return;
  }
  if (page == PG_CMP) {
    static const int k[6] = { KEY_7, KEY_9, KEY_ENTER, 0, 0, KEY_EXIT };
    if (fn == 2 && geo_ell_index != ELL_CUSTOM) strcpy(cmp_msg, "EDIT: CUSTOM only (ELL+)");
    else if (fn == 3) { plan_cur = 0; go(PG_PLAN); }
    else if (fn == 4) chart_open(PG_CMP);
    else cmp_key(k[fn]);
    return;
  }
  if (page == PG_DP) { dp_soft(fn); return; }
  if (page == PG_DPIN) { if (fn == 5) dpin_key(KEY_EXIT); return; }
  if (page == PG_ETAT) {
    static const int k[6] = { KEY_8, KEY_2, KEY_5, KEY_ENTER, KEY_EXIT, KEY_EXIT };
    if (fn == 5) { go(PG_VOY); voy_etd = etd_now(); } else etat_key(k[fn]);
    return;
  }
  if (page == PG_ETA) {
    if (fn == 1) { etat_base = voy_kn; etat_sel = 0; go(PG_ETAT); return; }
    if (fn == 0) { voy_etd0 = 0; route_save(); }
    else if (fn == 3) eta_keep_eta ^= 1;
    else if (fn == 4 || fn == 5) { go(PG_VOY); voy_etd = etd_now(); }
    return;
  }
  if (page == PG_VOY) {
    static const int k[6] = { KEY_RUN, KEY_SUB, KEY_ADD, KEY_0, 0, KEY_EXIT };
    if (fn == 4) { voy_play = 0; chart_open(PG_PLAN); }
    else voy_key(k[fn]);
    return;
  }
  if (page == PG_CHART) {
    static const int k[6] = { KEY_9, KEY_7, KEY_5, KEY_0, 0, KEY_EXIT };
    if (fn == 4) { plan_cur = 0; go(PG_PLAN); }
    else chart_key(k[fn]);
    return;
  }
  if (fn == 5) {                                       /* >> */
    menu_page = menu_page == 3 ? 0 : (page == PG_ROUTE && menu_page == 2) ? 3 : (menu_page + 1) % 3;
    return;
  }
  switch (menu_page * 6 + fn) {
  case 0: go(PG_VALM); break;
  case 1: go(PG_VSPLIT); break;
  case 2: go(page == PG_VSKY ? PG_VSKY2 : PG_VSKY); break;         /* SKY again: SKY2, the dome */
  case 3: go(PG_VALLSKY); break;
  case 4: go(PG_VMOON); break;
  case 6: go(PG_VANIM); break;
  case 7: go(PG_TIDE); flow_hw_step = flow_day_step = 0; break;      /* T&F: tide and flow, the nearest station */
  case 8: go(PG_STATION); st_day_off = 0; break;                    /* TIDE: the chosen station */
  case 9: go(PG_ROUTE); menu_page = 3; break;
  case 10: go(PG_ZONES); break;
  case 12: go(PG_SET); sel_field = 0; break;
  case 13: go(PG_FILES); N->msg[0] = 0; break;
  case 14: go(PG_INFO); break;
  case 15: go(PG_DP); dp_enter(); break;                              /* DP OPS */
  case 16: if (usbview_is_on()) usbview_off(); else usbview_on(); go(PG_INFO); break;   /* INFO shows the state */
  case 18: route_insert(N->nwp ? N->wlat[wp_cur] : N->cfg.lat, N->nwp ? N->wlon[wp_cur] : N->cfg.lon); wp_start_edit(); wp_new = 1; break;
  case 19: wp_new = 0; wp_start_edit(); break;
  case 20: route_delete(); route_save(); break;
  case 21: N->cfg.gc = (N->cfg.gc + 1) % 3; route_save(); break;
  case 22: route_insert(N->cfg.lat, N->cfg.lon); route_save(); break;
  }
}

static void step_time(double hours) { toff += hours; }

static void handle_key(int key) {
  if (key == KEY_SHIFT) { shift = !shift; return; }
  if (shift) {
    shift = 0;
    if (key == KEY_EXIT) { cfg_save(); SET_ST(STAT_PGM_END); return; }
    if (key == KEY_ENTER && page == PG_PLAN) { plan_msg[0] = 0; plan_nmea(); return; }
    if (key == KEY_0) {
      static const uint8_t mid[] = { MI_SET_TIME, MI_SET_DATE, MI_BEEP_MUTE, MI_SLOW_AUTOREP, MI_MSC, MI_SYSTEM_ENTER, 0 };
      static const smenu_t MID = { "NAV47 Setup", mid, NULL, NULL };
      SET_ST(STAT_MENU);
      handle_menu(&MID, MENU_RESET, 0);
      CLR_ST(STAT_MENU);
      wait_for_key_release(-1);
      return;
    }
  }
  if ((edit_field >= 0 || wp_edit >= 0 || cmp_edit >= 0 || voy_edit || eta_edit >= 0) && key == KEY_F6) key = KEY_EXIT;
  if (edit_field >= 0) { key_edit(key); return; }
  if (wp_edit >= 0) { key_wp(key); return; }
  if (cmp_edit >= 0) { cmp_edit_key(key); return; }
  if (voy_edit) { voy_edit_key(key); return; }
  if (eta_edit >= 0) { eta_edit_key(key); return; }
  if (key >= KEY_F1 && key <= KEY_F6) { softkey(key - KEY_F1); vframe = 0; return; }
  if (key == KEY_CHS) { N->cfg.lt ^= 1; N->valid = 0; cfg_save(); return; }   /* +/-: the times in UT or LT, every page */
  if (IS_VIEW(page)) {
    /* as Almanac 47: up / down one hour later / earlier; 4 6 ten minutes, 7 9 a day, 5 now */
    if (key == KEY_UP) step_time(1);
    else if (key == KEY_DOWN) step_time(-1);
    else if (key == KEY_4) step_time(-1.0 / 6);
    else if (key == KEY_6) step_time(1.0 / 6);
    else if (key == KEY_7) step_time(-24);
    else if (key == KEY_9) step_time(24);
    else if (key == KEY_5) toff = 0;
    else if (key == KEY_0 && page == PG_VMOON) { N->cfg.south ^= 1; cfg_save(); }
    else if (key == KEY_0 && (page == PG_VSKY || page == PG_VALLSKY)) go(PG_VSKY2);
    else if (key == KEY_0 && page == PG_VSKY2) go(PG_VSKY);
    else if (key == KEY_8 && page == PG_VSPLIT) split_first--;
    else if (key == KEY_2 && page == PG_VSPLIT) split_first++;
    else if (key == KEY_EXIT) go(PG_VALM);
    vframe = 0;
    return;
  }
  switch (page) {
  case PG_ZONES:
    if (key == KEY_4) { N->cfg.zone = (N->cfg.zone + NZONES - 1) % NZONES; N->cfg.city = 0; }
    else if (key == KEY_6) { N->cfg.zone = (N->cfg.zone + 1) % NZONES; N->cfg.city = 0; }
    else if (key == KEY_8 || key == KEY_UP) N->cfg.city = (N->cfg.city + ZONES[N->cfg.zone].n - 1) % ZONES[N->cfg.zone].n;
    else if (key == KEY_2 || key == KEY_DOWN) N->cfg.city = (N->cfg.city + 1) % ZONES[N->cfg.zone].n;
    else if (key == KEY_5) { N->cfg.zone = zone_nearest(N->cfg.tz, ut_now()); N->cfg.city = 0; }
    break;
  case PG_TIDE:
    if (key == KEY_4) flow_hw_step--;
    else if (key == KEY_6) flow_hw_step++;
    else if (key == KEY_1) { flow_day_step--; flow_hw_step = 0; }
    else if (key == KEY_3) { flow_day_step++; flow_hw_step = 0; }
    else if (key == KEY_5) flow_hw_step = flow_day_step = 0;
    else if (key == KEY_ENTER) pick_open();
    else if (key == KEY_0) { N->cfg.tauto ^= 1; cfg_save(); }
    break;
  case PG_STATION:
    if (key == KEY_8 || key == KEY_UP) station_step(-1);
    else if (key == KEY_2 || key == KEY_DOWN) station_step(1);
    else if (key == KEY_4) st_day_off--;
    else if (key == KEY_6) st_day_off++;
    else if (key == KEY_5) st_day_off = 0;
    else if (key == KEY_ENTER) pick_open();
    else if (key == KEY_0) N->cfg.tauto ^= 1;
    if (key != KEY_EXIT) cfg_save();
    break;
  case PG_INFO: {                                       /* 8 2 scroll, 7 9 a page */
    int vis = (174 - 17) / LH, mx = info_lines - vis > 0 ? info_lines - vis : 0;
    if (key == KEY_8 || key == KEY_UP) info_top--;
    else if (key == KEY_2 || key == KEY_DOWN) info_top++;
    else if (key == KEY_7) info_top -= vis - 1;
    else if (key == KEY_9) info_top += vis - 1;
    if (info_top > mx) info_top = mx;
    if (info_top < 0) info_top = 0;
    return;
  }
  case PG_PICK:
    if (key == KEY_8 || key == KEY_UP) pick_move(-1);
    else if (key == KEY_2 || key == KEY_DOWN) pick_move(1);
    else if (key == KEY_ENTER || key == KEY_5) { pick_choose(); cfg_save(); }
    else if (key == KEY_EXIT) go(pick_back == PG_TIDE ? PG_TIDE : PG_STATION);
    return;
  case PG_ROUTE:
    if ((key == KEY_8 || key == KEY_UP) && wp_cur > 0) wp_cur--;
    else if ((key == KEY_2 || key == KEY_DOWN) && wp_cur < N->nwp - 1) wp_cur++;
    else if (key == KEY_ENTER) wp_start_edit();
    else if (key == KEY_RUN && N->nwp > 1) { plan_cur = 0; go(PG_PLAN); }
    else if (key == KEY_5 && N->nwp > 1) go(PG_CMP);
    else if (key == KEY_0) chart_open(PG_ROUTE);
    else if (key == KEY_4 && N->cfg.gc == 2 && cs_lim > 10) { cs_lim -= 1; route_save(); }
    else if (key == KEY_6 && N->cfg.gc == 2 && cs_lim < 85) { cs_lim += 1; route_save(); }
    break;
  case PG_PLAN:
    plan_key(key);
    return;
  case PG_CMP:
    cmp_key(key);
    return;
  case PG_CHART:
    if (key == KEY_RUN && N->nwp > 1) { voy_open(PG_CHART); return; }
    chart_key(key);
    return;
  case PG_VOY:
    voy_key(key);
    return;
  case PG_ETA:
    if (key == KEY_DIV || key == KEY_MUL) { etat_base = voy_kn; etat_sel = 0; go(PG_ETAT); return; }
    eta_key(key);
    return;
  case PG_ETAT:
    etat_key(key);
    return;
  case PG_DP:
    dp_key(key);
    return;
  case PG_DPIN:
    dpin_key(key);
    return;
  case PG_SET:
    if (key == KEY_8 || key == KEY_UP) { do sel_field = (sel_field + F_COUNT - 1) % F_COUNT; while (N->cfg.live && (sel_field == F_DATE || sel_field == F_TIME)); }
    else if (key == KEY_2 || key == KEY_DOWN) { do sel_field = (sel_field + 1) % F_COUNT; while (N->cfg.live && (sel_field == F_DATE || sel_field == F_TIME)); }
    else if (key == KEY_ENTER) {
      if (sel_field == F_LT) { N->cfg.lt ^= 1; N->valid = 0; cfg_save(); }
      else if (sel_field == F_LIVE) {
        if (N->cfg.live) N->cfg.fixed_jd = ut_now();
        N->cfg.live ^= 1;
        toff = 0;
        N->valid = 0;
        cfg_save();
      } else { edit_field = sel_field; elen = 0; ebuf[0] = 0; }
    }
    break;
  }
  if (key == KEY_EXIT) go(page == PG_FILES ? PG_SET : PG_VALM);
}

static void *nav_alloc(size_t n) { return calloc(1, n); }
void _exit(int code) { (void)code; sys_reset(); for (;;) {} }

void program_main(void) {
  geo_set_ellipsoid(ELL_WGS84);   /* geo47 */
  run_menu_item_app = NULL;
  menu_line_str_app = NULL;
  switch_usb_powered_freq();
  N = (Nav *)nav_alloc(sizeof(Nav));
  if (!N || !c47_init()) { lcd_clear_buf(); draw_text(&TINY, 10, 100, "NAV47: not enough memory", 0); lcd_refresh(); for (;;) wait_for_key_press(); }
  nt_init();
  cfg_load();
  route_load();
  if (N->cfg.zone < 0 || N->cfg.zone >= NZONES) N->cfg.zone = zone_nearest(N->cfg.tz, ut_now());
  int nf = N->cfg.nfiles;
  N->cfg.nfiles = 0;
  for (int i = 0; i < nf; i++) {
    char p[40];
    strcpy(p, N->cfg.files[i]);
    if (load_file(p)) { strcpy(N->cfg.files[N->cfg.nfiles], p); N->cfg.nfiles++; }
  }
  redraw();
  uint32_t last = sys_current_ms();
  for (;;) {
    if (usbview_is_on() && !ST(STAT_PGM_END) && key_empty())
      usbser_idle(page == PG_VANIM || (page == PG_VOY && voy_play) ? 250 : 1000);       /* the USB runs: no sleep (it would stop the USB) */
    else if ((ST(STAT_PGM_END) && ST(STAT_SUSPENDED)) || (!ST(STAT_PGM_END) && key_empty())) {
      sys_timer_start(0, page == PG_VANIM || (page == PG_VOY && voy_play) ? 250 : 1000);
      CLR_ST(STAT_RUNNING);
      sys_sleep();
    }
    if (ST(STAT_PGM_END) || ST(STAT_SUSPENDED)) {
      if (!ST(STAT_SUSPENDED)) {
        cfg_save();
        usbview_off();
        lcd_set_buf_cleared(0);
        draw_power_off_image(1);
        LCD_power_off(0);
        SET_ST(STAT_SUSPENDED);
        SET_ST(STAT_OFF);
      }
      continue;
    }
    SET_ST(STAT_RUNNING);
    CLR_ST(STAT_SUSPENDED);
    if (ST(STAT_OFF)) {
      LCD_power_on();
      rtc_wakeup_delay();
      CLR_ST(STAT_OFF);
      redraw();
    }
    if (ST(STAT_POWER_CHANGE)) { switch_usb_powered_freq(); CLR_ST(STAT_POWER_CHANGE); }
    /* SKY: the name loop, a name a second. ANIM: a frame every half second, the last held 3 s, then again.
       Live pages: the zone clock every second, the others every 30 s (a new time). */
    uint32_t t = sys_current_ms();
    if (key_empty()) {
      if (page == PG_VSKY && t - last >= 1000) { last = t; vframe++; redraw(); }
      else if (page == PG_VOY && voy_play && t - last >= 250) { last = t; voy_tick(); redraw(); }
      else if (page == PG_VANIM && t - last >= (vframe >= ANIM_FRAMES - 1 ? 3000u : 500u)) {
        last = t;
        vframe = vframe >= ANIM_FRAMES - 1 ? 0 : vframe + 1;
        redraw();
      } else if (N->cfg.live && ((page == PG_ZONES && t - last >= 1000) || t - last >= 30000)) {
        last = t;
        if (page != PG_VANIM && page != PG_CHART && page != PG_VOY) redraw();
      }
    }
    if (!key_empty()) reset_auto_off();
    int key = key_pop();
    if (key == USBVIEW_KEY_REDRAW) {          /* DMCP's serial console: the PC sent L, it wants the whole screen */
      redraw();
      usbview_redraw_request();
    } else if (key > 0) {
      handle_key(key);
      redraw();
      last = sys_current_ms();
    }
  }
}
