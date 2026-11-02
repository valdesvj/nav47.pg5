/* astro.c - NAV47 astronomy: the C twin of Almanac 47's python/native/c47astro.py, step for step.
 * Same methods, constants and series (src/almdata.c is c47data.py bit for bit):
 *   sun_at()      SUNA  VSOP87D Earth (truncated), FK5, IAU1980 nutation, aberration
 *   star_pos()    STAR  IAU2006 precession, proper motion, nutation, aberration
 *   moon_pos()    MOON  Meeus ch. 47 (60+60 terms) + 55 correction terms fitted to JPL DE421
 *   planet_pos()  PLAN  VSOP87D truncated, light time (2 passes), FK5, aberration, nutation
 *   sun_event()   SUNRISE  rise/set, twilights, meridian passage
 *   moon_phase()  PHAS
 *   hcz()         CHZ
 * The order of every floating-point operation follows the Python, so the two agree to the last bits
 * (tests/test_astro.py checks it).
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#include <math.h>
#include "astro.h"
#include "almdata.h"

#define PI_D 3.141592653589793238462643383279502884
static const double DEG2RAD = PI_D / 180.0;     /* CPython math.radians / degrees */
static const double RAD2DEG = 180.0 / PI_D;
#define R2D 57.295779513082320876798154814105

/* Python float % : the result has the sign of the divisor */
double pmod(double x, double y) {
  double r = fmod(x, y);
  if (r != 0.0) { if ((r < 0) != (y < 0)) r += y; }
  else r = copysign(0.0, y);
  return r;
}
double dsin(double x) { return sin(pmod(x, 360.0) * DEG2RAD); }
double dcos(double x) { return cos(pmod(x, 360.0) * DEG2RAD); }
double dtan(double x) { return tan(pmod(x, 360.0) * DEG2RAD); }
double dasin(double x) { return asin(x) * RAD2DEG; }
double datan2(double y, double x) { return atan2(y, x) * RAD2DEG; }
static double ddeg(double r) { return r * RAD2DEG; }

/* ---- time ---- */
static double pyfloordiv(double a, double b) { return floor(a / b); }

double jd_ymd(int y, int m, int d, double h) {
  if (m <= 2) { y -= 1; m += 12; }
  int a = (int)pyfloordiv(y, 100);
  int b = 2 - a + (int)pyfloordiv(a, 4);
  return (double)((long)(365.25 * (y + 4716)) + (long)(30.6001 * (m + 1)) + d + b) - 1524.5 + h / 24.0;
}

void jd_to_ymd(double jd, int *y, int *m, int *d, double *h) {
  double z = floor(jd + 0.5), f = jd + 0.5 - z;
  double a = z;
  if (z >= 2299161) { double al = floor((z - 1867216.25) / 36524.25); a = z + 1 + al - floor(al / 4); }
  double b = a + 1524, c = floor((b - 122.1) / 365.25), dd = floor(365.25 * c), e = floor((b - dd) / 30.6001);
  *d = (int)(b - dd - floor(30.6001 * e));
  *m = (int)(e < 14 ? e - 1 : e - 13);
  *y = (int)(*m > 2 ? c - 4716 : c - 4715);
  *h = f * 24.0;
}

double day0(double j) { return (double)(long)(j - 0.5) + 0.5; }

/* ---- SER: sum A cos(B + C tau) tau^k, x 1E-8; the terms of each power summed, then Horner ---- */
static double ser(const Series *S, double tau) {
  int kmax = 0;
  for (int i = 0; i < S->n; i++) if ((int)S->r[i][0] > kmax) kmax = (int)S->r[i][0];
  double s = 0.0;
  for (int k = kmax; k >= 0; k--) {
    double g = 0.0;
    for (int i = 0; i < S->n; i++)
      if ((int)S->r[i][0] == k) g += S->r[i][1] * cos(S->r[i][2] + S->r[i][3] * tau);
    s = s * tau + g;
  }
  return s * 1e-8;
}

/* ---- nutation (IAU1980, 10 terms) ---- */
static void nut(double T, double a[5], double *dp, double *de) {
  a[0] = 297.85036 + 445267.11148 * T;
  a[1] = 357.52772 + 35999.05034 * T;
  a[2] = 134.96298 + 477198.867398 * T;
  a[3] = 93.27191 + 483202.017538 * T;
  a[4] = 125.04452 - 1934.136261 * T;
  double p = 0.0, e = 0.0;
  for (int i = 0; i < NU_N; i++) {
    const double *r = NU[i];
    double x = r[0] * a[0] + r[1] * a[1] + r[2] * a[2] + r[3] * a[3] + r[4] * a[4];
    p += (r[5] + r[6] * T) * dsin(x);
    e += (r[7] + r[8] * T) * dcos(x);
  }
  *dp = p * 1e-4;
  *de = e * 1e-4;
}

