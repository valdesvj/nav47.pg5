/* views.c - NAV47: Almanac 47's views (python/native/c47screen21.py) above the DMCP menu bar.
 * ALMANAC, SPLIT, SKY (with its name loop), ANIM, ALLSKY and MOON47 (python/moon47.py). The same bodies, charts,
 * glyphs47 symbols, tinyFont labels and dotted celestial equator as Almanac 47, drawn on the C47 layer (c47scr.c);
 * the text in the DMCP system font (ntext.c) on 16-pixel rows: 208 rows of LCD instead of 240, so compressed.
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <main.h>
#include <dmcp.h>
#include "views.h"
#include "almdata.h"
#include "c47scr.h"
#include "ntext.h"
#include "moon47.h"

#define LUN 29.530589
#define R NT_ROW
#define CY(ly) (239 - (ly))                /* LCD row -> C47 y (up) */
#define BL(row) (CY((row) + 13))           /* the C47 y of a text row's baseline */
static const char *const PNAME[5] = { "", "VENUS", "MARS", "JUPITER", "SATURN" };

static long ipd(double x) { return (long)x; }

/* ---------------------------------------------------------------- the Almanac (c47screen.py) */
void al_compute(Al *al, double j, double lat, double lon) {
  al->j = j; al->lat = lat; al->lon = lon;
  double j0 = day0(j);
  al->times[0] = sun_event(j0, lat, lon, EV_NTWA);
  al->times[1] = sun_event(j0, lat, lon, EV_RISE);
  al->times[2] = sun_event(j0, lat, lon, EV_TRAN);
  al->times[3] = sun_event(j0, lat, lon, EV_SET);
  al->times[4] = sun_event(j0, lat, lon, EV_NTWP);
  sun_at(&al->sun, j);
  moon_phase(&al->sun, &al->illum, &al->age);
  moon_pos(&al->sun, &al->moon[0], &al->moon[1], &al->moon[2], &al->moon[3]);
  for (int p = 1; p <= 4; p++) {                                   /* PLN3 */
    double g, d, dist, hc, zn;
    planet_quick(&al->sun, p, &g, &d, &dist);
    hcz(lat, lon, d, g, &hc, &zn);
    if (hc > -1) planet_pos(&al->sun, p, dist * 0.0057755183, &al->planets[p][0], &al->planets[p][1],
                            &al->planets[p][2], &al->planets[p][3]);
    else { al->planets[p][0] = g; al->planets[p][1] = d; al->planets[p][2] = 0.0; al->planets[p][3] = 0.0; }
  }
  memset(al->have_star, 0, sizeof al->have_star);
}

static void al_star(Al *al, int n, double *g, double *d) {
  if (!al->have_star[n]) {
    star_pos(&al->sun, n, &al->star[n][0], &al->star[n][1], &al->star[n][2]);
    al->have_star[n] = 1;
  }
  *g = al->star[n][0];
  *d = al->star[n][1];
}

int vbodies(Al *al, VBody *out) {
  int n = 0;
  double hc, zn;
  hcz(al->lat, al->lon, al->sun.dec, al->sun.gha, &hc, &zn);
  out[n++] = (VBody){ 0, al->sun.gha, al->sun.dec, hc, zn };
  hcz(al->lat, al->lon, al->moon[1], al->moon[0], &hc, &zn);
  if (hc > 0) out[n++] = (VBody){ -1, al->moon[0], al->moon[1], hc, zn };
  static const int ORDER[4] = { 1, 3, 2, 4 };
  for (int i = 0; i < 4; i++) {
    int p = ORDER[i];
    double g = al->planets[p][0], d = al->planets[p][1];
    hcz(al->lat, al->lon, d, g, &hc, &zn);
    if (hc > 0) { out[n++] = (VBody){ -(p + 1), g, d, hc, zn }; break; }
  }
  for (int r = 0; r < 58 && n < NBODY; r++) {
    int s = BRIGHT[r];
    double g, d;
    al_star(al, s, &g, &d);
    hcz(al->lat, al->lon, d, g, &hc, &zn);
    if (hc > 10.0) out[n++] = (VBody){ s, g, d, hc, zn };
  }
  return n;
}

