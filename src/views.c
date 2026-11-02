/* views.c - NAV47: the C47 views of Almanac 47 (python/native/c47screen21.py), line for line:
 * ALMANAC, CHART, SKY (with its name loop), SPLIT, ANIM, ALLSKY. The same 8 bodies, the header line, the C47
 * standard font (GRFNT 21), the tinyFont chart labels, the glyphs47 symbols, the dotted celestial equator.
 * test/test_views.py compares them with the Python pixel for pixel.
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "views.h"
#include "almdata.h"
#include "c47scr.h"

#define LUN 29.530589
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

static int width(const char *t) { return c47_width(t, &F21); }

/* chart_x / chart_ys of c47screen.py */
static int chart_x(double zn, double off, double w, double x0) {
  double v = zn + off;
  v = v - 360 * floor(v / 360);
  return (int)(v * w / 360 + x0);
}
static int chart_ys(double sh, double base, int sign, double scale) {
  double h = sh * scale;
  return sign > 0 ? (int)(h + base) : (int)(base - h);
}

/* the layout columns (genviews_atx.py, mode t21) */
static int DIG, PDM, ZNW, HMW, DTW, NAMEW;
static void metrics(void) {
  if (DIG) return;
  DIG = width("0");
  PDM = 4 * DIG + DIG + 2 * DIG + width(".") + DIG;
  ZNW = width("000.0"); HMW = width("00:00"); DTW = width("00-00-0000");
  NAMEW = 0;
  for (int i = 0; i < 58; i++) if (width(STAR_NAME47[i]) > NAMEW) NAMEW = width(STAR_NAME47[i]);
}

typedef struct { int date, time, UT, DR, N, lat, E, lon; } TopX;
static TopX top_x(void) {
  TopX X;
  X.date = 2;
  X.time = 2 + DTW + DIG;
  X.UT = X.time + HMW + DIG;
  X.DR = X.UT + width("UT") + DIG;
  X.N = X.DR + width("DR") + DIG;
  X.lat = X.N + width("N") + DIG - 2 * DIG;
  X.E = X.lat + PDM + DIG;
  X.lon = X.E + width("W") + DIG - DIG;
  return X;
}

typedef struct { int num, name, gha, ns, dec, hc, zn, hGHA, hDEC, hHC, hZN; } TabX;
static int cen(double a, double b, const char *t) { return (int)nearbyint((a + b - width(t)) / 2); }
static TabX table_x(void) {
  TabX X;
  X.num = 14; X.name = 34;
  X.gha = X.name + NAMEW;
  X.ns = X.gha + PDM + 4;
  X.dec = X.ns + width("N") + 6 - 2 * DIG;
  X.hc = X.dec + PDM + 6 - DIG;
  X.zn = X.hc + PDM + 6;
  int e = (398 - X.zn - ZNW) / 4;           /* Python // on a positive number */
  X.gha += 1 * e; X.ns += 2 * e; X.dec += 2 * e; X.hc += 3 * e; X.zn += 4 * e;
  X.hGHA = cen(X.gha + DIG, X.gha + PDM, "GHA");
  X.hDEC = cen(X.ns, X.dec + PDM, "DEC");
  X.hHC = cen(X.hc + DIG, X.hc + PDM, "HC");
  X.hZN = cen(X.zn, X.zn + ZNW, "ZN");
  return X;
}

static int state_x(const char *w) { return 200 - width(w) / 2; }

static void header(const Al *al, double j, double uth) {
  TopX T = top_x();
  double lat = al->lat, lon = al->lon;
  c47_pdat(226, T.date, j);
  c47_phm(226, T.time, uth);
  c47_text(226, T.UT, "UT", &F21);
  c47_text(226, T.DR, "DR", &F21);
  c47_text(226, T.N, lat < 0 ? "S" : "N", &F21);
  c47_pdm(226, T.lat, fabs(lat));
  c47_text(226, T.E, lon < 0 ? "W" : "E", &F21);
  c47_pdm(226, T.lon, fabs(lon));
  c47_text(226, 388, "S", &F21);
  c47_pixel(-221, 0);
}

static void tiny(int y, int x, const char *s) { c47_text(y, x, s, &TINY21); }

static void hc_box(int y, int x) { c47_xor_box(y - 1, x + 7, PDM - 6, 14); }

static void alt_axis(int HY, int HS, const int *xt, int nx, int label) {
  static const int V[6] = { 10, 20, 30, 45, 60, 90 };
  for (int i = 0; i < 6; i++) {
    int y = HY + (int)(HS * dsin(V[i]));
    for (int k = 0; k < nx; k++) c47_pixel(y, xt[k]);
    if (label) c47_tiny_num(y - 2, 2, V[i]);
  }
}

