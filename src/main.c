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

#define DIR       "/NAV47"
#define TIDEDIR   "/NAV47/TIDES"
#define CFGFILE   "/NAV47/NAV47.CFG"
#define ROUTEFILE "/NAV47/ROUTE.TXT"
#define MENU_Y    (LCD_Y - LCD_MENU_LINES)     /* 208 */
#define LH        9                            /* tiny font line pitch */

/* ------------------------------------------------------------------ */
/*  State (heap: a DMCP5 program has 8 KB of static RAM)              */
/* ------------------------------------------------------------------ */
enum { PG_VALM, PG_VSPLIT, PG_VSKY, PG_VALLSKY, PG_VMOON, PG_VANIM,
       PG_TIDE, PG_ROUTE, PG_ZONES, PG_SET, PG_FILES, PG_INFO, PG_PICK };
#define IS_VIEW(p) ((p) <= PG_VANIM)

typedef struct {
  double lat, lon;          /* degrees, N+ E+ */
  double tz;                /* the calculator clock's offset from UT, hours */
  int    live;              /* 1: time from the clock, 0: the fixed UT below */
  double fixed_jd;          /* UT */
  double wdir, wkn;         /* wind (from), knots, for FLOW */
  int    tsrc, treg, tst;   /* tide station: source (0 public, 1 private, 2 file), region, station */
  int    zone, city;        /* ZONES stop and city */
  char   files[4][40];      /* loaded tide files, reloaded at start */
  int    nfiles;
  int    south;             /* MOON47: the view from the south */
  int    gc;                /* ROUTE: great circle instead of rhumb line */
} Cfg;

typedef struct { int src, reg, st; } Row;
#define MAXROWS 400
#define MAXWP   40

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
} Nav;

static Nav *N;
static int page = PG_VALM, menu_page, shift, vframe;
static double toff;                      /* hours added to the time by the arrows */
static int edit_field = -1, sel_field, pick_cursor, flow_hw_step, flow_day_step;
static int wp_cur, wp_edit = -1;          /* ROUTE: the cursor; editing 0 lat, 1 lon */
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

static void header(const char *title, const char *right) {
  nt_inv(2, 0, title);
  if (right) nt_r(LCD_X - 2, 0, right);
  lcd_fill_rect(0, 15, LCD_X, 1, 1);
}

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
  fmt_off(o, off);
  sprintf(s, "UTC%s", off == 0 ? "" : o);
  nt_inv(2, 0, s);
  fmt_hm(t, h);
  nt(150, 0, t);
  sprintf(s, "%s %d %s", WDAY[(n + 1) % 7], d, MON[m - 1]);
  nt_r(LCD_X - 2, 0, s);
  fmt_off(o, N->cfg.tz);
  sprintf(s, "TZ %s", o);
  nt(2, 17, s);
  fmt_hm(t, fmod((ut + 0.5) * 24.0, 24.0) + N->cfg.tz);
  nt(150, 17, t);
  double dd = off - N->cfg.tz;
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

static const TStation *cur_station(void) {
  Cfg *c = &N->cfg;
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
  header("STATIONS", "8 2 move  ENTER choose");
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
    flow_hw_step = flow_day_step = 0;
  }
  page = PG_TIDE;
}