static int symbol(int id) { return id == 0 ? '@' : id == -1 ? '(' : id < 0 ? "<>=?"[-id - 2] : '*'; }
static const char *body_name(int id) {
  return id == 0 ? "SUN" : id == -1 ? "MOON" : id < 0 ? PNAME[-id - 1] : STAR_NAME47[id - 1];
}
static int phase_index(double age) { return (int)(ipd(age / LUN * 8 + 0.5) % 8); }
static const char *daylight(double hc) { return hc > 0 ? "DAY" : hc > -12 ? "TWILIGHT" : "NIGHT"; }
static const char *moon_word(const Al *al) {
  if (99.5 <= al->illum) return "FULL";
  if (0.5 > al->illum) return "NEW";
  return 14.765 < al->age ? "WANING" : "WAXING";
}
static double ut_hours(double j) { return pmod(j + 0.5, 1.0) * 24; }

static int chart_x(double zn, double off, double w, double x0) {
  double v = zn + off;
  v = v - 360 * floor(v / 360);
  return (int)(v * w / 360 + x0);
}
static int chart_ys(double sh, double base, int sign, double scale) {
  double h = sh * scale;
  return sign > 0 ? (int)(h + base) : (int)(base - h);
}

/* ---------------------------------------------------------------- number formats (the C47 printers' rounding) */
static void f_dm(char *s, double v) {                    /* 123°45.6 (no sign) */
  long t = ipd(fabs(v) * 600 + 0.5), d = ipd(t / 600.0);
  t -= d * 600;
  sprintf(s, "%ld" NT_DEG "%ld%ld.%ld", d, t / 100, (t / 10) % 10, t % 10);
}
static void f_hm(char *s, double h) {
  if (h > 98) { strcpy(s, "--:--"); return; }
  long m = ipd(h * 60 + 0.5);
  sprintf(s, "%02ld:%02ld", (m / 60) % 24, m % 60);
}
static void f_zn(char *s, double v) {
  long t = ipd(v * 10 + 0.5);
  if (3600 <= t) t = 0;
  sprintf(s, "%03ld.%ld", t / 10, t % 10);
}
static void f_f1(char *s, double v) { long m = ipd(fabs(v) * 10 + 0.5); sprintf(s, "%ld.%ld", m / 10, m % 10); }
static void f_date(char *s, double j) {
  int y, m, d;
  double h;
  jd_to_ymd(j + 1e-9, &y, &m, &d, &h);
  sprintf(s, "%02d-%02d-%04d", d, m, y);
}

/* the header row: date, time UT, the DR position (as Almanac 47's top line) */
static void header(const Al *al, double j) {
  char s[80], a[24], b[24], t[8], dt[16];
  f_date(dt, j);
  f_hm(t, ut_hours(j));
  f_dm(a, fabs(al->lat));
  f_dm(b, fabs(al->lon));
  sprintf(s, "%s %s UT", dt, t);
  nt(1, 0, s);
  sprintf(s, "%s%s %s%s%s", al->lat < 0 ? "S" : "N", a, al->lon < 0 ? "W" : "E", fabs(al->lon) < 100 ? "0" : "", b);
  nt_r(399, 0, s);
  lcd_fill_rect(0, 15, LCD_X, 1, 1);
}

/* ---------------------------------------------------------------- the body table (ALMANAC, SPLIT) */
typedef struct { int name, gha, ns, dec, hc, zn; } Cols;
static int NAMEW;
static Cols cols(void) {
  int c = nt_cw(), dm3 = nt_w("000" NT_DEG "00.0"), dm2 = nt_w("00" NT_DEG "00.0");
  if (!NAMEW) {
    for (int i = 0; i < 58; i++) if (nt_w(STAR_NAME47[i]) > NAMEW) NAMEW = nt_w(STAR_NAME47[i]);
    if (nt_w("JUPITER") > NAMEW) NAMEW = nt_w("JUPITER");
  }
  Cols X;
  X.name = 24;
  X.gha = X.name + NAMEW + 4 + dm3;             /* right edges */
  X.ns = X.gha + 6;
  X.dec = X.ns + nt_w("N") + 2 + dm2;
  X.hc = X.dec + 6 + dm2;
  X.zn = X.hc + 6 + nt_w("000.0");
  (void)c;
  int e = (398 - X.zn) / 4;
  if (e > 0) { X.gha += e; X.ns += 2 * e; X.dec += 2 * e; X.hc += 3 * e; X.zn += 4 * e; }
  return X;
}