static void hczs3(const Al *al, double dec, double gha, double *hc, double *zn, double *sh) {
  *sh = hczs(al->lat, al->lon, dec, gha, hc, zn);
}

static const int COLS13[13] = { 20, 51, 82, 113, 145, 176, 207, 238, 270, 301, 332, 363, 395 };

/* ---------------------------------------------------------------- the menu (NAV) */
void view_menu(Al *al, const char *const *items, int nitems) {
  metrics();
  c47_clear();
  c47_text(226, 2, "NAV47  ALMANAC 47", &F21);
  c47_text(226, 230, "VALID 2000-2050", &F21);
  TopX T = top_x();
  int y = 206;
  c47_pdat(y, T.date, al->j);
  c47_phm(y, T.time, ut_hours(al->j));
  c47_text(y, T.UT, "UT", &F21);
  c47_text(y, T.DR, "DR", &F21);
  c47_text(y, T.N, al->lat < 0 ? "S" : "N", &F21);
  c47_pdm(y, T.lat, fabs(al->lat));
  c47_text(y, T.E, al->lon < 0 ? "W" : "E", &F21);
  c47_pdm(y, T.lon, fabs(al->lon));
  c47_pixel(-199, 0);
  for (int i = 0; i < nitems; i++) {
    int col = i / 5, row = i % 5;
    c47_text(176 - 28 * row, 16 + 200 * col, items[i], &F21);
  }
  c47_text(36, 2, "KEY A NUMBER   + MENU   UP DN 1 HOUR   F1-F6", &F21);
  c47_pixel(-22, 0);
  c47_text(6, 2, "DOES NOT REPLACE THE NAUTICAL ALMANAC", &F21);
}

/* ---------------------------------------------------------------- 1 ALMANAC */
void view_almanac(Al *al) {
  metrics();
  c47_clear();
  TabX X = table_x();
  int NX = X.name, GX = X.gha, NSX = X.ns, DX = X.dec, HX = X.hc, ZX = X.zn;
  const int TOP = 193, PITCH = 16, ROWS = NBODY + 1;
  header(al, al->j, ut_hours(al->j));
  c47_text(207, X.name, "BODY", &F21); c47_text(207, X.hGHA, "GHA", &F21); c47_text(207, X.hDEC, "DEC", &F21);
  c47_text(207, X.hHC, "HC", &F21); c47_text(207, X.hZN, "ZN", &F21);
  c47_text(TOP, NX, "ARIES", &F21);
  c47_pdm(TOP, GX, al->sun.aries);
  int y = TOP - PITCH;
  VBody B[NBODY];
  int nb = vbodies(al, B);
  for (int i = 0; i < nb; i++) {
    VBody *b = &B[i];
    c47_glyph(&SYMB, symbol(b->id), y, 0);
    if (b->id > 0) c47_pinb(y, X.num, b->id);
    c47_text(y, NX, body_name(b->id), &F21);
    c47_pdm(y, GX, b->gha);
    c47_text(y, NSX, b->dec < 0 ? "S" : "N", &F21);
    c47_pdm(y, DX, fabs(b->dec));
    c47_pdm(y, HX, b->hc);
    c47_pzn(y, ZX, b->zn);
    if (b->hc < 0) c47_xor_box(y - 1, HX + 7, PDM - 6, 14);
    y -= PITCH;
  }
  int FY = TOP - PITCH * ROWS;
  c47_pixel(-(FY + 8), 0);
  int y1 = FY - 6, y2 = FY - 20, y3 = FY - 34;
  int t1 = 2, w;
  w = width("NAUT TWI"); if (width("RISE/SET") > w) w = width("RISE/SET"); if (width("MER PASS") > w) w = width("MER PASS");
  t1 = 2 + w + 8;
  int t2 = t1 + HMW + 8, x;
  c47_text(y1, 2, "NAUT TWI", &F21); c47_phm(y1, t1, al->times[0]); c47_phm(y1, t2, al->times[4]);
  c47_text(y2, 2, "RISE/SET", &F21); c47_phm(y2, t1, al->times[1]); c47_phm(y2, t2, al->times[3]);
  c47_text(y3, 2, "MER PASS", &F21); c47_phm(y3, t1, al->times[2]); x = c47_text(y3, t2, "SD ", &F21); c47_pf1(y3, x, al->sun.sd);
  const int MX = 212;
  x = c47_text(y1, MX, "MOON ", &F21); x = c47_pinb(y1, x, al->illum); x = c47_text(y1, x, "% ", &F21);
  x = c47_glyph(&SYMB, '0' + phase_index(al->age), y1, x); x = c47_text(y1, x, " ", &F21); c47_text(y1, x, moon_word(al), &F21);
  x = c47_text(y2, MX, "AGE ", &F21); x = c47_pf1(y2, x, al->age); c47_text(y2, x, " DAYS", &F21);
  x = c47_text(y3, MX, "HP ", &F21); x = c47_pf1(y3, x, al->moon[2]); x = c47_text(y3, x, " SD ", &F21); c47_pf1(y3, x, al->moon[3]);
}

