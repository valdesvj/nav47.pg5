/* compass.c - NAV47: the gyro error and the magnetic compass's deviation from a body's true azimuth; the deviation
 * card fitted with the five classic coefficients (A + B sin h + C cos h + D sin 2h + E cos 2h, h the compass heading)
 * and the advice to compensate when it is over DEV_LIMIT. The calculator's COMPASS / CARD pages and the web's Compass
 * tab use this same code. Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "compass.h"

#define D2R (3.14159265358979323846 / 180)
static double wrap180(double a) { a = fmod(a + 540.0, 360.0); if (a < 0) a += 360; return a - 180.0; }
static double wrap360(double a) { a = fmod(a, 360.0); return a < 0 ? a + 360 : a; }

void cmp_errors(double zn, double gyro_brg, double gyro_hdg, double compass_hdg, double var, CmpOut *o) {
  o->gyro_err = (isnan(zn) || isnan(gyro_brg)) ? NAN : wrap180(zn - gyro_brg);
  o->true_hdg = (isnan(gyro_hdg) || isnan(o->gyro_err)) ? NAN : wrap360(gyro_hdg + o->gyro_err);
  o->comp_err = (isnan(o->true_hdg) || isnan(compass_hdg)) ? NAN : wrap180(o->true_hdg - compass_hdg);
  o->dev = (isnan(o->comp_err) || isnan(var)) ? NAN : wrap180(o->comp_err - var);
}

/* solve the 5 x 5 normal equations (Gauss, partial pivoting); 0 singular */
static int solve5(double M[5][6], double x[5]) {
  for (int c = 0; c < 5; c++) {
    int p = c;
    for (int r = c + 1; r < 5; r++) if (fabs(M[r][c]) > fabs(M[p][c])) p = r;
    if (fabs(M[p][c]) < 1e-9) return 0;
    if (p != c) for (int k = 0; k < 6; k++) { double t = M[c][k]; M[c][k] = M[p][k]; M[p][k] = t; }
    for (int r = 0; r < 5; r++) {
      if (r == c) continue;
      double f = M[r][c] / M[c][c];
      for (int k = c; k < 6; k++) M[r][k] -= f * M[c][k];
    }
  }
  for (int c = 0; c < 5; c++) x[c] = M[c][5] / M[c][c];
  return 1;
}
static double curve(const double c[5], double h) {
  double r = h * D2R;
  return c[0] + c[1] * sin(r) + c[2] * cos(r) + c[3] * sin(2 * r) + c[4] * cos(2 * r);
}
/* linear between the card's neighbours round the circle */
static double interp(int n, const double *hdg, const double *dev, double h) {
  if (n <= 0) return NAN;
  if (n == 1) return dev[0];
  int lo = -1, hi = -1; double dlo = 1e9, dhi = 1e9;
  for (int i = 0; i < n; i++) {
    double d = wrap360(h - hdg[i]);                    /* how far behind h */
    if (d < dlo) { dlo = d; lo = i; }
    double u = wrap360(hdg[i] - h);                    /* how far ahead */
    if (u < dhi) { dhi = u; hi = i; }
  }
  if (dlo + dhi < 1e-9) return dev[lo];
  return dev[lo] + (dev[hi] - dev[lo]) * dlo / (dlo + dhi);
}

void dev_fit(int n, const double *hdg, const double *dev, DevFit *f) {
  memset(f, 0, sizeof *f);
  f->fitted = 0;
  if (n <= 0) return;
  /* five or more headings, spread over three quadrants at least: the coefficients */
  int q[4] = { 0 }, nq = 0;
  for (int i = 0; i < n; i++) q[(int)(wrap360(hdg[i]) / 90) & 3] = 1;
  for (int k = 0; k < 4; k++) nq += q[k];
  if (n >= 5 && nq >= 3) {
    double M[5][6] = { { 0 } };
    for (int i = 0; i < n; i++) {
      double r = hdg[i] * D2R, b[5] = { 1, sin(r), cos(r), sin(2 * r), cos(2 * r) };
      for (int a = 0; a < 5; a++) { for (int c = 0; c < 5; c++) M[a][c] += b[a] * b[c]; M[a][5] += b[a] * dev[i]; }
    }
    if (solve5(M, f->c)) {
      f->fitted = 1;
      double s = 0;
      for (int i = 0; i < n; i++) { double e = dev[i] - curve(f->c, hdg[i]); s += e * e; }
      f->rms = sqrt(s / n);
    }
  }
  /* the largest over the circle, every degree (the card's own points with it) */
  f->max = 0; f->at = 0;
  for (int h = 0; h < 360; h++) {
    double d = f->fitted ? curve(f->c, h) : interp(n, hdg, dev, h);
    if (fabs(d) > fabs(f->max)) { f->max = d; f->at = h; }
  }
  for (int i = 0; i < n; i++) if (fabs(dev[i]) > fabs(f->max)) { f->max = dev[i]; f->at = wrap360(hdg[i]); }
}

double dev_at(int n, const double *hdg, const double *dev, const DevFit *f, double h) {
  if (n <= 0) return NAN;
  return f && f->fitted ? curve(f->c, h) : interp(n, hdg, dev, h);
}

void dev_advice(const DevFit *f, char *out, int len) {
  out[0] = 0;
  if (fabs(f->max) <= DEV_LIMIT) return;
  int o = snprintf(out, (size_t)len, "DEVIATION %.1f %c at %03.0f: over %.0f, adjust the compass\n",
                   fabs(f->max), f->max >= 0 ? 'E' : 'W', f->at, DEV_LIMIT);
  if (f->fitted && o < len) {
    static const char *const FIX[5] = {                /* each coefficient, its corrector (short: the calculator's line) */
      "lubber line / binnacle alignment (no magnet)",
      "fore-and-aft magnets, Flinders bar (E/W)",
      "athwartship magnets (N/S)",
      "quadrantal spheres in / out (NE SE SW NW)",
      "spheres slewed (N E S W)" };
    int ord[5] = { 0, 1, 2, 3, 4 };                    /* the largest coefficients first */
    for (int i = 0; i < 5; i++) for (int j = i + 1; j < 5; j++) if (fabs(f->c[ord[j]]) > fabs(f->c[ord[i]])) { int t = ord[i]; ord[i] = ord[j]; ord[j] = t; }
    for (int k = 0; k < 5 && o < len; k++) {
      int i = ord[k];
      if (fabs(f->c[i]) < 1.0) break;                  /* under a degree: not worth a corrector */
      o += snprintf(out + o, (size_t)(len - o), "%c %+.1f: %s\n", "ABCDE"[i], f->c[i], FIX[i]);
    }
  }
  if (o < len) snprintf(out + o, (size_t)(len - o), "by a compass adjuster; swing her, a new card");
}