/* ---- SUNA ---- */
void sun_at(Sun *s, double j) {
  s->jd = j;
  double T = (j + ASTRO_DT / 86400.0 - 2451545.0) / 36525.0;
  double tau = T / 10.0;
  double L = ser(&VL, tau), B = ser(&VB, tau), R = ser(&VR, tau);
  double lon = pmod(L * R2D + 180.0, 360.0);
  double lat = -(B * R2D);
  double lp = lon - T * 1.397;
  lat += (dcos(lp) - dsin(lp)) * 0.03916 / 3600.0;
  lon -= 0.09033 / 3600.0;
  nut(T, s->args, &s->dpsi, &s->deps);
  lon += s->dpsi / 3600.0;
  lon -= 20.4898 / R / 3600.0;
  double eps = (84381.448 - T * 46.815 + s->deps) / 3600.0;
  s->dec = dasin(dsin(lat) * dcos(eps) + dcos(lat) * dsin(eps) * dsin(lon));
  double ra = datan2(dsin(lon) * dcos(eps) - dtan(lat) * dsin(eps), dcos(lon));
  double d = j - 2451545.0;
  double aries = pmod(d * 360.98564736629 + 280.46061837 + pow(d / 36525.0, 2) * 0.000387933
                      + s->dpsi / 3600.0 * dcos(eps), 360.0);
  s->T = T; s->R = R; s->lam = lon; s->eps = eps; s->aries = aries;
  s->gha = pmod(aries - ra, 360.0);
  s->sd = 15.99383 / R;
}

/* ---- STAR / STR2 ---- */
void star_pos(const Sun *s, int n, double *gha, double *dec, double *sha) {
  double T = s->T;
  const double *st = ST[n - 1];
  double a0 = st[0], d0 = st[1], pa = st[2], pd = st[3];
  double t = T * 100.0;
  double a = a0 + pa * t / 3600000.0 / dcos(d0);
  double d = d0 + pd * t / 3600000.0;
  double ze = (((T * 0.01801828 + 0.2988499) * T + 2306.083227) * T + 2.650545) / 3600.0;
  double z = (((T * 0.01826837 + 1.0927348) * T + 2306.077181) * T - 2.650545) / 3600.0;
  double th = (((T * -0.04182264 - 0.4294934) * T + 2004.191903) * T) / 3600.0;
  double az = a + ze;
  double dm = dasin(dsin(th) * dcos(d) * dcos(az) + dcos(th) * dsin(d));
  double am = datan2(dcos(d) * dsin(az), dcos(th) * dcos(d) * dcos(az) - dsin(th) * dsin(d)) + z;
  double e0 = s->eps - s->deps / 3600.0;
  double be = dasin(dsin(dm) * dcos(e0) - dcos(dm) * dsin(e0) * dsin(am));
  double la = datan2(dsin(am) * dcos(e0) + dtan(dm) * dsin(e0), dcos(am));
  double ek = T * -0.000042037 + 0.016708634;
  double pi = T * 1.71946 + 102.93735;
  double dbe = (dsin(s->lam - la) - dsin(pi - la) * ek) * dsin(be) * -20.49552 / 3600.0;
  la += ((dcos(s->lam - la) * -20.49552 + dcos(pi - la) * ek * 20.49552) / dcos(be) + s->dpsi) / 3600.0;
  be += dbe;
  double de = dasin(dsin(be) * dcos(s->eps) + dcos(be) * dsin(s->eps) * dsin(la));
  double ra = pmod(datan2(dsin(la) * dcos(s->eps) - dtan(be) * dsin(s->eps), dcos(la)), 360.0);
  double sh = pmod(360.0 - ra, 360.0);
  *gha = pmod(sh + s->aries, 360.0);
  *dec = de;
  if (sha) *sha = sh;
}

static double horner(const double *c, int n, double T) {
  double v = c[n - 1];
  for (int k = n - 2; k >= 0; k--) v = v * T + c[k];
  return v;
}

