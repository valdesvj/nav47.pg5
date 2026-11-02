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

#define DIR       "/NAV47"
#define TIDEDIR   "/NAV47/TIDES"
#define CFGFILE   "/NAV47/NAV47.CFG"
#define MENU_Y    (LCD_Y - LCD_MENU_LINES)     /* 208 */
#define LH        9                            /* tiny font line pitch */

/* ------------------------------------------------------------------ */
/*  State (heap: a DMCP5 program has 8 KB of static RAM)              */
/* ------------------------------------------------------------------ */
enum { PG_MENU, PG_VALM, PG_VCHART, PG_VSKY, PG_VSPLIT, PG_VANIM, PG_VALLSKY,
       PG_ZONES, PG_TIDES, PG_FLOW, PG_SET, PG_FILES, PG_INFO, PG_PICK };
#define IS_VIEW(p) ((p) <= PG_VALLSKY)

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
} Cfg;

typedef struct { int src, reg, st; } Row;
#define MAXROWS 400

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
} Nav;

static Nav *N;
static int page = PG_MENU, menu_page, shift, vframe;
static double toff;                      /* hours added to the time by the arrows */
static int edit_field = -1, sel_field, pick_cursor, tide_day_off, flow_hw_step, flow_day_step;
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
  draw_text(&STD, 2, 0, title, 1);
  if (right) draw_text_r(&STD, LCD_X - 2, 0, right, 0);
}

static const char *MON[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
static const char *WDAY[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };


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
  draw_text(&STD, 2, 0, s, 1);
  fmt_hm(t, h);
  draw_text(&STD, 150, 0, t, 0);
  sprintf(s, "%s %d %s", WDAY[(n + 1) % 7], d, MON[m - 1]);
  draw_text_r(&STD, LCD_X - 2, 0, s, 0);
  fmt_off(o, N->cfg.tz);
  sprintf(s, "TZ %s", o);
  draw_text(&STD, 2, 19, s, 0);
  fmt_hm(t, fmod((ut + 0.5) * 24.0, 24.0) + N->cfg.tz);
  draw_text(&STD, 150, 19, t, 0);
  double dd = off - N->cfg.tz;
  if (dd == 0) strcpy(s, "same time"); else { fmt_off(o, dd); sprintf(s, "%s h", o); }
  draw_text_r(&STD, LCD_X - 2, 19, s, 0);
  /* map: 120 x 45 dots, 3 px each, from x 20; zone column c (5 dots, 15 deg) is UTC c - 11 */
  int mx = 20, my = 40;
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
  draw_text(&STD, 2, MENU_Y - 20, s, 0);
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
    tide_day_off = 0;
  }
  page = PG_TIDES;
}

static void pick_open(void) {
  Row *rows = N->rows;
  int n = list_rows(rows, MAXROWS);
  for (int i = 0; i < n; i++)
    if (rows[i].src == N->cfg.tsrc && rows[i].reg == N->cfg.treg && rows[i].st == N->cfg.tst) pick_cursor = i;
  page = PG_PICK;
}

