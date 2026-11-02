/* tide.c - NAV47 tides, tidal streams and FLOW: the C twin of Tide 47's python/tide47ref.py, step for step.
 *
 *   h(t) = Z0 + sum_k f_k H_k cos(V_k(t) + u_k - g_k)     (Schureman, SP 98; NOAA's and the Admiralty's method)
 *
 * tide_day() is engine(): one exact evaluation an hour (value, first and second derivative), the events from the
 * Hermite cubic of the event function between the hours plus one exact Newton step, the curve every 20 minutes from
 * the hourly values and slopes. Streams (rectilinear / rotary) and the FLOW atlas as Tide 47 (FLOW47, ARTF47).
 * test/test_tide.py checks it against tide47ref.py.
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "tide.h"

#define PI_D 3.141592653589793238462643383279502884
static const double D2R = PI_D / 180.0;      /* Python: math.pi / 180 */
static const double RAD2DEG = 180.0 / PI_D;  /* math.degrees */

static double pmod(double x, double y) {
  double r = fmod(x, y);
  if (r != 0.0) { if ((r < 0) != (y < 0)) r += y; }
  else r = copysign(0.0, y);
  return r;
}

/* ---- calendar (JDN) ---- */
long jdn(int y, int m, int d) {
  long a = (14 - m) / 12, yy = y + 4800 - a, mm = m + 12 * a - 3;
  return d + (153 * mm + 2) / 5 + 365 * yy + yy / 4 - yy / 100 + yy / 400 - 32045;
}

void cal(long n, int *y, int *m, int *d) {
  long a = n + 32044, b = (4 * a + 3) / 146097, c = a - 146097 * b / 4;
  long dd = (4 * c + 3) / 1461, e = c - 1461 * dd / 4, mm = (5 * e + 2) / 153;
  *y = (int)(100 * b + dd - 4800 + mm / 10);
  *m = (int)(mm + 3 - 12 * (mm / 10));
  *d = (int)(e - (153 * mm + 2) / 5 + 1);
}

int tide_dst(const TStation *st, long n) {
  if (!st->dst_us) return 0;
  int y, m, d;
  cal(n, &y, &m, &d);
  long m1 = jdn(y, 3, 1), n1 = jdn(y, 11, 1);
  long start = m1 + (7 - (m1 + 1) % 7) % 7 + 7;
  long end = n1 + (7 - (n1 + 1) % 7) % 7;
  return (start <= n && n < end) ? 1 : 0;
}

/* ---- the astronomical angles, the basics, the vector (V + u, f) ---- */
static void astro(double j, double a[6], double *Nout) {
  double T = (j - 2451545.0) / 36525;
  double s = 218.3164477 + 481267.88123421 * T - 0.0015786 * T * T;
  double h = 280.46646 + 36000.76983 * T + 0.0003032 * T * T;
  double p = 83.3532465 + 4069.0137287 * T - 0.0103200 * T * T;
  double N = 125.04452 - 1934.136261 * T + 0.0020708 * T * T;
  double p1 = 282.93734 + 1.71946 * T + 0.00045688 * T * T;
  double ut = pmod(j + 0.5, 1) * 360;
  double tau = ut + 180 + h - s;
  a[0] = pmod(tau, 360); a[1] = pmod(s, 360); a[2] = pmod(h, 360);
  a[3] = pmod(p, 360); a[4] = pmod(-N, 360); a[5] = pmod(p1, 360);
  *Nout = pmod(N, 360);
}

