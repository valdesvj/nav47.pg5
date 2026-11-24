/* tide_ui.c - NAV47: the tide pages: the station list (STATNS), T&F (Tide 47's FLOW47 / ARTF47) and TIDE (the day).
 * Split from main.c; the shared state and the calls between the files are in app.h.
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#include "app.h"

/* ------------------------------------------------------------------ */
/*  Tide stations: public, private (compiled), files                  */
/* ------------------------------------------------------------------ */
static int src_nreg(int src) { return src == 0 ? PUB_NREGIONS : src == 1 ? PRIV_NREGIONS : N->nfr; }
static const TRegion *src_reg(int src, int r) { return src == 0 ? &PUB_REGIONS[r] : src == 1 ? &PRIV_REGIONS[r] : &N->fr[r]; }

/* the station nearest (lat, lon) of a kind (want 0: a tide, 1: a stream), every source; its distance (NM) in *nm */
const TStation *nearest_station(double lat, double lon, int want, double *nm, int *src, int *reg, int *st) {
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

void draw_pick(void) {
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

void pick_move(int d) {
  Row *rows = N->rows;
  int n = list_rows(rows, MAXROWS), i = pick_cursor;
  do { i += d; } while (i >= 0 && i < n && rows[i].st < 0);
  if (i >= 0 && i < n) pick_cursor = i;
}

void pick_choose(void) {
  Row *rows = N->rows;
  int n = list_rows(rows, MAXROWS);
  if (pick_cursor < n && rows[pick_cursor].st >= 0) {
    N->cfg.tsrc = rows[pick_cursor].src; N->cfg.treg = rows[pick_cursor].reg; N->cfg.tst = rows[pick_cursor].st;
    N->cfg.tauto = 0;                                    /* a station chosen: no longer AUTO */
    st_day_off = 0;
  }
  page = PG_STATION;
}

void pick_open(void) {
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
const FlowAtlas *atlas(void) {
  if (N->have_ffl) return &N->ffl;
  if (FLOW_ATLAS_PRESENT) return &FLOW_ATLAS;
  return NULL;
}

const TStation *find_station(const char *name, int *region_src, int *region) {
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

void arrow(int cx, int cy, double set, double len) {
  int x1 = cx + (int)lround(len * dsin(set)), y1 = cy - (int)lround(len * dcos(set));
  gfx_line(cx, cy, x1, y1, 1);
  int ax = x1 - (int)lround(3 * dsin(set - 30)), ay = y1 + (int)lround(3 * dcos(set - 30));
  int bx = x1 - (int)lround(3 * dsin(set + 30)), by = y1 + (int)lround(3 * dcos(set + 30));
  gfx_line(x1, y1, ax, ay, 1);
  gfx_line(x1, y1, bx, by, 1);
}

/* the station's local time zone (with US summer time) and its local date and hour at UT jd */
double st_local(const TStation *st, double ut, long *n, double *h) {
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
void draw_tide(void) {
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
void station_step(int d) {                /* the next / previous station of the list (8 2) */
  Row *rows = N->rows;
  int n = list_rows(rows, MAXROWS), cur = -1;
  for (int i = 0; i < n; i++)
    if (rows[i].st >= 0 && rows[i].src == N->cfg.tsrc && rows[i].reg == N->cfg.treg && rows[i].st == N->cfg.tst) cur = i;
  int i = cur;
  do { i = (i + d + n) % n; } while (rows[i].st < 0 && i != cur);
  if (rows[i].st >= 0) { N->cfg.tsrc = rows[i].src; N->cfg.treg = rows[i].reg; N->cfg.tst = rows[i].st; N->cfg.tauto = 0; }
}

void draw_station(void) {
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
  hint(N->cfg.tauto ? "AUTO: the nearest  8 2 station  4 6 day  5 today  ENTER list" : "8 2 station  4 6 day  5 today  ENTER list  0 AUTO");
}