static void table_heads(int y, const Cols *X) {
  nt(X->name, y, "BODY");
  nt_c(X->gha - nt_w("000" NT_DEG "00.0") / 2, y, "GHA");
  nt_c((X->ns + X->dec) / 2, y, "DEC");
  nt_c(X->hc - nt_w("00" NT_DEG "00.0") / 2, y, "HC");
  nt_c(X->zn - nt_w("000.0") / 2, y, "ZN");
}

/* ---------------------------------------------------------------- ALMANAC */
void view_almanac(Al *al) {
  c47_clear();
  Cols X = cols();
  VBody B[NBODY];
  int nb = vbodies(al, B);
  for (int i = 0; i < nb; i++) {                       /* the symbols first (C47 layer) */
    int y = 2 * R + R * (i + 1);
    c47_glyph(&SYMB, symbol(B[i].id), BL(y), 0);
    if (B[i].id > 0) c47_tiny_num(BL(y) + 7, 14, B[i].id);
  }
  c47_glyph(&SYMB, '0' + phase_index(al->age), BL(12 * R + 1), 0);
  c47_show();
  header(al, al->j);
  table_heads(R, &X);
  char s[64], a[8], b[8], t[16];
  nt(X.name, 2 * R, "ARIES");
  f_dm(s, al->sun.aries); nt_r(X.gha, 2 * R, s);
  for (int i = 0; i < nb; i++) {
    int y = 3 * R + R * i;
    VBody *v = &B[i];
    char nm[12];
    snprintf(nm, sizeof nm, "%.11s", body_name(v->id));
    nt(X.name, y, nm);
    f_dm(s, v->gha); nt_r(X.gha, y, s);
    nt(X.ns, y, v->dec < 0 ? "S" : "N");
    f_dm(s, fabs(v->dec)); nt_r(X.dec, y, s);
    f_dm(s, fabs(v->hc));
    if (v->hc < 0) nt_inv(X.hc - nt_w(s), y, s); else nt_r(X.hc, y, s);
    f_zn(s, v->zn); nt_r(X.zn, y, s);
  }
  lcd_fill_rect(0, 11 * R, LCD_X, 1, 1);
  /* the Sun's day and the Moon (Almanac 47's three footer lines in two) */
  f_hm(a, al->times[1]); f_hm(b, al->times[3]); f_hm(t, al->times[2]);
  sprintf(s, "RISE %s SET %s MER %s", a, b, t);
  nt(1, 11 * R + 2, s);
  f_hm(a, al->times[0]); f_hm(b, al->times[4]);
  f_f1(t, al->sun.sd);
  sprintf(s, "TWI %s %s SD %s", a, b, t);
  nt_r(399, 11 * R + 2, s);
  char hp[24], sd[24];
  f_f1(t, al->age); f_f1(hp, al->moon[2]); f_f1(sd, al->moon[3]);
  sprintf(s, "MOON %ld%% %s  AGE %s d", (long)ipd(al->illum + 0.5), moon_word(al), t);
  nt(16, 12 * R + 1, s);
  sprintf(s, "HP %s  SD %s", hp, sd);
  nt_r(399, 12 * R + 1, s);
}

/* ---------------------------------------------------------------- charts */
static void alt_axis(int HY, int HS, const int *xt, int nx) {
  static const int V[6] = { 10, 20, 30, 45, 60, 90 };
  for (int i = 0; i < 6; i++) {
    int y = HY + (int)(HS * dsin(V[i]));
    for (int k = 0; k < nx; k++) c47_pixel(y, xt[k]);
    c47_tiny_num(y - 2, 2, V[i]);
  }
}