/* basics: ub[NUB] = xi nu nup nupp2 Q R p (deg), fb[NFB] = Mm Mf O1 K1 J1 OO1 M2 K2 L2 M1 */
static void basics(double j, double a[6], double ub[NUB], double fb[NFB]) {
  double N;
  astro(j, a, &N);
  double n = N * D2R;
  double I = acos(TK_CICW - TK_SISW * cos(n));
  double A = atan2(TK_K1 * sin(n / 2), cos(n / 2));
  double B = atan2(TK_K2 * sin(n / 2), cos(n / 2));
  double nu = A - B, xi = n - A - B;
  double s2I = sin(2 * I), sI = sin(I), cI = cos(I);
  double nup = atan2(s2I * sin(nu), s2I * cos(nu) + 0.3347);
  double nupp2 = atan2(pow(sI, 2) * sin(2 * nu), pow(sI, 2) * cos(2 * nu) + 0.0727);
  double P = a[3] * D2R - xi;
  double fMm = (2.0 / 3 - pow(sI, 2)) / TK_DMM;
  double fMf = pow(sI, 2) / TK_DMF;
  double fO1 = sI * pow(cos(I / 2), 2) / TK_DO1;
  double fJ1 = s2I / TK_DJ1;
  double fOO1 = sI * pow(sin(I / 2), 2) / TK_DOO1;
  double fM2 = pow(cos(I / 2), 4) / TK_DM2;
  double fK1 = sqrt(0.2523 * pow(s2I, 2) + 0.1689 * s2I * cos(nu) + 0.0283) / TK_DK1;
  double fK2 = sqrt(0.2533 * pow(sI, 4) + 0.0367 * pow(sI, 2) * cos(2 * nu) + 0.0013) / TK_DK2;
  double t2 = pow(tan(I / 2), 2);
  double fL2 = fM2 * sqrt(1 - 12 * t2 * cos(2 * P) + 36 * t2 * t2);
  double R = atan2(sin(2 * P), 1 / (6 * t2) - cos(2 * P));
  double c2 = pow(cos(I / 2), 2);
  double fM1 = fO1 * sqrt(0.25 + 1.5 * cI / c2 * cos(2 * P) + 2.25 * cI * cI / (c2 * c2));
  double Q = atan2((5 * cI - 1) * sin(P), (7 * cI + 1) * cos(P));
  ub[0] = xi / D2R; ub[1] = nu / D2R; ub[2] = nup / D2R; ub[3] = nupp2 / D2R; ub[4] = Q / D2R; ub[5] = R / D2R;
  ub[6] = a[3];
  fb[0] = fMm; fb[1] = fMf; fb[2] = fO1; fb[3] = fK1; fb[4] = fJ1; fb[5] = fOO1; fb[6] = fM2; fb[7] = fK2;
  fb[8] = fL2; fb[9] = fM1;
}

static void vector(double j, double vu[NCONS], double f[NCONS]) {
  double a[6], ub[NUB], fb[NFB], lf[NFB];
  basics(j, a, ub, fb);
  for (int b = 0; b < NFB; b++) lf[b] = log(fb[b]);
  for (int k = 0; k < NCONS; k++) {
    double V = 0.0;
    for (int i = 0; i < 6; i++) V += CON_DOOD[k][i] * a[i];
    V += 90 * CON_K90[k];
    double u = 0.0;
    for (int b = 0; b < NUB; b++) u += CON_U[k][b] * ub[b];
    double e = 0.0;
    for (int b = 0; b < NFB; b++) e += CON_F[k][b] * lf[b];
    vu[k] = pmod(V + u, 360);
    f[k] = exp(e);
  }
}

static double speed(int k) {
  double w = 0.0;
  for (int i = 0; i < 6; i++) w += CON_DOOD[k][i] * TIDE_SPEED[i];
  return w;
}

/* ---- one component (LBL :VEC:) ---- */
typedef struct { int n; unsigned char k[NCONS]; double P[NCONS], A[NCONS], AW[NCONS], AW2[NCONS], W[NCONS]; } Comp;

static double hget(const TCon *h, int nh, int k, double *g) {
  for (int i = 0; i < nh; i++) if (h[i].k == k) { *g = h[i].g; return h[i].H; }
  *g = 0.0;
  return 0.0;
}

static int port_index(const TStation *st, unsigned char *ix) {
  int n = 0;
  for (int k = 0; k < NCONS; k++) {
    double g, H1 = hget(st->hc, st->nhc, k, &g), H2 = hget(st->hv, st->nhv, k, &g);
    if (H1 > 0 || H2 > 0) ix[n++] = (unsigned char)k;
  }
  return n;
}