/* ---- TIDES: the day of the station ---- */
static void draw_tides(void) {
  const TStation *st = cur_station();
  int stream = st->kind != TK_TIDE;
  double ut = ut_now();
  /* the station's local date now */
  int y, m, d;
  double h;
  long n0;
  {
    double tz0 = st->tz;
    jd_to_ymd(ut + tz0 / 24.0, &y, &m, &d, &h);
    n0 = jdn(y, m, d);
    if (tide_dst(st, n0)) { jd_to_ymd(ut + (tz0 + 1) / 24.0, &y, &m, &d, &h); n0 = jdn(y, m, d); }
  }
  long n = n0 + tide_day_off;
  TDay *Dp = &N->tday;
  tide_day(st, n, stream, Dp);
#define D (*Dp)
  cal(n, &y, &m, &d);
  char s[96], o[12], t[8];
  draw_text(&STD, 2, 0, st->name, 1);
  fmt_off(o, D.tz);
  sprintf(s, "%s %04d-%02d-%02d  UTC%s  %s%s%s", WDAY[(n + 1) % 7], y, m, d, o, stream ? "knots" : "metres above ",
          stream ? "" : st->datum, tide_day_off ? "" : "  (today)");
  draw_text(&TINY, 2, 22, s, 0);
  /* graph */
  int gx = 24, gy = 34, gw = 216, gh = 120;
  double lo = 1e9, hi = -1e9;
  for (int k = 0; k < 73; k++) { if (D.curve[k] < lo) lo = D.curve[k]; if (D.curve[k] > hi) hi = D.curve[k]; }
  if (stream && st->kind == TK_RECT) { double a = fmax(fabs(lo), fabs(hi)); lo = -a; hi = a; }
  if (stream && st->kind == TK_ROT) lo = 0;
  if (hi - lo < 0.2) { hi += 0.1; lo -= 0.1; }
  double pad = (hi - lo) * 0.08;
  lo -= pad; hi += pad;
  gfx_frame(gx, gy, gw + 1, gh + 1);
  for (int k = 0; k <= 24; k += 3) {
    int x = gx + k * gw / 24;
    gfx_rect(x, gy + gh, 1, 3, 1);
    if (k % 6 == 0) { sprintf(s, "%02d", k); draw_text(&TINY, x - 5, gy + gh + 4, s, 0); }
  }
  sprintf(s, "%.1f", hi); draw_text_r(&TINY, gx - 2, gy, s, 0);
  sprintf(s, "%.1f", lo); draw_text_r(&TINY, gx - 2, gy + gh - 8, s, 0);
  if (lo < 0 && hi > 0) gfx_hline_dotted(gx, gx + gw, gy + (int)((hi - 0) / (hi - lo) * gh), 3);
  int px = -1, py = -1;
  for (int k = 0; k < 73; k++) {
    int x = gx + k * gw / 72, yy = gy + (int)((hi - D.curve[k]) / (hi - lo) * gh);
    if (px >= 0) { gfx_line(px, py, x, yy, 1); gfx_line(px, py + 1, x, yy + 1, 1); }
    px = x; py = yy;
  }
  /* now */
  double now_h = -1;
  if (tide_day_off == 0) {
    now_h = fmod((ut + 0.5) * 24.0 + D.tz + 48.0, 24.0);
    int x = gx + (int)(now_h / 24.0 * gw);
    for (int yy = gy; yy < gy + gh; yy += 2) gfx_pixel(x, yy, 1);
  }
  /* events */
  int ex = 250, ey = 36;
  for (int i = 0; i < D.nev; i++) {
    const TEvent *e = &D.ev[i];
    fmt_hm(t, e->t);
    if (!stream) sprintf(s, "%-5s %s %6.2f m", e->kind > 0 ? "HIGH" : "LOW", t, e->val);
    else if (st->kind == TK_RECT) {
      if (e->kind < 0) sprintf(s, "SLACK %s", t);
      else sprintf(s, "%-5s %s %4.1f kn %03.0f", e->val > 0 ? "FLOOD" : "EBB", t, fabs(e->val), e->set);
    } else sprintf(s, "%-5s %s %4.1f kn %03.0f", e->kind > 0 ? "MAX" : "MIN", t, e->val, e->set);
    draw_text(&TINY, ex, ey, s, 0);
    ey += LH + 2;
  }
  if (now_h >= 0) {
    double v, dir;
    stream_now(st, n, now_h, &v, &dir);
    fmt_hm(t, now_h);
    if (!stream) sprintf(s, "NOW %s %.2f m", t, v);
    else sprintf(s, "NOW %s %.1f kn %03.0f", t, fabs(v), dir);
    draw_text(&TINY, ex, ey + 4, s, 1);
  }
  sprintf(s, "%s  %s", st->src && *st->src ? st->src : "", N->cfg.tsrc == 1 ? "(private)" : N->cfg.tsrc == 2 ? "(file)" : "");
  draw_text(&TINY, 2, MENU_Y - 2 * LH, s, 0);
  draw_text(&TINY, 2, MENU_Y - LH, "4 6 day   5 today   ENTER stations      check official tables", 0);
#undef D
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

static void draw_flow(void) {
  char s[96], a[24], b[24], t[8];
  header("FLOW", NULL);
  const FlowAtlas *fl = atlas();
  if (!fl) {
    draw_text(&TINY, 2, 30, "No tidal stream atlas.", 0);
    draw_text(&TINY, 2, 30 + LH, "Build with make TIDE47=~/opt/tide47 (python/flow_private.py),", 0);
    draw_text(&TINY, 2, 30 + 2 * LH, "or copy FLOW.FLW (tools/gen_tides.py files) to /NAV47/TIDES and load it in FILES.", 0);
    return;
  }
  int rs, rr;
  const TStation *ref = find_station(fl->ref, &rs, &rr);
  fmt_lat(a, N->cfg.lat);
  fmt_lon(b, N->cfg.lon);
  sprintf(s, "%s %s  wind %03.0f/%.0f kn", a, b, N->cfg.wdir, N->cfg.wkn);
  draw_text_r(&TINY, LCD_X - 2, 6, s, 0);
  double FU[13], FV[13];
  if (!ref) { sprintf(s, "reference station \"%s\" not loaded", fl->ref); draw_text(&TINY, 2, 30, s, 0); return; }
  if (!flow_cell(fl, N->cfg.lat, N->cfg.lon, FU, FV)) {
    draw_text(&TINY, 2, 30, "No atlas here (outside the charts or on land). Set the position in SET.", 0);
    return;
  }
  double ut = ut_now();
  int y, m, d;
  double h;
  double tz = ref->tz;
  jd_to_ymd(ut + tz / 24.0, &y, &m, &d, &h);
  long n = jdn(y, m, d) + flow_day_step;
  long on;
  int om;
  flow_pick(ref, n, flow_day_step ? 12.0 : h, &on, &om);
  for (int k = 0; k < flow_hw_step; k++) flow_step(ref, on, om, 1, &on, &om);
  for (int k = 0; k > flow_hw_step; k--) flow_step(ref, on, om, -1, &on, &om);
  double t0, hh, rng, fac;
  flow_cycle(ref, on, om, &t0, &hh, &rng, &fac);
  double U[13], V[13];
  flow_rows(FU, FV, fac, N->cfg.wdir, N->cfg.wkn, U, V);
  int cy, cm, cd;
  cal(on, &cy, &cm, &cd);
  fmt_hm(t, t0);
  sprintf(s, "HW %04d-%02d-%02d %s %.2f m  x%.2f springs  (%.24s)", cy, cm, cd, t, hh, fac, ref->name);
  draw_text(&TINY, 2, 22, s, 0);
  /* the hour of now relative to that HW */
  double off_now = ((double)(jdn(y, m, d) - on) * 24.0 + h) - t0;
  int yy = 34;
  draw_text(&TINY, 2, yy, "     HOUR  CLOCK      SET     KN", 0);
  gfx_rect(2, yy + 8, 214, 1, 1);
  yy += 11;
  for (int k = 0; k < 13; k++) {
    double set, kn;
    setkn(U[k], V[k], &set, &kn);
    fmt_hm(t, t0 + (k - 6) - tz + N->cfg.tz);
    int mark = fabs(off_now - (k - 6)) <= 0.5;
    sprintf(s, "%c HW%+d   %s      %03.0f   %4.2f", mark ? '>' : ' ', k - 6, t, set, kn);
    if (k == 6) sprintf(s, "%c HW     %s      %03.0f   %4.2f", mark ? '>' : ' ', t, set, kn);
    draw_text(&TINY, 2, yy, s, 0);
    arrow(204, yy + 3, set, 3 + 5 * fmin(kn, 2.0));
    yy += LH + 2;
  }
  /* rose */
  int cx = 318, cyy = 122, R = 62;
  double mx = 0.25;
  for (int k = 0; k < 13; k++) { double se, kn; setkn(U[k], V[k], &se, &kn); if (kn > mx) mx = kn; }
  double ring = mx <= 0.5 ? 0.25 : mx <= 1.0 ? 0.5 : 1.0;
  double scale = R / (ceil(mx / ring) * ring);
  for (double r = ring; r * scale <= R + 1; r += ring) gfx_circle(cx, cyy, (int)(r * scale), 1);
  int px = 0, py = 0;
  for (int k = 0; k < 13; k++) {
    int x = cx + (int)lround(U[k] * scale), yv = cyy - (int)lround(V[k] * scale);
    if (k) gfx_line(px, py, x, yv, 1);
    if (k == 0 || k == 6 || k == 12) gfx_disc(x, yv, 2, 1);
    px = x; py = yv;
  }
  if (off_now >= -6 && off_now <= 6) {
    double u, v, se, kn;
    flow_now(U, V, off_now, &u, &v);
    setkn(u, v, &se, &kn);
    arrow(cx, cyy, se, kn * scale);
    arrow(cx + 1, cyy, se, kn * scale);
    sprintf(s, "NOW %03.0f %.2f kn", se, kn);
    draw_text(&TINY, 236, 44, s, 1);
  }
  sprintf(s, "rings %.2f kn", ring);
  draw_text(&TINY, cx - text_width(&TINY, s) / 2, cyy + R + 3, s, 0);
  /* ARTF: the tide at the nearest port of the reference station's region */
  const TRegion *reg = src_reg(rs, rr);
  int best = 0;
  double bd = 1e9;
  for (int i = 0; i < reg->n; i++) {
    const TStation *p = &reg->st[i];
    if (p->kind != TK_TIDE) continue;
    double dd = 60 * hypot(p->lat - N->cfg.lat, (p->lon - N->cfg.lon) * dcos(N->cfg.lat));
    if (dd < bd) { bd = dd; best = i; }
  }
  sprintf(s, "near %.14s %.0f nm", reg->st[best].name, bd);
  draw_text(&TINY, 236, 34, s, 0);
  draw_text(&TINY, 2, MENU_Y - LH, "4 6 HW  1 3 day  5 now    spring atlas x range, + 2.5% of the wind", 0);
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
  int y = 26;
  for (int f = 0; f < F_COUNT; f++) {
    if ((f == F_DATE || f == F_TIME) && N->cfg.live) continue;
    field_text(f, v);
    if (edit_field == f) sprintf(v, "%s_", ebuf);
    draw_text(&STD, 8, y, FNAME[f], f == sel_field);
    draw_text(&STD, 150, y, v, 0);
    draw_text_r(&TINY, LCD_X - 2, y + 7, FHELP[f], 0);
    y += 21;
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
  o += sprintf(o, "NAV47 CFG 1\nLAT %.9f\nLON %.9f\nTZ %.4f\nLIVE %d\nFIXED %.9f\nWIND %.1f %.1f\nTIDE %d %d %d\nZONE %d %d\n",
               c->lat, c->lon, c->tz, c->live, c->fixed_jd, c->wdir, c->wkn, c->tsrc, c->treg, c->tst, c->zone, c->city);
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
}

/* ------------------------------------------------------------------ */
/*  Menus, keys, main loop                                            */
/* ------------------------------------------------------------------ */
static const char *const MENU_ITEMS[10] = {
  "1 ALMANAC", "2 SPLIT", "3 SKY", "4 ANIM", "5 ALLSKY",
  "6 CHART", "7 ZONES", "8 TIDES", "9 FLOW", "0 SET",
};
static const int MENU_PAGE[10] = { PG_SET, PG_VALM, PG_VSPLIT, PG_VSKY, PG_VANIM, PG_VALLSKY,
                                   PG_VCHART, PG_ZONES, PG_TIDES, PG_FLOW };
static const char *MENUS[2][6] = {
  { "MENU", "ZONES", "TIDES", "FLOW", "STATNS", ">>" },
  { "SET", "FILES", "INFO", "NOW", "ANIM", ">>" },
};
static const char *FILE_MENU[6] = { "TID", "FLW", "CLEAR", "", "", "BACK" };
static const char *EDIT_MENU[6] = { "", "", "", "", "", "" };

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
    case PG_MENU: view_menu(&N->al, MENU_ITEMS, 10); break;
    case PG_VALM: view_almanac(&N->al); break;
    case PG_VCHART: view_chart(&N->al); break;
    case PG_VSKY: view_sky(&N->al, vframe); break;
    case PG_VSPLIT: view_split(&N->al); break;
    case PG_VANIM: view_anim(&N->al, ANIM_FRAMES, ANIM_STEP, vframe < ANIM_FRAMES ? vframe : ANIM_FRAMES - 1); break;
    case PG_VALLSKY: view_allsky(&N->al); break;
    }
    c47_show();
    lcd_refresh();
    return;
  }
  switch (page) {
  case PG_ZONES: draw_zones(); break;
  case PG_TIDES: draw_tides(); break;
  case PG_FLOW: draw_flow(); break;
  case PG_SET: draw_set(); break;
  case PG_FILES: draw_files(); break;
  case PG_INFO: draw_info(); break;
  case PG_PICK: draw_pick(); break;
  }
  lcd_draw_menu_keys(page == PG_FILES ? FILE_MENU : edit_field >= 0 ? EDIT_MENU : MENUS[menu_page]);
  lcd_refresh();
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
    } else if (fn == 5) page = PG_TIDES;
    return;
  }
  switch (menu_page * 6 + fn) {
  case 0: page = PG_MENU; break;
  case 1: page = PG_ZONES; break;
  case 2: page = PG_TIDES; break;
  case 3: page = PG_FLOW; flow_hw_step = flow_day_step = 0; break;
  case 4: pick_open(); break;
  case 6: page = PG_SET; sel_field = 0; break;
  case 7: page = PG_FILES; N->msg[0] = 0; break;
  case 8: page = PG_INFO; break;
  case 9: toff = 0; tide_day_off = 0; flow_hw_step = flow_day_step = 0; N->valid = 0; break;
  case 10: page = PG_VANIM; vframe = 0; break;
  case 5: case 11: menu_page ^= 1; break;
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
  if (edit_field >= 0) { key_edit(key); return; }
  if (key >= KEY_F1 && key <= KEY_F6) { softkey(key - KEY_F1); vframe = 0; return; }
  if (page == PG_MENU) {                         /* as Almanac 47's NAV menu: key a number */
    int d = digit(key);
    if (d >= 0) { page = MENU_PAGE[d]; vframe = 0; if (page == PG_SET) sel_field = 0; return; }
    if (key == KEY_UP) { step_time(1); return; }
    if (key == KEY_DOWN) { step_time(-1); return; }
    return;
  }
  switch (page) {
  case PG_VALM: case PG_VCHART: case PG_VSKY: case PG_VSPLIT: case PG_VANIM: case PG_VALLSKY:
    /* as Almanac 47 (WPLS): + back to the menu, up / down one hour later / earlier */
    if (key == KEY_ADD || key == KEY_EXIT) { page = PG_MENU; return; }
    if (key == KEY_UP) step_time(1);
    else if (key == KEY_DOWN) step_time(-1);
    else if (key == KEY_4) step_time(-1.0 / 6);
    else if (key == KEY_6) step_time(1.0 / 6);
    else if (key == KEY_7) step_time(-24);
    else if (key == KEY_9) step_time(24);
    else if (key == KEY_5) toff = 0;
    vframe = 0;
    return;
  case PG_ZONES:
    if (key == KEY_4) { N->cfg.zone = (N->cfg.zone + NZONES - 1) % NZONES; N->cfg.city = 0; }
    else if (key == KEY_6) { N->cfg.zone = (N->cfg.zone + 1) % NZONES; N->cfg.city = 0; }
    else if (key == KEY_8 || key == KEY_UP) N->cfg.city = (N->cfg.city + ZONES[N->cfg.zone].n - 1) % ZONES[N->cfg.zone].n;
    else if (key == KEY_2 || key == KEY_DOWN) N->cfg.city = (N->cfg.city + 1) % ZONES[N->cfg.zone].n;
    else if (key == KEY_5) { N->cfg.zone = zone_nearest(N->cfg.tz, ut_now()); N->cfg.city = 0; }
    break;
  case PG_TIDES:
    if (key == KEY_4) tide_day_off--;
    else if (key == KEY_6) tide_day_off++;
    else if (key == KEY_5) tide_day_off = 0;
    else if (key == KEY_ENTER) pick_open();
    break;
  case PG_PICK:
    if (key == KEY_8 || key == KEY_UP) pick_move(-1);
    else if (key == KEY_2 || key == KEY_DOWN) pick_move(1);
    else if (key == KEY_ENTER || key == KEY_5) { pick_choose(); cfg_save(); }
    else if (key == KEY_EXIT) page = PG_TIDES;
    break;
  case PG_FLOW:
    if (key == KEY_4) flow_hw_step--;
    else if (key == KEY_6) flow_hw_step++;
    else if (key == KEY_1) { flow_day_step--; flow_hw_step = 0; }
    else if (key == KEY_3) { flow_day_step++; flow_hw_step = 0; }
    else if (key == KEY_5) flow_hw_step = flow_day_step = 0;
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
  if ((key == KEY_EXIT || key == KEY_ADD) && page != PG_PICK) page = PG_MENU;
}

static void *nav_alloc(size_t n) { return calloc(1, n); }
void _exit(int code) { (void)code; sys_reset(); for (;;) {} }

void program_main(void) {
  run_menu_item_app = NULL;
  menu_line_str_app = NULL;
  switch_usb_powered_freq();
  N = (Nav *)nav_alloc(sizeof(Nav));
  if (!N || !c47_init()) { lcd_clear_buf(); draw_text(&STD, 10, 100, "NAV47: not enough memory", 0); lcd_refresh(); for (;;) wait_for_key_press(); }
  cfg_load();
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