/* ---- MOON / MOO2 ---- */
void moon_pos(const Sun *s, double *gha, double *dec, double *hp, double *sd) {
  static const double cLp[5] = { 218.3164477, 481267.8812, -0.0015786, 1.855835024E-06, -1.533883486E-08 };
  static const double cD[5]  = { 297.8501921, 445267.1114, -0.0018819, 1.831944719E-06, -8.844469995E-09 };
  static const double cM[4]  = { 357.5291092, 35999.05029, -0.0001536, 4.083299306E-08 };
  static const double cMp[5] = { 134.9633964, 477198.8675, 0.0087414, 1.434740814E-05, -6.797172376E-08 };
  static const double cF[5]  = { 93.272095, 483202.0175, -0.0036539, -2.836074872E-07, 1.158332465E-09 };
  static const double cE[3]  = { 1, -0.002516, -7.4E-06 };
  double T = s->T;
  double Lp = pmod(horner(cLp, 5, T), 360.0);
  double D  = pmod(horner(cD, 5, T), 360.0);
  double M  = pmod(horner(cM, 4, T), 360.0);
  double Mp = pmod(horner(cMp, 5, T), 360.0);
  double F  = pmod(horner(cF, 5, T), 360.0);
  double E  = horner(cE, 3, T);
  double acc6 = 0.0, acc7 = 0.0, acc8 = 0.0;
  struct { const double (*t)[6]; int n, rs, rc; } tabs[4] = {
    { ML, ML_N, 6, 7 }, { MB, MB_N, 8, 8 }, { MCL, MCL_N, 6, 6 }, { MCB, MCB_N, 8, 8 } };
  for (int k = 0; k < 4; k++) {
    for (int i = 0; i < tabs[k].n; i++) {
      const double *r = tabs[k].t[i];
      double d = r[0], m = r[1], mp = r[2], f = r[3], cs = r[4], cc = r[5];
      double arg = d * D + m * M + mp * Mp + f * F;
      double e = (m == (double)(long)m) ? pow(E, (double)(long)fabs(m)) : pow(E, fabs(m));
      double vs = cs * e * dsin(arg);
      if (tabs[k].rs == 6) acc6 += vs; else acc8 += vs;
      double vc = cc * e * dcos(arg);
      if (tabs[k].rc == 6) acc6 += vc; else if (tabs[k].rc == 7) acc7 += vc; else acc8 += vc;
    }
  }
  double A1 = T * 131.849 + 119.75;
  acc6 += (dsin(A1) * 3958 + dsin(Lp - F) * 1962 + dsin(T * 479264.29 + 53.09) * 318
           + (T * 367.1904613 + 56.41732779));
  acc8 += (dsin(Lp) * -2235 + dsin(T * 481266.484 + 313.45) * 382 + dsin(A1 - F) * 175
           + dsin(A1 + F) * 175 + dsin(Lp - Mp) * 127 + dsin(Lp + Mp) * -115
           + (T * 40.04748125 + -9.463218981));
  double lam = acc6 / 1e6 + Lp + s->dpsi / 3600.0;
  double bet = acc8 / 1e6;
  double dist = acc7 / 1000.0 + 385000.56;
  *dec = dasin(dsin(bet) * dcos(s->eps) + dcos(bet) * dsin(s->eps) * dsin(lam));
  double ra = datan2(dsin(lam) * dcos(s->eps) - dtan(bet) * dsin(s->eps), dcos(lam));
  *gha = pmod(s->aries - ra + 360.0, 360.0);
  if (hp) *hp = dasin(6378.14 / dist) * 60.0;
  if (sd) *sd = 358473400.0 / dist / 60.0;
}

/* ---- PLN3 quick: mean elements ---- */
static void helio(int b, double T, double *x, double *y, double *z) {
  double el[6];
  for (int i = 0; i < 6; i++) el[i] = KEPLER[b][0][i] + KEPLER[b][1][i] * T;
  double a = el[0], e = el[1], I = el[2], L = el[3], w = el[4], O = el[5];
  double M = L - w, E = M;
  for (int k = 0; k < 4; k++) E = dsin(E) * e * 57.29577951308232 + M;
  double xv = (dcos(E) - e) * a;
  double yv = sqrt(1 - e * e) * a * dsin(E);
  double rr = hypot(xv, yv);
  double u = datan2(yv, xv) + w - O;
  *z = dsin(u) * dsin(I) * rr;
  double si = dsin(u) * dcos(I);
  *x = (dcos(u) * dcos(O) - si * dsin(O)) * rr;
  *y = (dcos(u) * dsin(O) + si * dcos(O)) * rr;
}