/* ---------------------------------------------------------------- 2 CHART */
void view_chart(Al *al) {
  metrics();
  c47_clear();
  const int X0 = 176, CW = 150, HY = 14, HS = 200;
  for (int y = 0; y < 221; y++) c47_pixel(y, X0 - 4);
  c47_pixel(-221, 0);
  c47_hline(HY, 18, CW + 1);
  for (int y = HY; y < HY + HS + 1; y++) c47_pixel(y, 17);
  static const int XT[2] = { 15, 16 };
  alt_axis(HY, HS, XT, 2, 1);
  double off = al->lat < 0 ? 180 : 0;
  const char *lab = off ? "SWNES" : "NESWN";
  for (int k = 0; k < 5; k++) { char t[2] = { lab[k], 0 }; tiny(5, 16 + CW * k / 4, t); }
  double hc, zn, sh;
  for (int g = 0; g < 358; g += 3) {
    hczs3(al, 0, g, &hc, &zn, &sh);
    if (hc > 1e-4) c47_dot4(chart_ys(sh, HY, 1, HS), chart_x(zn, off, CW, 18));
  }
  header(al, al->j, ut_hours(al->j));
  int ZX = 398 - ZNW, HX = ZX - 6 - PDM, NM = X0 + 14;
  c47_text(207, NM, "BODY", &F21);
  c47_text(207, (int)nearbyint(HX + 8 + (PDM - 8 - width("HC")) / 2.0), "HC", &F21);
  c47_text(207, (int)nearbyint(ZX + (ZNW - width("ZN")) / 2.0), "ZN", &F21);
  int y = 193;
  VBody B[NBODY];
  int nb = vbodies(al, B);
  for (int i = 0; i < nb; i++) {
    VBody *b = &B[i];
    hczs3(al, b->dec, b->gha, &hc, &zn, &sh);
    int cx = chart_x(zn, off, CW, 18), cy = chart_ys(sh, HY, 1, HS);
    if (b->hc > 0) {
      c47_glyph(&SYMS, symbol(b->id), cy - 3, cx - 3);
      if (b->id > 0) {
        int lx = cx + 5;
        if (lx > 18 + CW - 20) lx -= 22;
        c47_tiny_num(cy - 3, lx, b->id);
      }
    }
    c47_glyph(&SYMB, symbol(b->id), y, X0);
    if (b->id > 0) {
      c47_pinb(y, NM, b->id);
      c47_text(y, NM + width("00 "), body_name(b->id), &F21);
    } else c47_text(y, NM, body_name(b->id), &F21);
    c47_pdm(y, HX, b->hc);
    c47_pzn(y, ZX, b->zn);
    if (b->hc < 0) hc_box(y, HX);
    y -= 16;
  }
}

/* ---------------------------------------------------------------- 4 SKY */
static void sky_pos(double sh, double zn, double off, int HB, int HS, int *r, int *x) {
  *x = chart_x(zn, off, 375, 20);
  *r = 241 - chart_ys(sh, 241 - HB, -1, HS);
}

