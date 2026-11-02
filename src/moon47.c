/* moon47.c - NAV47: MOON47, the C twin of Almanac 47's python/moon47.py (the formulas of every calculator version):
 * 20 terms of Meeus ch. 47, the elongation, the phases by the secant method, age, % lit, HP, SD, the disc.
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#include <math.h>
#include "moon47.h"

#define PI_D 3.141592653589793238462643383279502884
static const double DEG2RAD = PI_D / 180.0, RAD2DEG = 180.0 / PI_D;
#define RATE 12.190749
#define DT (69.0 / 86400.0)

const char *const M47_NAMES[8] = { "NEW MOON", "WAXING CRESCENT", "FIRST QUARTER", "WAXING GIBBOUS", "FULL MOON",
                                   "WANING GIBBOUS", "LAST QUARTER", "WANING CRESCENT" };
const char *const M47_PHASES[4] = { "NEW MOON", "FIRST QUARTER", "FULL MOON", "LAST QUARTER" };

static const double TERMS[20][6] = {
  { 0, 0, 1, 0, 6288774, -20905355 }, { 2, 0, -1, 0, 1274027, -3699111 }, { 2, 0, 0, 0, 658314, -2955968 },
  { 0, 0, 2, 0, 213618, -569925 }, { 0, 1, 0, 0, -185116, 48888 }, { 0, 0, 0, 2, -114332, 0 },
  { 2, 0, -2, 0, 58793, 246158 }, { 2, -1, -1, 0, 57066, -152138 }, { 2, 0, 1, 0, 53322, -170733 },
  { 2, -1, 0, 0, 45758, -204586 }, { 0, 1, -1, 0, -40923, -129620 }, { 1, 0, 0, 0, -34720, 108743 },
  { 0, 1, 1, 0, -30383, 104755 }, { 2, 0, 0, -2, 15327, 0 }, { 0, 0, 1, 2, -12528, 0 },
  { 0, 0, 1, -2, 10980, 79661 }, { 4, 0, -1, 0, 10675, 0 }, { 0, 0, 3, 0, 10034, 0 },
  { 4, 0, -2, 0, 8548, 0 }, { 2, 1, -1, 0, -7888, 0 },
};

static double sind(double x) { return sin(x * DEG2RAD); }
static double cosd(double x) { return cos(x * DEG2RAD); }
static double pmod(double x, double y) {
  double r = fmod(x, y);
  if (r != 0.0) { if ((r < 0) != (y < 0)) r += y; }
  else r = copysign(0.0, y);
  return r;
}

static void moon(double j, double *e, double *dist, double *beta, double *Mo) {
  double T = (j + DT - 2451545.0) / 36525.0;
  double D = 297.8501921 + 445267.1114 * T, M = 357.5291092 + 35999.05029 * T;
  double Mp = 134.9633964 + 477198.8675 * T, F = 93.272095 + 483202.0175 * T, E = 1.0 - 0.002516 * T;
  double lon = 0.0, ds = 0.0;
  for (int i = 0; i < 20; i++) {
    const double *t = TERMS[i];
    double a = t[0] * D + t[1] * M + t[2] * Mp + t[3] * F;
    double ee = t[1] != 0 ? E : 1.0;
    lon += t[4] * ee * sind(a);
    ds += t[5] * ee * cosd(a);
  }
  double c = (1.914602 - 0.004817 * T) * sind(M) + 0.019993 * sind(2 * M) + 0.000289 * sind(3 * M);
  *e = pmod(D + lon / 1e6 - c + 0.00569, 360.0);
  *dist = 385000.56 + ds / 1000.0;
  *beta = 5.128189 * sind(F);
  *Mo = M;
}

static double elong(double j) { double e, d, b, m; moon(j, &e, &d, &b, &m); return e; }

static double phase_time(double j, double target) {
  double d = pmod(target - elong(j), 360.0);
  double t0 = j, f0 = -d, t1 = j + d / RATE;
  for (int i = 0; i < 3; i++) {
    double f1 = pmod(elong(t1) - target + 180.0, 360.0) - 180.0;
    if (f1 == f0) break;
    double t2 = t1 - f1 * (t1 - t0) / (f1 - f0);
    t0 = t1; t1 = t2; f0 = f1;
  }
  return t1;
}

void moon47_page(double j, int south, Moon47 *p) {
  double e, dist, beta, M;
  moon(j, &e, &dist, &beta, &M);
  double psi = acos(cosd(beta) * cosd(e)) * RAD2DEG;
  double r = 149597870.7 * (1.000140 - 0.016708 * cosd(M) - 0.000141 * cosd(2 * M));
  double i = atan2(r * sind(psi), dist - r * cosd(psi)) * RAD2DEG;
  double nw = phase_time(j - e / RATE - 1.0, 0.0);
  if (nw > j) nw = phase_time(nw - M47_LUN - 1.0, 0.0);
  p->jd = j;
  p->age = j - nw;
  p->lit = (1.0 + cosd(i)) * 50.0;
  p->hp = asin(6378.14 / dist) * RAD2DEG * 60.0;
  p->sd = 358473400.0 / dist / 60.0;
  p->index = (int)(p->age / M47_LUN * 8 + 0.5) % 8;
  p->south = south;
  p->elong = e;
  for (int k = 0; k < 4; k++) {
    double t = phase_time(j, 90.0 * k);
    if (t < j) t = phase_time(t + M47_LUN - 2.0, 90.0 * k);
    p->next_t[k] = t;
    p->next_k[k] = k;
  }
  for (int a = 0; a < 4; a++)                 /* sort by time */
    for (int b = a + 1; b < 4; b++)
      if (p->next_t[b] < p->next_t[a]) {
        double t = p->next_t[a]; p->next_t[a] = p->next_t[b]; p->next_t[b] = t;
        int k = p->next_k[a]; p->next_k[a] = p->next_k[b]; p->next_k[b] = k;
      }
}

int moon47_disc_runs(int r, double age, int south, int dx, int runs[2][2]) {
  double ct = cosd(age / M47_LUN * 360.0);
  if (ct == 0.0) ct = 1e-30;
  int v = (south ? -dx : dx) * (age <= M47_LUN / 2 ? 1 : -1);
  int ymax = (int)floor(sqrt((r + 0.3) * (r + 0.3) - dx * dx));
  double qi = (r - 1.2) * (r - 1.2) - dx * dx;
  int n = 0, a;
  a = qi < 0 ? 0 : (int)floor(sqrt(qi)) + 1;
  if (a <= ymax) { runs[n][0] = a; runs[n][1] = ymax; n++; }
  double q = (double)r * r - (v / ct) * (v / ct);
  int ra = 1, rb = 0;
  if (ct >= 0) {
    if (v > 0) { ra = q < 0 ? 0 : (int)floor(sqrt(q)) + 1; rb = ymax; }
  } else if (v >= 0) { ra = 0; rb = ymax; }
  else if (q > 0) { ra = 0; rb = (int)floor(sqrt(q)); if (rb > ymax) rb = ymax; }
  if (ra <= rb) { runs[n][0] = ra; runs[n][1] = rb; n++; }
  return n;
}
