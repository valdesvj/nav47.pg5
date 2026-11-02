/* c47scr.c - NAV47: the C47 screen of Almanac 47 (python/native/c47screen.py Screen), line for line.
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "c47scr.h"

#ifndef NAV47_NO_LCD
#include <dmcp.h>
#endif

static C47Screen *S;

int c47_init(void) {
  if (!S) S = (C47Screen *)calloc(1, sizeof(C47Screen));
  return S != NULL;
}
C47Screen *c47(void) { return S; }
void c47_clear(void) { memset(S, 0, sizeof *S); }

static long ipd(double x) { return (long)x; }                     /* C47 IP: truncate toward zero */
static long pym(long a, long b) { long r = a % b; return r < 0 ? r + b : r; }

static void set(int x, int y) {
  if (x < 0 || y < 0 || x >= SW || y >= SH) return;
  S->b[y][x >> 3] |= (unsigned char)(0x80 >> (x & 7));
}
static void flip(int x, int y) {
  if (x < 0 || y < 0 || x >= SW || y >= SH) return;
  S->b[y][x >> 3] ^= (unsigned char)(0x80 >> (x & 7));
}
int c47_get(int x, int y) {
  if (x < 0 || y < 0 || x >= SW || y >= SH) return 0;
  return (S->b[y][x >> 3] >> (7 - (x & 7))) & 1;
}

void c47_pixel(int y, int x) {
  if (x >= 0 && y >= 0) set(x, y);
  if (x < 0) for (int yy = 0; yy < 240; yy++) set(-x, yy);
  if (y < 0) for (int xx = 0; xx < 400; xx++) set(xx, -y);
}

void c47_dot4(int y, int x) {
  for (int dy = 0; dy < 2; dy++)
    for (int dx = 0; dx < 2; dx++) c47_pixel(y + dy, x + dx);
}

int c47_hline(int y, int x, int n) {
  for (int i = 0; i < n; i++) set(x + i, y);
  return x + n;
}

void c47_xor_box(int y, int x, int w, int h) {
  for (int i = 0; i < w; i++)
    for (int j = 0; j < h; j++) flip(x + i, y + j);
}

int c47_glyph(const C47Font *f, int ch, int y, int x) {
  const C47Glyph *g = &f->g[ch & 0x7f];
  for (int k = 0; k < g->n; k++) {
    const C47Col *c = &f->c[g->ofs + k];
    unsigned int m = c->m;
    for (int i = 0; m; i++, m >>= 1)
      if (m & 1) set(x + c->x, y + g->yoff + i);
  }
  return x + g->adv;
}

int c47_text(int y, int x, const char *s, const C47Font *f) {
  for (; *s; s++) x = c47_glyph(f, (unsigned char)*s, y, x);
  return x;
}

int c47_width(const char *s, const C47Font *f) {
  int w = 0;
  for (; *s; s++) w += f->g[(unsigned char)*s & 0x7f].adv;
  return w;
}

static int digits(int y, int x, long n, const C47Font *f) {
  if (n > 99) x = c47_glyph(f, (int)(48 + ipd(n / 100.0)), y, x);
  if (n > 9) x = c47_glyph(f, (int)(48 + pym(ipd(n / 10.0), 10)), y, x);
  return c47_glyph(f, (int)(48 + pym(n, 10)), y, x);
}

int c47_pinb(int y, int x, double v) { return digits(y, x, ipd(fabs(v) + 0.5), &F21); }
int c47_tiny_num(int y, int x, double v) { return digits(y, x, ipd(fabs(v) + 0.5), &TINY21); }

int c47_pf1(int y, int x, double v) {
  long m = ipd(fabs(v) * 10 + 0.5);
  x = digits(y, x, ipd(m / 10.0), &F21);
  x = c47_glyph(&F21, '.', y, x);
  return c47_glyph(&F21, (int)(48 + pym(m, 10)), y, x);
}

int c47_phm(int y, int x, double h) {
  if (h > 98) return c47_text(y, x, "--:--", &F21);
  long m = ipd(h * 60 + 0.5);
  long hh = pym(ipd(m / 60.0), 24), mm = pym(m, 60);
  char s[8] = { (char)('0' + ipd(hh / 10.0)), (char)('0' + hh % 10), ':', (char)('0' + ipd(mm / 10.0)), (char)('0' + mm % 10), 0 };
  return c47_text(y, x, s, &F21);
}

int c47_pdm(int y, int x, double v) {
  long t = ipd(fabs(v) * 600 + 0.5);
  long d = ipd(t / 600.0);
  t -= d * 600;
  int w = F21.g['0'].adv;
  if (!(d > 99)) x += w;
  if (!(d > 9)) x += w;
  x = c47_glyph(&F21, v < 0 ? '-' : ' ', y, x);
  x = digits(y, x, d, &F21);
  x = c47_glyph(&F21, ' ', y, x);
  char s[8] = { (char)('0' + ipd(t / 100.0)), (char)('0' + pym(ipd(t / 10.0), 10)), '.', (char)('0' + t % 10), 0 };
  return c47_text(y, x, s, &F21);
}

int c47_pzn(int y, int x, double v) {
  long t = ipd(v * 10 + 0.5);
  if (3600 <= t) t = 0;
  char s[8] = { (char)('0' + ipd(t / 1000.0)), (char)('0' + pym(ipd(t / 100.0), 10)), (char)('0' + pym(ipd(t / 10.0), 10)), '.',
                (char)('0' + pym(t, 10)), 0 };
  return c47_text(y, x, s, &F21);
}

/* jd_to_date of c47screen.py (C47 IP arithmetic) */
static void jd_to_date47(double j, long *day, long *mon, long *yr) {
  long z = ipd(j + 0.5);
  long al = ipd((z - 1867216.25) / 36524.25);
  long b = z + 1 + al - ipd(al / 4.0) + 1524;
  long c = ipd((b - 122.1) / 365.25);
  b -= ipd(c * 365.25);
  long e = ipd(b / 30.6001);
  *day = b - ipd(e * 30.6001);
  *mon = e - 1;
  if (12 < *mon) *mon -= 12;
  *yr = c - 4716;
  if (2 >= *mon) *yr += 1;
}

int c47_pdat(int y, int x, double jd) {
  long d, m, yr;
  jd_to_date47(jd, &d, &m, &yr);
  char s[16];
  long hi = ipd(yr / 100.0), lo = pym(yr, 100);
  s[0] = (char)('0' + d / 10); s[1] = (char)('0' + d % 10); s[2] = '-';
  s[3] = (char)('0' + m / 10); s[4] = (char)('0' + m % 10); s[5] = '-';
  s[6] = (char)('0' + hi / 10); s[7] = (char)('0' + hi % 10); s[8] = (char)('0' + lo / 10); s[9] = (char)('0' + lo % 10); s[10] = 0;
  return c47_text(y, x, s, &F21);
}

void c47_xor_text_from(const C47Screen *o) {
  for (int y = 0; y < SH; y++)
    for (int i = 0; i < SW / 8; i++) S->b[y][i] ^= o->b[y][i];
}

#ifndef NAV47_NO_LCD
void c47_show(void) {
  for (int y = 0; y < SH; y++) {
    int row = SH - 1 - y, x = 0;
    while (x < SW) {
      if (!c47_get(x, y)) { x++; continue; }
      int x0 = x;
      while (x < SW && c47_get(x, y)) x++;
      lcd_fill_rect((uint32_t)x0, (uint32_t)row, (uint32_t)(x - x0), 1, 1);
    }
  }
}
#endif