static const int COLS13[13] = { 20, 51, 82, 113, 145, 176, 207, 238, 270, 301, 332, 363, 395 };
static const int LX[5] = { 19, 112, 206, 300, 394 };

/* the horizon chart of SKY and SPLIT: horizon at C47 y HY, the zenith HS above */
static void horizon_chart(const Al *al, int HY, int HS) {
  c47_hline(HY, 20, 376);
  for (int y = HY + 2; y < HY + HS + 1; y += 3) c47_pixel(y, 18);
  for (int i = 0; i < 13; i++)
    for (int y = HY - 1; y >= HY - 3; y--) c47_pixel(y, COLS13[i]);
  static const int XT[3] = { 15, 16, 17 };
  alt_axis(HY, HS, XT, 3);
  double off = al->lat < 0 ? 180 : 0;
  const char *lab = off ? "SWNES" : "NESWN";
  for (int k = 0; k < 5; k++) { char t[2] = { lab[k], 0 }; c47_text(HY - 9, LX[k], t, &TINY21); }
  double hc, zn;
  for (int g = 0; g < 359; g += 2) {
    double sh = hczs(al->lat, al->lon, 0, g, &hc, &zn);
    if (hc > 1e-4) c47_dot4(chart_ys(sh, HY, 1, HS), chart_x(zn, off, 375, 20));
  }
}

static void plot_body(const Al *al, const VBody *b, int HY, int HS, int *px, int *py) {
  double hc, zn, sh = hczs(al->lat, al->lon, b->dec, b->gha, &hc, &zn), off = al->lat < 0 ? 180 : 0;
  int x = chart_x(zn, off, 375, 20), y = chart_ys(sh, HY, 1, HS);
  if (b->hc > 0) {
    c47_glyph(&SYMS, symbol(b->id), y - 3, x - 3);
    if (b->id > 0) { int lx = x + 5; if (lx > 380) lx -= 22; c47_tiny_num(y - 3, lx, b->id); }
  }
  if (px) { *px = x; *py = CY(y); }
}

/* ---------------------------------------------------------------- SPLIT: the horizon chart over the table */
void view_split(Al *al) {
  c47_clear();
  const int HY = CY(77), HS = 58;
  horizon_chart(al, HY, HS);
  Cols X = cols();
  VBody B[NBODY];
  int nb = vbodies(al, B);
  int rows = (208 - 104) / R;
  for (int i = 0; i < nb; i++) {
    plot_body(al, &B[i], HY, HS, NULL, NULL);
    if (i < rows) {
      int y = 104 + R * i;
      c47_glyph(&SYMB, symbol(B[i].id), BL(y), 0);
      if (B[i].id > 0) c47_tiny_num(BL(y) + 7, 14, B[i].id);
    }
  }
  c47_show();
  header(al, al->j);
  table_heads(88, &X);
  for (int i = 0; i < nb && i < rows; i++) {
    int y = 104 + R * i;
    char s[24], nm[12];
    VBody *v = &B[i];
    snprintf(nm, sizeof nm, "%.11s", body_name(v->id));
    nt(X.name, y, nm);
    f_dm(s, v->gha); nt_r(X.gha, y, s);
    nt(X.ns, y, v->dec < 0 ? "S" : "N");
    f_dm(s, fabs(v->dec)); nt_r(X.dec, y, s);
    f_dm(s, fabs(v->hc));
    if (v->hc < 0) nt_inv(X.hc - nt_w(s), y, s); else nt_r(X.hc, y, s);
    f_zn(s, v->zn); nt_r(X.zn, y, s);
  }
}