int view_sky(Al *al, int frame) {
  metrics();
  c47_clear();
  const int HB = 28, HS = 184;
  c47_hline(HB, 20, 376);
  for (int y = HB + 2; y < 213; y += 3) c47_pixel(y, 18);
  for (int i = 0; i < 13; i++)
    for (int y = HB - 1; y >= HB - 3; y--) c47_pixel(y, COLS13[i]);
  static const int XT[3] = { 15, 16, 17 };
  alt_axis(HB, HS, XT, 3, 1);
  double off = al->lat < 0 ? 180 : 0;
  const char *lab = off ? "SWNES" : "NESWN";
  static const int LX[5] = { 19, 112, 206, 300, 394 };
  for (int k = 0; k < 5; k++) { char t[2] = { lab[k], 0 }; tiny(HB - 9, LX[k], t); }
  double hc, zn, sh;
  int r, x;
  for (int g = 0; g < 359; g += 2) {
    hczs3(al, 0, g, &hc, &zn, &sh);
    if (hc > 1e-4) { sky_pos(sh, zn, off, HB, HS, &r, &x); c47_dot4(r, x); }
  }
  VBody B[NBODY];
  int nb = vbodies(al, B);
  for (int i = 0; i < nb; i++) {
    VBody *b = &B[i];
    if (b->id == 0 || b->hc > 0) {
      hczs3(al, b->dec, b->gha, &hc, &zn, &sh);
      sky_pos(sh, zn, off, HB, HS, &r, &x);
      if (b->hc > 0) c47_glyph(&SYMS, symbol(b->id), r - 3, x - 3);
      if (b->id > 0) {
        int lx = x + 5;
        if (lx > 380) lx -= 22;
        c47_tiny_num(r - 3, lx, b->id);
      }
      if (b->id == 0) { const char *w = daylight(b->hc); c47_text(4, state_x(w), w, &F21); }
    }
  }
  header(al, al->j, ut_hours(al->j));
  /* the name loop: the frames (consecutive duplicates removed) */
  int fr[NBODY], nf = 0;
  for (int i = 0; i < nb; i++) {
    int f = B[i].hc <= 0 ? -1 : i;
    if (!nf || fr[nf - 1] != f) fr[nf++] = f;
  }
  if (!nf) return 1;
  int k = fr[((frame % nf) + nf) % nf];
  if (k >= 0) {
    VBody *b = &B[k];
    sky_pos(dsin(b->hc), b->zn, off, HB, HS, &r, &x);
    int nx = x + 8 + (b->id > 0 ? 14 : 0);
    const char *t = body_name(b->id);
    if (nx + 8 * (int)strlen(t) > 380) nx = x - 28 - 8 * (int)strlen(t);
    /* XOR the name: draw it on an empty screen, then XOR it in (base ^ name as c47screen21) */
    static C47Screen *tmp;
    if (!tmp) tmp = (C47Screen *)calloc(1, sizeof(C47Screen));
    if (tmp) {
      C47Screen *main = c47();
      memcpy(tmp, main, sizeof *tmp);
      c47_clear();
      c47_text(r - 3, nx, t, &F21);
      for (int yy = 0; yy < SH; yy++)
        for (int i = 0; i < SW / 8; i++) tmp->b[yy][i] ^= main->b[yy][i];
      memcpy(main, tmp, sizeof *tmp);
    }
  }
  return nf;
}

/* ---------------------------------------------------------------- 5 SPLIT */
void view_split(Al *al) {
  metrics();
  c47_clear();
  TabX X = table_x();
  int NX = X.name, GX = X.gha, NSX = X.ns, DX = X.dec, HX = X.hc, ZX = X.zn;
  const int HY = 144, HS = 68, TOP = 107;
  header(al, al->j, ut_hours(al->j));
  c47_hline(HY, 20, 376);
  for (int y = HY; y < HY + HS + 1; y += 3) c47_pixel(y, 18);
  static const int XT[3] = { 15, 16, 17 };
  alt_axis(HY, HS, XT, 3, 1);
  for (int i = 0; i < 13; i++) { c47_pixel(HY - 1, COLS13[i]); c47_pixel(HY - 2, COLS13[i]); }
  double off = al->lat < 0 ? 180 : 0;
  const char *lab = off ? "SWNES" : "NESWN";
  static const int LX[5] = { 19, 112, 206, 300, 394 };
  for (int k = 0; k < 5; k++) { char t[2] = { lab[k], 0 }; tiny(HY - 9, LX[k], t); }
  double hc, zn, sh;
  for (int g = 0; g < 358; g += 3) {
    hczs3(al, 0, g, &hc, &zn, &sh);
    if (hc > 1e-4) c47_dot4(chart_ys(sh, HY, 1, HS), chart_x(zn, off, 375, 20));
  }
  c47_text(TOP + 14, X.name, "BODY", &F21); c47_text(TOP + 14, X.hGHA, "GHA", &F21); c47_text(TOP + 14, X.hDEC, "DEC", &F21);
  c47_text(TOP + 14, X.hHC, "HC", &F21); c47_text(TOP + 14, X.hZN, "ZN", &F21);
  int y = TOP;
  VBody B[NBODY];
  int nb = vbodies(al, B);
  for (int i = 0; i < nb; i++) {
    if (y < 9) break;
    VBody *b = &B[i];
    hczs3(al, b->dec, b->gha, &hc, &zn, &sh);
    int cx = chart_x(zn, off, 375, 20), cy = chart_ys(sh, HY, 1, HS);
    if (b->hc > 0) {
      c47_glyph(&SYMS, symbol(b->id), cy - 3, cx - 3);
      if (b->id > 0) {
        int lx = cx + 5;
        if (lx > 380) lx -= 22;
        c47_tiny_num(cy - 3, lx, b->id);
      }
    }
    c47_glyph(&SYMB, symbol(b->id), y, 0);
    if (b->id > 0) c47_pinb(y, X.num, b->id);
    c47_text(y, NX, body_name(b->id), &F21);
    c47_pdm(y, GX, b->gha);
    c47_text(y, NSX, b->dec < 0 ? "S" : "N", &F21);
    c47_pdm(y, DX, fabs(b->dec));
    c47_pdm(y, HX, b->hc);
    c47_pzn(y, ZX, b->zn);
    if (b->hc < 0) hc_box(y, HX);
    y -= 14;
  }
}