void planet_quick(const Sun *s, int p, double *gha, double *dec, double *dist) {
  double T = s->T, xe, ye, ze, xp, yp, zp;
  helio(0, T, &xe, &ye, &ze);
  helio(p, T, &xp, &yp, &zp);
  double x = xp - xe, y = yp - ye, z = zp - ze;
  *dist = sqrt(x * x + y * y + z * z);
  double lam = datan2(y, x) + T * 1.3969713;
  double bet = datan2(z, hypot(x, y));
  *dec = dasin(dsin(bet) * dcos(23.4393) + dcos(bet) * dsin(23.4393) * dsin(lam));
  double ra = datan2(dsin(lam) * dcos(23.4393) - dtan(bet) * dsin(23.4393), dcos(lam));
  *gha = pmod(s->aries - ra, 360.0);
}

void moon_quick(const Sun *s, double *gha, double *dec) {
  static const double L[6][3] = { { 6.29, 135.0, 477198.87 }, { -1.27, 259.3, -413335.36 }, { 0.66, 235.7, 890534.22 },
                                  { 0.21, 269.9, 954397.74 }, { -0.19, 357.5, 35999.05 }, { -0.11, 186.5, 966404.03 } };
  static const double B[4][3] = { { 5.13, 93.3, 483202.02 }, { 0.28, 228.2, 960400.89 }, { -0.28, 318.3, 6003.15 },
                                  { -0.17, 217.6, -407332.21 } };
  double T = s->T;
  double lam = T * 481267.881 + 218.32;
  for (int i = 0; i < 6; i++) lam += dsin(T * L[i][2] + L[i][1]) * L[i][0];
  double bet = 0.0;
  for (int i = 0; i < 4; i++) bet += dsin(T * B[i][2] + B[i][1]) * B[i][0];
  *dec = dasin(dsin(bet) * dcos(23.4393) + dcos(bet) * dsin(23.4393) * dsin(lam));
  double ra = datan2(dsin(lam) * dcos(23.4393) - dtan(bet) * dsin(23.4393), dcos(lam));
  *gha = pmod(s->aries - ra, 360.0);
}

/* ---- PLAN / PLN2 ---- */
void planet_pos(const Sun *s, int p, double lt, double *gha, double *dec, double *sha, double *hp) {
  static const Series *const PS[5][3] = { { 0, 0, 0 }, { &VNL, &VNB, &VNR }, { &MAL, &MAB, &MAR },
                                          { &JUL, &JUB, &JUR }, { &SAL, &SAB, &SAR } };
  double T = s->T, tau = T / 10.0;
  double L0 = ser(&EEL, tau), B0 = ser(&EEB, tau), R0 = ser(&EER, tau);
  int passes = lt > 0 ? 1 : 2;
  if (lt <= 0) lt = 0.0;
  double x = 0, y = 0, z = 0, dist = 0;
  for (int k = 0; k < passes; k++) {
    double tp = tau - lt / 365250.0;
    double L = ser(PS[p][0], tp), B = ser(PS[p][1], tp), R = ser(PS[p][2], tp);
    x = R * cos(B) * cos(L) - R0 * cos(B0) * cos(L0);
    y = R * cos(B) * sin(L) - R0 * cos(B0) * sin(L0);
    z = R * sin(B) - R0 * sin(B0);
    dist = pow(x * x + y * y + z * z, 0.5);
    lt = dist * 0.0057755183;
  }
  double rho = hypot(x, y);
  double lam = pmod(datan2(y, x) + 360.0, 360.0);
  double bet = datan2(z, rho);
  double lp = lam - T * 1.397;                                       /* FK5 */
  lam += ((dcos(lp) + dsin(lp)) * dtan(bet) * 0.03916 - 0.09033) / 3600.0;
  bet += (dcos(lp) - dsin(lp)) * 0.03916 / 3600.0;
  double sun = L0 * 57.29577951308232 + 180.0;                       /* aberration */
  double e = T * -0.000042037 + 0.016708634;
  double pi = T * 1.71946 + 102.93735;
  lam += ((dcos(sun - lam) * -20.49552 + dcos(pi - lam) * e * 20.49552) / dcos(bet) + s->dpsi) / 3600.0;
  bet += (dsin(sun - lam) - dsin(pi - lam) * e) * dsin(bet) * -20.49552 / 3600.0;
  *dec = dasin(dsin(bet) * dcos(s->eps) + dcos(bet) * dsin(s->eps) * dsin(lam));
  double ra = pmod(datan2(dsin(lam) * dcos(s->eps) - dtan(bet) * dsin(s->eps), dcos(lam)) + 360.0, 360.0);
  *gha = pmod(s->aries - ra + 360.0, 360.0);
  if (sha) *sha = pmod(360.0 - ra, 360.0);
  if (hp) *hp = 8.794 / dist / 60.0;
}