/* ---------------------------------------------------------------- SKY: the horizon chart and the name loop */
int view_sky(Al *al, int frame) {
  c47_clear();
  const int HB = CY(180), HS = 160;
  horizon_chart(al, HB, HS);
  VBody B[NBODY];
  int nb = vbodies(al, B), bx[NBODY], by[NBODY];
  for (int i = 0; i < nb; i++) plot_body(al, &B[i], HB, HS, &bx[i], &by[i]);
  c47_show();
  header(al, al->j);
  nt_c(200, 192, daylight(B[0].hc));
  int fr[NBODY], nf = 0;                             /* the name loop (consecutive duplicates removed) */
  for (int i = 0; i < nb; i++) {
    int f = B[i].hc <= 0 ? -1 : i;
    if (!nf || fr[nf - 1] != f) fr[nf++] = f;
  }
  if (!nf) return 1;
  int k = fr[((frame % nf) + nf) % nf];
  if (k >= 0) {
    const char *t = body_name(B[k].id);
    int nx = bx[k] + 8 + (B[k].id > 0 ? 14 : 0);
    if (nx + nt_w(t) > 396) nx = bx[k] - 10 - nt_w(t);
    int ny = by[k] - 8;
    if (ny < 17) ny = 17;
    nt_inv(nx, ny, t);
  }
  return nf;
}

/* ---------------------------------------------------------------- ANIM and ALLSKY: the whole sphere */
#define HYA CY(104)
#define HSA 84

static void sky_chart(double off) {
  c47_hline(HYA, 0, 400);
  for (int y = HYA - HSA; y < HYA + HSA + 1; y += 3) c47_pixel(y, 18);
  static const int V[6] = { 10, 20, 30, 45, 60, 90 };
  for (int i = 0; i < 6; i++) {
    int dy = (int)(HSA * dsin(V[i]));
    int ys[2] = { HYA + dy, HYA - dy };
    for (int k = 0; k < 2; k++) {
      c47_pixel(ys[k], 15); c47_pixel(ys[k], 16); c47_pixel(ys[k], 17);
      c47_tiny_num(ys[k] - 2, 2, V[i]);
    }
  }
  for (int i = 0; i < 13; i++) { c47_pixel(HYA - 1, COLS13[i]); c47_pixel(HYA - 2, COLS13[i]); }
  const char *lab = off ? "SWNES" : "NESWN";
  for (int k = 0; k < 5; k++) { char t[2] = { lab[k], 0 }; c47_text(HYA - 10, LX[k], t, &TINY21); }
}

static void apos(double lat, double lon, double off, double dec, double gha, double *hc, int *x, int *y) {
  double zn, sh = hczs(lat, lon, dec, gha, hc, &zn);
  *x = chart_x(zn, off, 375, 20);
  *y = chart_ys(sh, HYA, 1, HSA);
}

void view_anim(Al *al, int frames, double step, int upto) {
  double lat = al->lat, lon = al->lon, off = lat < 0 ? 180 : 0;
  c47_clear();
  sky_chart(off);
  double hc, hs = 0;
  int x, y;
  for (int g = 0; g < 359; g += 2) { apos(lat, lon, off, 0, g, &hc, &x, &y); c47_dot4(y, x); }
  if (frames < 1) frames = 1;
  if (upto >= frames) upto = frames - 1;
  double j = al->j;
  for (int k = 0; k <= upto; k++) {
    j = al->j + k * step / 24;
    double g, d;
    sun_fast(j, &g, &d);
    int xs, ys, xm, ym;
    apos(lat, lon, off, d, g, &hs, &xs, &ys);
    Sun q;                                          /* _Quick: T and the mean GHA Aries */
    memset(&q, 0, sizeof q);
    double dd = j - 2451545.0;
    q.T = (dd + ASTRO_DT / 86400.0) / 36525.0;
    q.aries = pmod(dd * 360.98564736629 + 280.46061837, 360.0);
    moon_quick(&q, &g, &d);
    apos(lat, lon, off, d, g, &hc, &xm, &ym);
    c47_glyph(&SYMS, '@', ys - 3, xs - 3);
    c47_glyph(&SYMS, '(', ym - 3, xm - 3);
  }
  c47_show();
  header(al, j);
  nt_c(200, 192, daylight(hs));
}