/* ---------------------------------------------------------------- 6 ANIM and 7 ALLSKY */
#define HYA 118
#define HSA 94

static void sky_chart(double off, int HY, int HS) {
  c47_pixel(-HY, 0);
  for (int y = HY - HS; y < HY + HS + 1; y += 3) c47_pixel(y, 18);
  static const int V[6] = { 10, 20, 30, 45, 60, 90 };
  for (int i = 0; i < 6; i++) {
    int dy = (int)(HS * dsin(V[i]));
    int ys[2] = { HY + dy, HY - dy };
    for (int k = 0; k < 2; k++) {
      c47_pixel(ys[k], 15); c47_pixel(ys[k], 16); c47_pixel(ys[k], 17);
      c47_tiny_num(ys[k] - 2, 2, V[i]);
    }
  }
  for (int i = 0; i < 13; i++) { c47_pixel(HY - 1, COLS13[i]); c47_pixel(HY - 2, COLS13[i]); }
  const char *lab = off ? "SWNES" : "NESWN";
  static const int LX[5] = { 19, 112, 206, 300, 394 };
  for (int k = 0; k < 5; k++) { char t[2] = { lab[k], 0 }; tiny(HY - 10, LX[k], t); }
}

static void apos(double lat, double lon, double off, double dec, double gha, double *hc, int *x, int *y) {
  double zn, sh = hczs(lat, lon, dec, gha, hc, &zn);
  *x = chart_x(zn, off, 375, 20);
  *y = chart_ys(sh, HYA, 1, HSA);
}

static void keep_rows(int lo, int hi) {              /* sc.pix = {p ... if lo < y < hi} */
  C47Screen *S = c47();
  for (int y = 0; y < SH; y++)
    if (!(y > lo && y < hi)) memset(S->b[y], 0, sizeof S->b[y]);
}

void view_anim(Al *al, int frames, double step, int upto) {
  metrics();
  double lat = al->lat, lon = al->lon, off = lat < 0 ? 180 : 0;
  c47_clear();
  sky_chart(off, HYA, HSA);
  c47_pixel(-221, 0);
  double hc;
  int x, y;
  for (int g = 0; g < 359; g += 2) { apos(lat, lon, off, 0, g, &hc, &x, &y); c47_dot4(y, x); }
  if (frames < 1) frames = 1;
  if (upto >= frames) upto = frames - 1;
  for (int k = 0; k <= upto; k++) {
    double j = al->j + k * step / 24;
    double uth = pmod(j + 0.5, 1.0) * 24;
    double g, d, hs;
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
    keep_rows(15, 224);
    header(al, j, uth);
    const char *w = daylight(hs);
    c47_text(4, state_x(w), w, &F21);
    c47_glyph(&SYMS, '@', ys - 3, xs - 3);
    c47_glyph(&SYMS, '(', ym - 3, xm - 3);
  }
}

void view_allsky(Al *al) {
  metrics();
  c47_clear();
  double off = al->lat < 0 ? 180 : 0;
  sky_chart(off, HYA, HSA);
  const Sun *s = &al->sun;
  header(al, al->j, ut_hours(al->j));
  double hc;
  int x, y;
  for (int g = 0; g < 358; g += 3) { apos(al->lat, al->lon, off, 0, g, &hc, &x, &y); c47_dot4(y, x); }
  double ty = (al->j - 2451545.0) / 365.25 / 3600.0;
  for (int n = 1; n <= 58; n++) {
    double ra = ST[n - 1][0], dec = ST[n - 1][1];
    double dec2 = dcos(ra) * 20.0431 * ty + dec;
    double ra2 = (dsin(ra) * dtan(dec) * 20.0431 + 46.1244) * ty + ra;
    apos(al->lat, al->lon, off, dec2, pmod(s->aries - ra2, 360.0), &hc, &x, &y);
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
  const char *w = daylight(hs);
  c47_text(4, state_x(w), w, &F21);
}