static void comp(const TStation *st, double j0, const TCon *hc, int nhc, Comp *C) {
  double vu[NCONS], f[NCONS];
  C->n = port_index(st, C->k);
  vector(j0, vu, f);
  for (int i = 0; i < C->n; i++) {
    int k = C->k[i];
    double g, H = hget(hc, nhc, k, &g);
    double a = f[k] * (H + 1e-40);
    double w = speed(k), r = w * D2R;
    C->P[i] = pmod(vu[k] - g, 360);
    C->A[i] = a; C->AW[i] = a * r; C->AW2[i] = a * r * r; C->W[i] = w;
  }
}

static void evaluate(const Comp *C, double z0, double t, double out[3]) {
  double s0 = 0.0, s1 = 0.0, s2 = 0.0;
  for (int i = 0; i < C->n; i++) {
    double z = (C->P[i] + C->W[i] * t) * D2R;
    double c = cos(z), s = sin(z);
    s0 += C->A[i] * c;
    s1 += C->AW[i] * s;
    s2 += C->AW2[i] * c;
  }
  out[0] = z0 + s0; out[1] = -s1; out[2] = -s2;
}

static void herm(double f0, double d0, double f1, double d1, double s, double *v, double *dv) {
  double s2 = s * s, s3 = s * s * s;
  *v = (2 * s3 - 3 * s2 + 1) * f0 + (s3 - 2 * s2 + s) * d0 + (-2 * s3 + 3 * s2) * f1 + (s3 - s2) * d1;
  *dv = (6 * s2 - 6 * s) * f0 + (3 * s2 - 4 * s + 1) * d0 + (-6 * s2 + 6 * s) * f1 + (3 * s2 - 2 * s) * d1;
}

typedef struct { const Comp *E, *N; double z0, v0; int stream, rot; } Eng;
typedef struct { double u[3], v[3], F, Fp; } UV;

/* work space (~9 KB): on the heap, a DMCP5 program has only 8 KB of static RAM and a small stack */
typedef struct { Comp E, N, C; UV HR[25]; } Work;
static Work *work(void) {
  static Work *w;
  if (!w) w = (Work *)malloc(sizeof(Work));
  return w;
}

static void uv(const Eng *g, double t, UV *r) {
  evaluate(g->E, g->z0, t, r->u);
  if (g->rot) evaluate(g->N, g->v0, t, r->v);
  else r->v[0] = r->v[1] = r->v[2] = 0.0;
  if (!g->stream) { r->F = r->u[1]; r->Fp = r->u[2]; return; }
  r->F = r->u[0] * r->u[1] + r->v[0] * r->v[1];
  r->Fp = r->u[1] * r->u[1] + r->u[0] * r->u[2] + r->v[1] * r->v[1] + r->v[0] * r->v[2];
}