void view_allsky(Al *al) {
  c47_clear();
  double off = al->lat < 0 ? 180 : 0;
  sky_chart(off);
  const Sun *s = &al->sun;
  double hc;
  int x, y;
  for (int g = 0; g < 358; g += 3) { apos(al->lat, al->lon, off, 0, g, &hc, &x, &y); c47_dot4(y, x); }
  double ty = (al->j - 2451545.0) / 365.25 / 3600.0;
  for (int n = 1; n <= 58; n++) {
    double ra = ST[n - 1][0], dec = ST[n - 1][1];
    double dec2 = dcos(ra) * 20.0431 * ty + dec;
    double ra2 = (dsin(ra) * dtan(dec) * 20.0431 + 46.1244) * ty + ra;
    apos(al->lat, al->lon, off, dec2, pmod(s->aries - ra2, 360.0), &hc, &x, &y);
    if (y < CY(190) || y > CY(17)) continue;
    c47_glyph(&SYMS, '*', y - 3, x - 3);
    int lx = x + 5;
    if (lx > 388) lx -= 17;
    c47_tiny_num(y - 3, lx, n);
  }
  for (int p = 1; p <= 4; p++) {
    apos(al->lat, al->lon, off, al->planets[p][1], al->planets[p][0], &hc, &x, &y);
    c47_glyph(&SYMS, "<>=?"[p - 1], y - 3, x - 3);
  }
  apos(al->lat, al->lon, off, al->moon[1], al->moon[0], &hc, &x, &y);
  c47_glyph(&SYMS, '(', y - 3, x - 3);
  double hs;
  apos(al->lat, al->lon, off, s->dec, s->gha, &hs, &x, &y);
  c47_glyph(&SYMS, '@', y - 3, x - 3);
  c47_show();
  header(al, al->j);
  nt_c(200, 192, daylight(hs));
}

/* ---------------------------------------------------------------- MOON47 (python/moon47.py page, moon47_screen) */
static const char *const PHSHORT[4] = { "NEW", "1ST QTR", "FULL", "LAST QTR" };

void view_moon(Al *al, int south) {
  Moon47 p;
  moon47_page(al->j, south, &p);
  c47_clear();
  const int cx = 70, cy = CY(98), r = 60;
  int runs[2][2];
  for (int dx = -r; dx <= r; dx++) {
    int n = moon47_disc_runs(r, p.age, south, dx, runs);
    for (int i = 0; i < n; i++)
      for (int a = runs[i][0]; a <= runs[i][1]; a++) { c47_pixel(cy + a, cx + dx); if (a) c47_pixel(cy - a, cx + dx); }
  }
  for (int i = 0; i < 8; i++) {                       /* the eight phases, today's inverted */
    int x = 6 + i * 20;
    c47_glyph(&SYMB, '0' + (south ? (8 - i) % 8 : i), BL(176), x);
    if ((south ? (8 - i) % 8 : i) == p.index) c47_xor_box(BL(176) - 2, x - 3, 18, 16);
  }
  c47_show();
  char s[64], t[16], d[16];
  f_date(d, al->j);
  f_hm(t, ut_hours(al->j));
  sprintf(s, "MOON47   %s %s UT", d, t);
  nt(1, 0, s);
  lcd_fill_rect(0, 15, LCD_X, 1, 1);
  const int X = 146;
  nt(X, 20, M47_NAMES[p.index]);
  sprintf(s, "LIT %ld%%   AGE %.1f d", (long)floor(p.lit + 0.5), p.age);
  nt(X, 40, s);
  sprintf(s, "HP %.1f'  SD %.1f'", p.hp, p.sd);
  nt(X, 56, s);
  for (int k = 0; k < 4; k++) {
    int y, m, dd;
    double h;
    jd_to_ymd(p.next_t[k] + 1e-9, &y, &m, &dd, &h);
    f_hm(t, h);
    sprintf(s, "%-8s %02d-%02d %s", PHSHORT[p.next_k[k]], dd, m, t);
    nt(X, 80 + R * k, s);
  }
  nt(1, 192, south ? "AS SEEN FROM THE SOUTH" : "AS SEEN FROM THE NORTH");
  nt_r(399, 192, "+/- FLIP");
}