static void pick_open(void) {
  Row *rows = N->rows;
  int n = list_rows(rows, MAXROWS);
  for (int i = 0; i < n; i++)
    if (rows[i].src == N->cfg.tsrc && rows[i].reg == N->cfg.treg && rows[i].st == N->cfg.tst) pick_cursor = i;
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

/* TIDE: Tide 47's ARTF47 page. Left: the hours HW-6 ... HW+6 of the reference station's High Water, with the
   stream of the FLOW atlas at the position (springs scaled, the wind added), or the station's own height or stream.
   Right: the tide of the port nearest the position (the chosen station without an atlas): the day's curve, the
   table's 13 hours as a bar, now dotted, the high and low waters. */
static void draw_tide(void) {
  char s[96], t[8], a[24], b[24];
  double ut = ut_now();
  const FlowAtlas *fl = atlas();
  int rs = 0, rr = 0;
  const TStation *ref = fl ? find_station(fl->ref, &rs, &rr) : NULL;
  double FU[13], FV[13];
  int have_flow = ref && flow_cell(fl, N->cfg.lat, N->cfg.lon, FU, FV);
  const TStation *st = ref ? ref : cur_station();
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
  /* the top line */
  fmt_dmn(a, N->cfg.lat, 2, N->cfg.lat < 0 ? 'S' : 'N');
  fmt_dmn(b, N->cfg.lon, 3, N->cfg.lon < 0 ? 'W' : 'E');
  sprintf(s, "%s %s", a, b);
  nt(1, 0, s);
  {
    int y, m, d;
    cal(on, &y, &m, &d);
    sprintf(s, "%s %02d-%02d-%04d", WDAY[(on + 1) % 7], d, m, y);
    nt_r(399, 0, s);
  }
  lcd_fill_rect(0, 15, LCD_X, 1, 1);
  for (int y = 17; y < MENU_Y; y += 2) lcd_fill_rect(200, (uint32_t)y, 1, 1, 1);
  /* left: the table */
  int mark = off_now >= -6.5 && off_now < 6.5 ? (int)floor(off_now + 6.5) : -1;
  int first = mark < 0 ? 1 : mark - 5;
  if (first < 0) first = 0;
  if (first > 2) first = 2;
  nt(tidal ? 12 : 4, 16, tidal ? "HW" : "  ");
  nt(46, 16, "TIME");
  if (vec) { nt(114, 16, "SET"); nt_r(196, 16, "KN"); } else nt_r(196, 16, "HEIGHT");
  double dclk = N->cfg.tz - tzl;
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
    fmt_hm(t, h0 + N->cfg.tz - tzl);
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
  for (int k = 0; k <= 24; k += 6) {
    gfx_rect(gx + k * gw / 24, ay - 1, 1, 3, 1);
    sprintf(s, "%02d", k);
    draw_text(&TINY, gx + k * gw / 24 - 5, ay + 3, s, 0);
  }
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
    fmt_hm(t, e->t + N->cfg.tz - rtz);
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
/*  SET                                                               */
/* ------------------------------------------------------------------ */
enum { F_LAT, F_LON, F_TZ, F_LIVE, F_DATE, F_TIME, F_WDIR, F_WKN, F_COUNT };
static const char *FNAME[F_COUNT] = { "LATITUDE", "LONGITUDE", "CLOCK TZ", "TIME", "DATE (UT)", "TIME (UT)", "WIND FROM", "WIND KN" };
static const char *FHELP[F_COUNT] = { "DD.MMm, +/- south", "DDD.MMm, +/- west", "clock - UT, hours", "ENTER switches",
                                      "YYYY.MMDD", "HH.MM", "degrees true", "knots" };

static void field_text(int f, char *s) {
  Cfg *c = &N->cfg;
  int y, m, d;
  double h;
  jd_to_ymd(c->fixed_jd + 0.5 / 1440, &y, &m, &d, &h);
  switch (f) {
  case F_LAT: fmt_lat(s, c->lat); break;
  case F_LON: fmt_lon(s, c->lon); break;
  case F_TZ: fmt_off(s, c->tz); break;
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
  header("SET", "8 2 field  ENTER edit");
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
    y += 20;
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
  header("TIDE FILES", NULL);
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
  o += sprintf(o, "NAV47 CFG 1\nLAT %.9f\nLON %.9f\nTZ %.4f\nLIVE %d\nFIXED %.9f\nWIND %.1f %.1f\nTIDE %d %d %d\nZONE %d %d\nSOUTH %d\nGC %d\n",
               c->lat, c->lon, c->tz, c->live, c->fixed_jd, c->wdir, c->wkn, c->tsrc, c->treg, c->tst, c->zone, c->city,
               c->south, c->gc);
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
  c->wdir = 0; c->wkn = 0; c->zone = -1;
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
    else if (!strncmp(l, "ZONE ", 5)) sscanf(l + 5, "%d %d", &c->zone, &c->city);
    else if (!strncmp(l, "SOUTH ", 6)) c->south = atoi(l + 6);
    else if (!strncmp(l, "GC ", 3)) c->gc = atoi(l + 3);
    else if (!strncmp(l, "FILE ", 5) && c->nfiles < 4) { strncpy(c->files[c->nfiles], l + 5, 39); c->files[c->nfiles++][39] = 0; }
    l = strtok(NULL, "\n");
  }
  free(t);
}

/* ------------------------------------------------------------------ */
/*  INFO                                                              */
/* ------------------------------------------------------------------ */
static void draw_info(void) {
  header("NAV47  v" PROGRAM_VERSION, NULL);
  const char *L[] = {
    "Almanac 47 + Zones 47 + Tide 47 for the SwissMicros DM42n (native DMCP5 program)",
    "(C) 2026 Victor Valdes - GPL-3.0-or-later. Written with the help of AI (Claude).",
    "",
    "ALMANAC: VSOP87D Sun and planets, IAU1980 nutation, Meeus Moon + DE421 fit, 58 stars",
    "         (Almanac 47 NAVINIT_FULL series, double precision, no tables needed)",
    "ZONES:   IANA tz database, DST computed on the calculator. Map: GLOBE (NOAA)",
    "TIDES:   Schureman harmonic method (Tide 47). NOAA (public domain), TICON-3 (CC-BY 4.0)",
    "FONTS:   C47 standardFont and tinyFont (C43/C47 authors, GPL v3)",
    "",
    "DOES NOT REPLACE THE NAUTICAL ALMANAC OR OFFICIAL TIDE TABLES.",
    "Not for use as the primary means of navigation.",
  };
  int y = 26;
  for (unsigned i = 0; i < sizeof L / sizeof *L; i++, y += LH) draw_text(&TINY, 2, y, L[i], 0);
  char s[96];
  sprintf(s, "tzdata %s   free heap %d KB   stations: %d public regions, %d private, %d files", TZDATA_VERSION,
          sys_free_mem() / 1024, PUB_NREGIONS, PRIV_NREGIONS, N->nfr);
  draw_text(&TINY, 2, y + 4, s, 0);
  sprintf(s, "CPU %s", usb_powered() ? "160 MHz (USB)" : "80 MHz (battery)");
  draw_text(&TINY, 2, y + 4 + LH, s, 0);
  char u[64];
  usbview_state(s, u, 64);
  nt_inv(4, 176, s);
  nt(4, 192, u);
}

/* ------------------------------------------------------------------ */
/*  ROUTE: passage planning                                           */
/* ------------------------------------------------------------------ */
static void route_save(void) {
  if (!sys_disk_ok()) return;
  char *b = (char *)malloc(64 + 48 * MAXWP);
  if (!b) return;
  char *o = b;
  o += sprintf(o, "NAV47 ROUTE 1\n%s\n", N->cfg.gc ? "GC" : "RL");
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
    double a, b;
    if (!strcmp(l, "GC")) N->cfg.gc = 1;
    else if (!strcmp(l, "RL")) N->cfg.gc = 0;
    else if (sscanf(l, "%lf %lf", &a, &b) == 2 && N->nwp < MAXWP && fabs(a) <= 90 && fabs(b) <= 180) {
      N->wlat[N->nwp] = a; N->wlon[N->nwp] = b; N->nwp++;
    }
    l = strtok(NULL, "\n");
  }
  free(t);
}

static void route_insert(double lat, double lon) {
  if (N->nwp >= MAXWP) return;
  int at = N->nwp ? wp_cur + 1 : 0;
  memmove(&N->wlat[at + 1], &N->wlat[at], sizeof(double) * (size_t)(N->nwp - at));
  memmove(&N->wlon[at + 1], &N->wlon[at], sizeof(double) * (size_t)(N->nwp - at));
  N->wlat[at] = lat; N->wlon[at] = lon;
  N->nwp++;
  wp_cur = at;
}

static void route_delete(void) {
  if (!N->nwp) return;
  memmove(&N->wlat[wp_cur], &N->wlat[wp_cur + 1], sizeof(double) * (size_t)(N->nwp - wp_cur - 1));
  memmove(&N->wlon[wp_cur], &N->wlon[wp_cur + 1], sizeof(double) * (size_t)(N->nwp - wp_cur - 1));
  N->nwp--;
  if (wp_cur >= N->nwp && wp_cur) wp_cur--;
}

static void leg(int i, double *crs, double *nm) {
  (N->cfg.gc ? leg_gc : leg_rl)(N->wlat[i - 1], N->wlon[i - 1], N->wlat[i], N->wlon[i], crs, nm);
}

static void draw_route(void) {
  char s[64];
  double tot = 0, crs, nm;
  for (int i = 1; i < N->nwp; i++) { leg(i, &crs, &nm); tot += nm; }
  nt_inv(2, 0, N->cfg.gc ? "ROUTE GC" : "ROUTE RL");
  sprintf(s, "%d WP  TOTAL %.1f NM", N->nwp, tot);
  nt_r(LCD_X - 2, 0, s);
  lcd_fill_rect(0, 15, LCD_X, 1, 1);
  int xw = 6 + nt_w("40"), xla = xw + 10 + nt_w("00" NT_DEG "00.0N"), xlo = xla + 12 + nt_w("000" NT_DEG "00.0E"),
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
    nt(4, 160, "RL: rhumb line, GC: great");
    nt(4, 176, "circle (initial course)");
    return;
  }
  int vis = 10;
  if (wp_edit >= 0) nt_inv(2, 192, wp_edit ? "LON DDD.MMm  +/- WEST  ENTER" : "LAT DD.MMm  +/- SOUTH  ENTER");
  else nt(2, 192, "UP DN select   ENTER edit");
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
      wp_edit = -1;
      route_save();
    }
    return;
  }
  if (key == KEY_EXIT) { wp_edit = -1; return; }
  if (key == KEY_BSP) { if (elen) ebuf[--elen] = 0; return; }
  if (key == KEY_CHS) { if (ebuf[0] == '-') { memmove(ebuf, ebuf + 1, (size_t)elen); elen--; } else if (elen < 14) { memmove(ebuf + 1, ebuf, (size_t)elen + 1); ebuf[0] = '-'; elen++; } return; }
  if (key == KEY_DOT && elen < 14 && !strchr(ebuf, '.')) { ebuf[elen++] = '.'; ebuf[elen] = 0; return; }
  if (d >= 0 && elen < 14) { ebuf[elen++] = (char)('0' + d); ebuf[elen] = 0; }
}