/* engine(): the day n (JDN of the local date) */
void tide_day(const TStation *st, long n, int stream, TDay *out) {
  Work *W = work();
  if (!W) { out->nev = 0; out->tz = st->tz; memset(out->curve, 0, sizeof out->curve); return; }
  Comp *Ep = &W->E, *Np = &W->N;
  UV *HR = W->HR;
  double tz = st->tz + tide_dst(st, n);
  double j0 = n - 0.5 - tz / 24;
  int rot = stream && st->kind == TK_ROT;
  comp(st, j0, st->hc, st->nhc, Ep);
  if (rot) comp(st, j0, st->hv, st->nhv, Np);
  Eng g = { Ep, Np, st->z0, st->v0, stream, rot };
  for (int t = 0; t < 25; t++) uv(&g, (double)t, &HR[t]);
  out->tz = tz;
  out->nev = 0;
  int most = stream ? 8 : 6;
  for (int k = 1; k < 25; k++) {
    double F0 = HR[k - 1].F, D0 = HR[k - 1].Fp, F1 = HR[k].F, D1 = HR[k].Fp;
    if (F0 * F1 < 0 && out->nev < most) {
      double s = F0 / (F0 - F1);
      for (int i = 0; i < 3; i++) {
        double p, dp;
        herm(F0, D0, F1, D1, s, &p, &dp);
        s -= p / dp;
        s = s < 0.0 ? 0.0 : s > 1.0 ? 1.0 : s;
      }
      double x0 = k - 1 + s;
      UV r;
      uv(&g, x0, &r);
      double x = x0 - r.F / r.Fp;
      if (x < k - 1.0) x = k - 1.0;
      if (x > (double)k) x = (double)k;
      double dx = x - x0;
      double uu = r.u[0] + r.u[1] * dx, vv = r.v[0] + r.v[1] * dx;
      int kind = r.Fp < 0 ? 1 : -1;
      TEvent *e = &out->ev[out->nev++];
      e->t = x; e->kind = kind;
      if (!stream) { e->val = uu; e->set = 0.0; }
      else if (!rot) { e->val = uu; e->set = uu >= 0 ? st->flood : st->ebb; }
      else { e->val = hypot(uu, vv); e->set = pmod(atan2(uu, vv) * RAD2DEG, 360); }
    }
  }
  for (int k = 0; k < 73; k++) {
    double t = k / 3.0;
    int i = (int)t;
    if (i > 23) i = 23;
    double s = t - i, u, v, d;
    herm(HR[i].u[0], HR[i].u[1], HR[i + 1].u[0], HR[i + 1].u[1], s, &u, &d);
    if (rot) {
      herm(HR[i].v[0], HR[i].v[1], HR[i + 1].v[0], HR[i + 1].v[1], s, &v, &d);
      out->curve[k] = hypot(u, v);
    } else out->curve[k] = u;
  }
}

void stream_now(const TStation *st, long n, double hour, double *val, double *set) {
  Work *W = work();
  if (!W) { *val = 0; *set = 0; return; }
  Comp *C = &W->C;
  double tz = st->tz + tide_dst(st, n);
  double j0 = n - 0.5 - tz / 24;
  double r[3];
  comp(st, j0, st->hc, st->nhc, C);
  evaluate(C, st->z0, hour, r);
  double u = r[0];
  if (st->kind != TK_ROT) { *val = u; *set = u >= 0 ? st->flood : st->ebb; return; }
  comp(st, j0, st->hv, st->nhv, C);
  evaluate(C, st->v0, hour, r);
  double v = r[0];
  *val = hypot(u, v);
  *set = pmod(atan2(u, v) * RAD2DEG, 360);
}

/* ---- FLOW ---- */
#define WFAC 0.025
#define WDEF 15.0

double flow_dm(double x) {
  double a = fabs(x);
  double d = floor(a + 1e-12);
  return copysign(d + (a - d) * 100 / 60, x);
}

int flow_cell(const FlowAtlas *fl, double lat, double lon, double FU[13], double FV[13]) {
  if (!fl || !fl->cells || fl->nlat < 2 || fl->nlon < 2) return 0;
  double k = 60.0 / fl->step_min;
  double fi = (lat - fl->lat0) * k, fj = (lon - fl->lon0) * k;
  if (!(0 <= fi && fi <= fl->nlat - 1 && 0 <= fj && fj <= fl->nlon - 1)) return 0;
  int i = (int)floor(fi), j = (int)floor(fj);
  if (i > fl->nlat - 2) i = fl->nlat - 2;
  if (j > fl->nlon - 2) j = fl->nlon - 2;
  double a = fi - i, b = fj - j, sw = 0.0;
  const int di[4] = { 0, 1, 0, 1 }, dj[4] = { 0, 0, 1, 1 };
  double w4[4] = { (1 - a) * (1 - b), a * (1 - b), (1 - a) * b, a * b };
  for (int h = 0; h < 13; h++) FU[h] = FV[h] = 0.0;
  for (int q = 0; q < 4; q++) {
    const unsigned char *c = fl->cells + 26 * ((i + di[q]) * fl->nlon + j + dj[q]);
    double w = w4[q];
    if (c[0] == 0 || w <= 0) continue;
    sw += w;
    for (int h = 0; h < 13; h++) {
      FU[h] += w * (c[2 * h] - 50) * fl->q;
      FV[h] += w * (c[2 * h + 1] - 50) * fl->q;
    }
  }
  if (sw < 1e-9) return 0;
  for (int h = 0; h < 13; h++) { FU[h] /= sw; FV[h] /= sw; }
  return 1;
}