/* ---- CHZ ---- */
void hcz(double lat, double lon, double dec, double gha, double *hc, double *zn) {
  double lha = gha + lon;
  *hc = dasin(dsin(lat) * dsin(dec) + dcos(lat) * dcos(dec) * dcos(lha));
  *zn = pmod(datan2(-(dcos(dec) * dsin(lha)), dcos(lat) * dsin(dec) - dsin(lat) * dcos(dec) * dcos(lha)) + 360.0, 360.0);
}

double hczs(double lat, double lon, double dec, double gha, double *hc, double *zn) {
  double lha = gha + lon;
  double sh = dsin(lat) * dsin(dec) + dcos(lat) * dcos(dec) * dcos(lha);
  sh = sh < -1.0 ? -1.0 : sh > 1.0 ? 1.0 : sh;
  *zn = pmod(datan2(-(dcos(dec) * dsin(lha)), dcos(lat) * dsin(dec) - dsin(lat) * dcos(dec) * dcos(lha)) + 360.0, 360.0);
  *hc = dasin(sh);
  return sh;
}

/* ---- SUNF + SUNRISE ---- */
void sun_fast(double j, double *gha, double *dec) {
  double d = j - 2451545.0;
  double g = d * 0.9856003 + 357.528;
  double lam = dsin(g) * 1.915 + dsin(g * 2) * 0.02 + d * 0.9856474 + 280.46;
  double eps = d * -0.0000004 + 23.439;
  *dec = dasin(dsin(eps) * dsin(lam));
  double ra = datan2(dcos(eps) * dsin(lam), dcos(lam));
  *gha = pmod(d * 360.98564736629 + 280.46061837 - ra, 360.0);
}

double sun_event(double j0, double lat, double lon, int kind) {
  static const double H0[EV_COUNT] = { -0.8333333333333333, -0.8333333333333333, -12.0, -12.0, -6.0, -6.0, 0.0 };
  static const int SG[EV_COUNT] = { -1, 1, -1, 1, -1, 1, 0 };
  double h0 = H0[kind];
  int sg = SG[kind];
  double t = sg * 6 + 12.0;
  for (int i = 0; i < 10; i++) {
    double gha, dec;
    sun_fast(t / 24.0 + j0, &gha, &dec);
    double target = 0.0;
    if (sg) {
      double c = (dsin(h0) - dsin(lat) * dsin(dec)) / (dcos(lat) * dcos(dec));
      if (fabs(c) > 1) return 99.0;
      target = ddeg(acos(c)) * sg;
    }
    double dt = (pmod(target - gha - lon + 540.0, 360.0) - 180.0) / 15.0;
    t += dt;
    if (fabs(dt) <= 0.0003) break;
  }
  return t;
}

/* ---- PHAS ---- */
static double age_from(double M, double Mp, double F, double dl) {
  double mn = M - 0.98560028 * dl, mpn = Mp - 13.064993 * dl, fn = F - 13.2293505 * dl;
  double c = (-0.4072 * dsin(mpn) + 0.17241 * dsin(mn) + 0.01608 * dsin(2 * mpn)
              + 0.01039 * dsin(2 * fn) + 0.00739 * dsin(mpn - mn) - 0.00514 * dsin(mpn + mn)
              + 0.00208 * dsin(2 * mn) - 0.00111 * dsin(mpn - 2 * fn)
              - 0.00057 * dsin(mpn + 2 * fn) + 0.00056 * dsin(2 * mpn + mn));
  return dl - c;
}

void moon_phase(const Sun *s, double *illum, double *age) {
  double D = s->args[0], M = s->args[1], Mp = s->args[2], F = s->args[3];
  double i = (180 - D - 6.289 * dsin(Mp) + 2.1 * dsin(M) - 1.274 * dsin(2 * D - Mp)
              - 0.658 * dsin(2 * D) - 0.214 * dsin(2 * Mp) - 0.11 * dsin(D));
  *illum = (1 + dcos(i)) * 50;
  double syn = 29.530589;
  double dl = pmod(D, 360.0) / 12.190749;
  double a = age_from(M, Mp, F, dl);
  if (a < 0) a = age_from(M, Mp, F, dl + syn);
  else if (a > 27) {
    double a2 = age_from(M, Mp, F, dl - syn);
    if (a2 >= 0) a = a2;
  }
  *age = a;
}