/* ------------------------------------------------------------------ */
/*  Menus, keys, main loop                                            */
/* ------------------------------------------------------------------ */
static const char *MENUS[4][6] = {
  { "ALMAN", "SPLIT", "SKY", "ALLSK", "MOON", ">>" },
  { "ANIM", "TIDE", "ROUTE", "ZONES", "STATNS", ">>" },
  { "SET", "FILES", "INFO", "NOW", "USB", ">>" },
  { "ADD", "EDIT", "DEL", "RL/GC", "HERE", ">>" },
};
static const char *FILE_MENU[6] = { "TID", "FLW", "CLEAR", "", "", "BACK" };
static const char *EDIT_MENU[6] = { "", "", "", "", "", "CANCEL" };

#define ANIM_FRAMES 24
#define ANIM_STEP   0.5

/* the views' almanac at the current time (computed again only when the time or the position changes) */
static void view_almanac_now(void) {
  double j = ut_now();
  if (!N->valid || N->vjd != j || N->al.lat != N->cfg.lat || N->al.lon != N->cfg.lon) {
    al_compute(&N->al, j, N->cfg.lat, N->cfg.lon);
    N->vjd = j;
    N->valid = 1;
  }
}

static void redraw(void) {
  lcd_clear_buf();
  if (IS_VIEW(page)) {
    view_almanac_now();
    switch (page) {
    case PG_VALM: view_almanac(&N->al); break;
    case PG_VSPLIT: view_split(&N->al); break;
    case PG_VSKY: view_sky(&N->al, vframe); break;
    case PG_VALLSKY: view_allsky(&N->al); break;
    case PG_VMOON: view_moon(&N->al, N->cfg.south); break;
    case PG_VANIM: view_anim(&N->al, ANIM_FRAMES, ANIM_STEP, vframe < ANIM_FRAMES ? vframe : ANIM_FRAMES - 1); break;
    }
  } else
    switch (page) {
    case PG_TIDE: draw_tide(); break;
    case PG_ROUTE: draw_route(); break;
    case PG_ZONES: draw_zones(); break;
    case PG_SET: draw_set(); break;
    case PG_FILES: draw_files(); break;
    case PG_INFO: draw_info(); break;
    case PG_PICK: draw_pick(); break;
    }
  if (page != PG_ROUTE && menu_page == 3) menu_page = 0;
  MENUS[2][4] = usbview_is_on() ? "USB*" : "USB";
  lcd_draw_menu_keys(page == PG_FILES ? FILE_MENU : (edit_field >= 0 || wp_edit >= 0) ? EDIT_MENU : MENUS[menu_page]);
  lcd_refresh();
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
  if (fn == 5) {                                       /* >> */
    menu_page = menu_page == 3 ? 0 : (page == PG_ROUTE && menu_page == 2) ? 3 : (menu_page + 1) % 3;
    return;
  }
  switch (menu_page * 6 + fn) {
  case 0: go(PG_VALM); break;
  case 1: go(PG_VSPLIT); break;
  case 2: go(PG_VSKY); break;
  case 3: go(PG_VALLSKY); break;
  case 4: go(PG_VMOON); break;
  case 6: go(PG_VANIM); break;
  case 7: go(PG_TIDE); flow_hw_step = flow_day_step = 0; break;
  case 8: go(PG_ROUTE); menu_page = 3; break;
  case 9: go(PG_ZONES); break;
  case 10: pick_open(); break;
  case 12: go(PG_SET); sel_field = 0; break;
  case 13: go(PG_FILES); N->msg[0] = 0; break;
  case 14: go(PG_INFO); break;
  case 15: toff = 0; flow_hw_step = flow_day_step = 0; N->valid = 0; break;
  case 16: if (usbview_is_on()) usbview_off(); else usbview_on(); go(PG_INFO); break;   /* INFO shows the state */
  case 18: route_insert(N->nwp ? N->wlat[wp_cur] : N->cfg.lat, N->nwp ? N->wlon[wp_cur] : N->cfg.lon); wp_start_edit(); break;
  case 19: wp_start_edit(); break;
  case 20: route_delete(); route_save(); break;
  case 21: N->cfg.gc ^= 1; route_save(); break;
  case 22: route_insert(N->cfg.lat, N->cfg.lon); route_save(); break;
  }
}