static double hround(const TStation *ref, int k) {
  double g, H = hget(ref->hc, ref->nhc, k, &g);
  return nearbyint(H * 1e4) / 1e4;                 /* Python int(round(H * 1E4)) / 1E4 (half to even) */
}

double flow_spring(const TStation *ref) {
  int m2 = -1, s2 = -1;
  for (int k = 0; k < NCONS; k++) {
    if (!strcmp(CON_NAME[k], "M2")) m2 = k;
    if (!strcmp(CON_NAME[k], "S2")) s2 = k;
  }
  return 2 * (hround(ref, m2) + hround(ref, s2));
}

void flow_step(const TStation *ref, long n, int m, int d, long *on, int *om) {
  TDay D;
  tide_day(ref, n, 0, &D);
  int pick = -1;
  for (int k = 0; k < D.nev; k++) {
    if (D.ev[k].kind <= 0) continue;
    if (d > 0 ? k > m : (m < 0 || k < m)) { if (d > 0) { pick = k; break; } pick = k; }
  }
  if (pick >= 0) { *on = n; *om = pick; return; }
  tide_day(ref, n + d, 0, &D);
  pick = -1;
  for (int k = 0; k < D.nev; k++) {
    if (D.ev[k].kind <= 0) continue;
    if (d > 0) { pick = k; break; }
    pick = k;
  }
  *on = n + d;
  *om = pick < 0 ? 0 : pick;
}

void flow_pick(const TStation *ref, long n, double target, long *on, int *om) {
  TDay D;
  tide_day(ref, n, 0, &D);
  int hw[8], nh = 0;
  for (int k = 0; k < D.nev; k++) if (D.ev[k].kind > 0) hw[nh++] = k;
  if (!nh) { flow_step(ref, n, -1, 1, on, om); return; }
  if (target - D.ev[hw[nh - 1]].t > 6.2) { flow_step(ref, n, hw[nh - 1], 1, on, om); return; }
  if (D.ev[hw[0]].t - target > 6.2) { flow_step(ref, n, hw[0], -1, on, om); return; }
  int best = hw[0];
  for (int i = 1; i < nh; i++) if (fabs(D.ev[hw[i]].t - target) < fabs(D.ev[best].t - target)) best = hw[i];
  *on = n;
  *om = best;
}

void flow_cycle(const TStation *ref, long n, int m, double *t, double *h, double *rng, double *fac) {
  TDay D;
  tide_day(ref, n, 0, &D);
  if (m < 0 || m >= D.nev) { *t = 0; *h = 0; *rng = 0; *fac = 1; return; }
  *t = D.ev[m].t;
  *h = D.ev[m].val;
  double sum = 0.0;
  int nl = 0;
  for (int k = m - 1; k <= m + 1; k += 2)
    if (k >= 0 && k < D.nev && D.ev[k].kind < 0) { sum += D.ev[k].val; nl++; }
  if (!nl) { *rng = 0.0; *fac = 1.0; return; }
  *rng = *h - sum / nl;
  *fac = *rng / flow_spring(ref);
}

void flow_rows(const double FU[13], const double FV[13], double f, double wdir, double wkn, double U[13], double V[13]) {
  double wu = WFAC * wkn * sin((wdir + 180 + WDEF) * D2R);
  double wv = WFAC * wkn * cos((wdir + 180 + WDEF) * D2R);
  for (int h = 0; h < 13; h++) { U[h] = f * FU[h] + wu; V[h] = f * FV[h] + wv; }
}

void flow_now(const double U[13], const double V[13], double off, double *u, double *v) {
  int k = (int)floor(off + 6);
  if (k > 11) k = 11;
  if (k < 0) k = 0;
  double s = off + 6 - k;
  *u = U[k] + (U[k + 1] - U[k]) * s;
  *v = V[k] + (V[k + 1] - V[k]) * s;
}

void setkn(double u, double v, double *set, double *kn) {
  *set = pmod(atan2(u, v) * RAD2DEG, 360);
  *kn = hypot(u, v);
}