static void step_time(double hours) { toff += hours; }

static void handle_key(int key) {
  if (key == KEY_SHIFT) { shift = !shift; return; }
  if (shift) {
    shift = 0;
    if (key == KEY_EXIT) { cfg_save(); SET_ST(STAT_PGM_END); return; }
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
  if ((edit_field >= 0 || wp_edit >= 0) && key == KEY_F6) key = KEY_EXIT;
  if (edit_field >= 0) { key_edit(key); return; }
  if (wp_edit >= 0) { key_wp(key); return; }
  if (key >= KEY_F1 && key <= KEY_F6) { softkey(key - KEY_F1); vframe = 0; return; }
  if (IS_VIEW(page)) {
    /* as Almanac 47: up / down one hour later / earlier; 4 6 ten minutes, 7 9 a day, 5 now */
    if (key == KEY_UP) step_time(1);
    else if (key == KEY_DOWN) step_time(-1);
    else if (key == KEY_4) step_time(-1.0 / 6);
    else if (key == KEY_6) step_time(1.0 / 6);
    else if (key == KEY_7) step_time(-24);
    else if (key == KEY_9) step_time(24);
    else if (key == KEY_5) toff = 0;
    else if (key == KEY_CHS && page == PG_VMOON) { N->cfg.south ^= 1; cfg_save(); }
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
    break;
  case PG_PICK:
    if (key == KEY_8 || key == KEY_UP) pick_move(-1);
    else if (key == KEY_2 || key == KEY_DOWN) pick_move(1);
    else if (key == KEY_ENTER || key == KEY_5) { pick_choose(); cfg_save(); }
    else if (key == KEY_EXIT) go(PG_TIDE);
    return;
  case PG_ROUTE:
    if ((key == KEY_8 || key == KEY_UP) && wp_cur > 0) wp_cur--;
    else if ((key == KEY_2 || key == KEY_DOWN) && wp_cur < N->nwp - 1) wp_cur++;
    else if (key == KEY_ENTER) wp_start_edit();
    break;
  case PG_SET:
    if (key == KEY_8 || key == KEY_UP) { do sel_field = (sel_field + F_COUNT - 1) % F_COUNT; while (N->cfg.live && (sel_field == F_DATE || sel_field == F_TIME)); }
    else if (key == KEY_2 || key == KEY_DOWN) { do sel_field = (sel_field + 1) % F_COUNT; while (N->cfg.live && (sel_field == F_DATE || sel_field == F_TIME)); }
    else if (key == KEY_ENTER) {
      if (sel_field == F_LIVE) {
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
    if ((ST(STAT_PGM_END) && ST(STAT_SUSPENDED)) || (!ST(STAT_PGM_END) && key_empty())) {
      sys_timer_start(0, page == PG_VANIM ? 250 : 1000);
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
      else if (page == PG_VANIM && t - last >= (vframe >= ANIM_FRAMES - 1 ? 3000u : 500u)) {
        last = t;
        vframe = vframe >= ANIM_FRAMES - 1 ? 0 : vframe + 1;
        redraw();
      } else if (N->cfg.live && ((page == PG_ZONES && t - last >= 1000) || t - last >= 30000)) {
        last = t;
        if (page != PG_VANIM) redraw();
      }
    }
    if (!key_empty()) reset_auto_off();
    int key = key_pop();
    if (key > 0) {
      handle_key(key);
      redraw();
      last = sys_current_ms();
    }
  }
}
