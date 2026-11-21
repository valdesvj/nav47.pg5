/* dpcap.c - DPCAP47's engine: DNV-ST-0111 (Edition December 2021) level 1, and the ABS SKP method.
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. See dpcap.h for the axes (y to starboard: DNV's y is to port).
 *
 * DNV-ST-0111 level 1, section by section:
 *   2.2, Table 2-1  the environment a DP capability number; 2.4.4 counted up from the lowest; 2.4.6 10 degrees;
 *                   2.5 A B C D; 2.4.7 the worst single failure, the lowest a heading across the redundancy groups
 *   3.2.2           wind, current and waves x 1.25, balanced statically (forces and moment)
 *   3.3             coincident directions; Tp = 1.4049 Tz
 *   3.5, 3.6, 3.7   the wind, current and wave drift formulas (dp_force)
 *   3.9.2           nominal thrust T = eta1 eta2 (D P)^(2/3), P = P_B etaM (Tables 3-1 to 3-4)
 *   3.9.3, 3.9.4    beta_misc 0.9; the ventilation loss beta_vent (dp_vent)
 *   3.10            rudders behind a propeller giving positive thrust: Fsurge = T (1 - Cx a^2), Fsway = T Cy a, a <= 30
 *   3.11.2          forbidden zones from the vessel file
 *   3.11.3          a thruster may not flush a working thruster (not a tunnel) closer than 15 D: a forbidden sector
 *   3.11.4          flushing a dead thruster (not a tunnel) closer than 8 D (open) / 4 D (ducted): a thrust loss
 *   3.11.5          flushing a skeg: a thrust loss (Tables 3-7, 3-8)
 *   3.12.3          10 % of each group's generated power kept for the ship
 * Not in: batteries (3.12.2), cycloidal and water jet actuators.
 *
 * The allocation: minimise |W (B f - tau)|^2 + eps |f|^2 with each thruster's force inside its own set, a union of
 * convex pieces (a tunnel's segment, an azimuth's disc less its forbidden sectors and notches, a propeller's fan with
 * its rudder and its astern segment), by projected gradient with Nesterov's momentum (FISTA) and restarts; if not
 * held, each combination of the pieces. Held: the residual under 0.5 % of the demand, and the power within each
 * group's 90 %. tools/lpcheck.py checks it against an exact linear programme. */
#include "dpcap.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define PI_F   3.14159265f
#define RAD(d) ((d) * (PI_F / 180.0f))
#define DEG(r) ((r) * (180.0f / PI_F))
#define RHO_AIR 1.226f                  /* kg/m3, 3.5 */
#define RHO_W   1026.0f                 /* kg/m3, 3.6 */
#define G       9.81f
#define TOL     0.005f                  /* held: residual under 0.5 % of the demand */
#define EPS     1e-3f

float dp_dyn = 1.25f;
int dp_maxit = 600;
volatile int dp_abort;
uint32_t dp_solves, dp_iters;

/* Table 2-1: Beaufort, wind (upper limit, 10 min mean at 10 m), Hs, Tp, current */
const Env DP_TABLE[NBF] = {
  {  0.0f,  0.0f,  0.0f, 0.00f }, {  1.5f,  0.1f,  3.5f, 0.25f }, {  3.4f,  0.4f,  4.5f, 0.50f },
  {  5.4f,  0.8f,  5.5f, 0.75f }, {  7.9f,  1.3f,  6.5f, 0.75f }, { 10.7f,  2.1f,  7.5f, 0.75f },
  { 13.8f,  3.1f,  8.5f, 0.75f }, { 17.1f,  4.2f,  9.0f, 0.75f }, { 20.7f,  5.7f, 10.0f, 0.75f },
  { 24.4f,  7.4f, 10.5f, 0.75f }, { 28.4f,  9.5f, 11.5f, 0.75f }, { 32.6f, 12.1f, 12.0f, 0.75f },
};

const char *const DP_TYPE[T_CNT] = { "TUN", "AZI", "AZD", "PROP" };

/* only when the file gives no diameter: nominal thrust a kW of shaft power, by kind (PROVISIONAL, flagged) */
static const float KN_KW[T_CNT] = { 0.110f, 0.130f, 0.160f, 0.130f };

static float wrap360(float a) { a = fmodf(a, 360.0f); return a < 0 ? a + 360.0f : a; }

Env dp_env_wind(float w) {
  Env e;
  if (w <= 0) { e = DP_TABLE[0]; e.wind = 0; return e; }
  int i = 1;
  while (i < NBF - 1 && DP_TABLE[i].wind < w) i++;
  const Env *a = &DP_TABLE[i - 1], *b = &DP_TABLE[i];
  float k = (w - a->wind) / (b->wind - a->wind);
  if (k > 1) k = 1;                     /* above Beaufort 11: the waves stay as at 11 */
  e.wind = w;
  e.hs = a->hs + k * (b->hs - a->hs);
  e.tp = (i == 1 ? b->tp : a->tp + k * (b->tp - a->tp));
  e.cur = a->cur + k * (b->cur - a->cur);
  return e;
}

/* ------------------------------------------------------------------ the weather, 3.5 - 3.7
   DNV's Fy is to port and its Mz bow to port: both change sign here, the levers stay. */
static float fper(float tp) {           /* f(T') of 3.7 */
  if (tp < 1) return 1;
  float t3 = 1.0f / (tp * tp * tp);
  return t3 * expf(1 - t3);
}

void dp_force_parts(const Vessel *v, const Env *e, float from, float o[9]) {
  float d = RAD(wrap360(from)), c = cosf(d), s = sinf(d);
  float dir = d <= PI_F ? d : 2 * PI_F - d, k = dir / PI_F;
  /* 3.5 wind */
  float qw = 0.5f * RHO_AIR * e->wind * e->wind / 1000.0f;            /* kN/m2 */
  o[0] = qw * v->afw * (-0.7f * c);
  o[1] = -qw * v->alw * (0.9f * s);
  o[2] = o[1] * (v->xlw + 0.3f * (1 - 2 * k) * v->lpp);
  /* 3.6 current */
  float qc = 0.5f * RHO_W * e->cur * e->cur / 1000.0f;
  float lc = 0.4f * (1 - 2 * k);
  if (lc > 0.25f) lc = 0.25f;
  if (lc < -0.2f) lc = -0.2f;
  o[3] = qc * v->b * v->t * (-0.07f * c);
  o[4] = -qc * v->alc * (0.6f * s);
  o[5] = o[4] * (v->xlc + lc * v->lpp);
  /* 3.7 wave drift (Pierson-Moskowitz, cos^2 spreading) */
  o[6] = o[7] = o[8] = 0;
  if (e->hs > 0 && e->tp > 0) {
    float tz = e->tp / 1.4049f;
    float qd = 0.5f * RHO_W * G * e->hs * e->hs / 1000.0f;
    float h1a = 0.8f * powf(v->bow, 0.45f), h1b = 0.7f * v->cwlaft * v->cwlaft;
    float h1 = h1a + k * (h1b - h1a);
    float h2 = 0.05f + 0.95f * atanf(1.45f * (dir - 1.75f));
    float tsu = tz / (0.9f * powf(v->lpp, 0.33f)), tsw = tz / (0.75f * sqrtf(v->b));
    o[6] = qd * v->b * 0.09f * h1 * h2 * fper(tsu);
    o[7] = -qd * v->los * (0.09f * s) * fper(tsw);
    o[8] = o[7] * (v->xlos + (0.05f - 0.14f * k) * v->los);
  }
}

void dp_force(const Vessel *v, const Env *e, float from, float tau[3]) {
  float o[9];
  dp_force_parts(v, e, from, o);
  tau[0] = dp_dyn * (o[0] + o[3] + o[6]);
  tau[1] = dp_dyn * (o[1] + o[4] + o[7]);
  tau[2] = dp_dyn * (o[2] + o[5] + o[8]);
}

/* ------------------------------------------------------------------ the thrust, 3.9 and 3.10 */
void dp_prepare(Vessel *v) {
  v->ngrp = 0;
  float cw = v->cwlaft;
  if (cw < 0.85f) cw = 0.85f;
  if (cw > 1.15f) cw = 1.15f;
  v->cwlaft = cw;
  for (int i = 0; i < v->nthr; i++) {
    Thruster *t = &v->thr[i];
    t->flags = 0;
    int ducted = t->type == T_AZD || (t->type == T_PROP && t->duct);
    float em = t->type == T_TUN ? 0.93f : t->type == T_PROP ? 0.97f : t->pod ? 0.98f : 0.93f;   /* Table 3-4 */
    float e1 = t->type == T_TUN ? 900.0f : ducted ? 1200.0f : t->contra ? 950.0f : 800.0f;      /* Table 3-1 */
    float e2 = t->type == T_TUN ? (t->rounded ? 1.07f : t->broken ? 1.0f : 0.93f) : 1.0f;      /* Tables 3-2, 3-3 */
    float p = t->kw * em;
    if (t->kn > 0) t->tnom = t->kn;
    else if (t->d > 0) t->tnom = e1 * e2 * powf(t->d * p, 2.0f / 3.0f) / 1000.0f;
    else { t->tnom = KN_KW[t->type] * p * e2; t->flags |= TF_NO_D; }
    t->tf = t->tnom * 0.9f;                                           /* beta_misc, 3.9.3 */
    t->tr = 0;
    if (t->type == T_TUN) t->tr = t->tf;
    if (t->type == T_PROP)                                             /* Table 3-3, reversed */
      t->tr = t->tf * (t->cpp ? (t->duct ? 0.5f : 0.65f) : (t->duct ? 0.7f : 0.9f));
    t->cy = 0;
    if (t->type == T_PROP && t->rudder) {
      if (t->ar > 0 && t->d > 0)
        t->cy = 0.0126f * (t->k1 > 0 ? t->k1 : 1.1f) * (t->duct ? 1.15f : 1.0f) * t->ar / (t->d * t->d);
      else t->flags |= TF_NO_AR;
    }
    if (t->grp < 1) t->grp = 1;
    if (t->grp > MAX_GRP) t->grp = MAX_GRP;
    if (t->grp > v->ngrp) v->ngrp = t->grp;
  }
}

/* 3.9.4: the ventilation loss of each thruster in this sea, from this direction */
void dp_vent(const Vessel *v, const Env *e, float from, float beta[MAX_THR]) {
  float tz = e->tp > 0 ? e->tp / 1.4049f : 0;
  float t0 = tz > 0 ? 0.64f * sqrtf(v->lpp) / tz : 1;
  if (t0 > 1) t0 = 1;
  float ad = fabsf(RAD(wrap360(from + 180.0f) - 180.0f));             /* |direction| in [0, pi] */
  float bf = ad <= PI_F / 2 ? 1 + 0.38f * ad / PI_F : 1.38f - 0.38f * ad / PI_F;
  for (int i = 0; i < v->nthr; i++) {
    const Thruster *t = &v->thr[i];
    beta[i] = 1;
    if (t->d <= 0) continue;
    float cf = t->x >= 0 ? 1 : 1 + 0.4f * t->x / v->lpp;
    float A = 0.85f * bf * cf;
    float plf = sqrtf(t->tnom * 1000.0f / (t->d * t->d * t->d)) / 15.2f;
    float sig = 0.25f * (A * e->hs * t0 + (plf > 1 ? plf - 1 : 0));
    float xi = v->t - t->z;
    float arg = 2.0f * (2 * xi / t->d) - 1.5f * sig;
    beta[i] = 0.5f * erfcf(-arg / 1.41421356f);                       /* the normal distribution */
  }
}

/* ------------------------------------------------------------------ each thruster's set: pieces */
static Region *reg;                     /* MAX_THR, heap */
static const Vessel *reg_v;
static uint32_t reg_mask, reg_sum;

static uint32_t vsum(const Vessel *v) {   /* a checksum of what the sets depend on */
  uint32_t h = 2166136261u;
  const unsigned char *p = (const unsigned char *)v->thr;
  for (size_t k = 0; k < sizeof(Thruster) * (size_t)v->nthr; k++) h = (h ^ p[k]) * 16777619u;
  p = (const unsigned char *)v->skx;
  for (size_t k = 0; k < sizeof v->skx + sizeof v->sky; k++) h = (h ^ p[k]) * 16777619u;
  return h ^ (uint32_t)v->nskeg ^ ((uint32_t)v->nthr << 8);
}

typedef struct { int kind; float c, h, beta, a1, am, a2, fm; } Notch;  /* 0: Cartesian (3.11.4), 1: polar (3.11.5) */

static float notch_factor(const Notch *n, float th) {
  if (n->kind == 0) {
    float dlt = wrap360(th - n->c + 180.0f) - 180.0f;
    if (fabsf(dlt) >= n->h) return 1;
    float e = dlt < 0 ? n->c - n->h : n->c + n->h;                    /* the chord from the sector edge to the tip */
    float P1[2] = { cosf(RAD(e)), sinf(RAD(e)) }, Pc[2] = { n->beta * cosf(RAD(n->c)), n->beta * sinf(RAD(n->c)) };
    float D[2] = { Pc[0] - P1[0], Pc[1] - P1[1] }, u[2] = { cosf(RAD(th)), sinf(RAD(th)) };
    float den = u[0] * D[1] - u[1] * D[0];
    if (fabsf(den) < 1e-9f) return n->beta;
    float r = (P1[0] * D[1] - P1[1] * D[0]) / den;
    return r < 0 ? 0 : r > 1 ? 1 : r;
  }
  float a = wrap360(360.0f - th);                                      /* DNV's actuator angle */
  if (a <= n->a1 || a >= n->a2) return 1;
  if (a <= n->am) return 1 + (n->fm - 1) * (a - n->a1) / (n->am - n->a1 > 1e-6f ? n->am - n->a1 : 1e-6f);
  return n->fm + (1 - n->fm) * (a - n->am) / (n->a2 - n->am > 1e-6f ? n->a2 - n->am : 1e-6f);
}

static void add_piece_seg(Region *r, float ax, float ay, float bx, float by) {
  if (r->n >= MAXP) return;
  Piece *p = &r->p[r->n++];
  p->kind = K_SEG; p->nv = 2;
  p->v[0][0] = ax; p->v[0][1] = ay; p->v[1][0] = bx; p->v[1][1] = by;
}

static void build_azimuth(const Vessel *v, uint32_t mask, int i, Region *r) {
  const Thruster *t = &v->thr[i];
  float R = t->tf, fz[MAX_THR + 2][2];                                 /* forbidden sectors: start, span */
  int nf = 0;
  Notch nt[MAX_THR + MAX_SKEG];
  int nn = 0;
  if (wrap360(t->fz_b - t->fz_a) > 0) { fz[nf][0] = t->fz_a; fz[nf][1] = wrap360(t->fz_b - t->fz_a); nf++; }
  int ducted = t->type == T_AZD;
  for (int j = 0; j < v->nthr && t->d > 0; j++) {
    const Thruster *o = &v->thr[j];
    if (j == i || o->type == T_TUN) continue;                          /* tunnels may be flushed */
    float dx = t->x - o->x, dy = t->y - o->y, s = sqrtf(dx * dx + dy * dy);
    if (s < 1e-3f) continue;
    float c = DEG(atan2f(dy, dx));                                     /* thrust this way washes the other */
    if (mask >> j & 1) {
      if (s < 15 * t->d && nf < MAX_THR + 2) {                         /* 3.11.3 */
        float h = DEG(atanf(0.1f + t->d / s));
        fz[nf][0] = wrap360(c - h); fz[nf][1] = 2 * h; nf++;
      }
    } else if (s < (ducted ? 4 : 8) * t->d) {                          /* 3.11.4 */
      float sd = s / t->d;
      Notch *n = &nt[nn++];
      n->kind = 0; n->c = wrap360(c); n->h = DEG(atanf((ducted ? 0.35f : 0.6f) * t->d / s));
      n->beta = 1 - 1 / (0.02f * sd * sd + 0.25f * sd + 1.2f);
    }
  }
  for (int k = 0; k < v->nskeg && t->d > 0; k++) {                    /* 3.11.5, in DNV's axes (y to port) */
    float xs = v->skx[k], ys = -v->sky[k], xt = t->x, yt = -t->y;
    if (xs <= xt && fabsf(ys - yt) < t->d / 2) continue;               /* (a thruster right behind the skeg) */
    float s = xs > xt ? sqrtf((xs - xt) * (xs - xt) + (ys - yt) * (ys - yt)) : fabsf(ys - yt);
    if (s >= (ducted ? 8 : 15) * t->d || fabsf(ys - yt) < 1e-6f) continue;
    float aj = atanf(0.6f * t->d / s), at = atanf((xs - xt) / (ys - yt));
    Notch *n = &nt[nn++];
    n->kind = 1;
    if (yt >= ys) {                                                    /* a port thruster */
      float af = PI_F / 2 - at, am = fminf(fmaxf(af + aj, PI_F / 2), PI_F);
      n->a1 = DEG(fmaxf(af - aj, 0)); n->am = DEG(am); n->a2 = DEG(fminf(am + 4 * aj, PI_F));
      n->fm = 2 * am / PI_F - 1;
    } else {
      float af = 3 * PI_F / 2 - at, am = fmaxf(fminf(af - aj, 3 * PI_F / 2), PI_F);
      n->a1 = DEG(fmaxf(am - 4 * aj, PI_F)); n->am = DEG(am); n->a2 = DEG(fminf(af + aj, 2 * PI_F));
      n->fm = 3 - 2 * am / PI_F;
    }
    if (n->fm < 0) n->fm = 0;
    if (n->fm > 1) n->fm = 1;
  }
  /* the allowed arcs: the circle less the forbidden sectors */
  float marks[2 * (MAX_THR + 2)][2];                                   /* (angle, +1 enter / -1 leave) */
  int nm = 0;
  for (int k = 0; k < nf; k++) {
    marks[nm][0] = fz[k][0]; marks[nm++][1] = 1;
    marks[nm][0] = wrap360(fz[k][0] + fz[k][1]); marks[nm++][1] = -1;
  }
  float arcs[MAX_THR + 3][2];
  int na = 0;
  if (!nf) { arcs[0][0] = 0; arcs[0][1] = 360; na = 1; }
  else {
    float ang[2 * (MAX_THR + 2)];                                      /* every sector edge */
    for (int k = 0; k < nm; k++) ang[k] = marks[k][0];
    for (int a = 1; a < nm; a++) for (int b = a; b > 0 && ang[b] < ang[b - 1]; b--) { float x = ang[b]; ang[b] = ang[b - 1]; ang[b - 1] = x; }
    for (int k = 0; k < nm; k++) {                                     /* free between two edges: its middle in no sector */
      float a0 = ang[k], a1 = k + 1 < nm ? ang[k + 1] : ang[0] + 360.0f;
      if (a1 - a0 < 1e-3f) continue;
      float mid = wrap360((a0 + a1) / 2);
      int in = 0;
      for (int q = 0; q < nf; q++) if (wrap360(mid - fz[q][0]) < fz[q][1]) in = 1;
      if (!in && na < MAX_THR + 3) { arcs[na][0] = a0; arcs[na][1] = a1 - a0; na++; }
    }
  }
  /* each arc: split at the notch tips and to 180 degrees or less; a sector, or a polygon where notched */
  for (int k = 0; k < na && r->n < MAXP; k++) {
    float cuts[16]; int nc = 0;
    cuts[nc++] = 0;
    for (int q = 0; q < nn; q++) {
      float tip = nt[q].kind == 0 ? nt[q].c : wrap360(360.0f - nt[q].am);
      float off = wrap360(tip - arcs[k][0]);
      if (off > 0.5f && off < arcs[k][1] - 0.5f && nc < 15) cuts[nc++] = off;
    }
    cuts[nc++] = arcs[k][1];
    for (int a = 1; a < nc; a++) for (int b = a; b > 0 && cuts[b] < cuts[b - 1]; b--) { float x = cuts[b]; cuts[b] = cuts[b - 1]; cuts[b - 1] = x; }
    for (int c = 0; c + 1 < nc && r->n < MAXP; c++) {
      float s0 = cuts[c], span = cuts[c + 1] - s0;
      int parts = span > 180.0f ? (span > 360.0f - 1e-3f && nn == 0 ? 1 : 2) : 1;
      for (int p = 0; p < parts && r->n < MAXP; p++) {
        float a0 = arcs[k][0] + s0 + p * span / parts, sp = span / parts;
        int notched = 0;
        for (int q = 0; q < nn; q++)
          for (int m = 0; m <= 8 && !notched; m++) if (notch_factor(&nt[q], a0 + sp * m / 8) < 0.9999f) notched = 1;
        Piece *pc = &r->p[r->n++];
        if (!notched && sp > 360.0f - 1e-3f) { pc->kind = K_DISK; pc->r = R; continue; }
        if (!notched) { pc->kind = K_SECT; pc->a0 = wrap360(a0); pc->span = sp; pc->r = R; continue; }
        pc->kind = K_POLY; pc->a0 = wrap360(a0); pc->span = sp;
        int nv = 0;
        pc->v[nv][0] = 0; pc->v[nv][1] = 0; nv++;
        int steps = (int)ceilf(sp / 5.0f);
        if (steps > MAXV - 2) steps = MAXV - 2;
        for (int m = 0; m <= steps; m++) {
          float th = a0 + sp * m / steps, f = 1;
          for (int q = 0; q < nn; q++) { float g = notch_factor(&nt[q], th); if (g < f) f = g; }
          pc->v[nv][0] = R * f * cosf(RAD(th)); pc->v[nv][1] = R * f * sinf(RAD(th)); nv++;
        }
        pc->nv = (unsigned char)nv;
      }
    }
  }
}

static void build(const Vessel *v, uint32_t mask) {
  for (int i = 0; i < v->nthr; i++) {
    Region *r = &reg[i];
    const Thruster *t = &v->thr[i];
    r->n = 0;
    if (!(mask >> i & 1)) continue;
    if (t->type == T_TUN) add_piece_seg(r, 0, -t->tf, 0, t->tf);
    else if (t->type == T_PROP) {
      if (t->cy > 0) {                                                 /* 3.10: the fan of the rudder, ahead */
        Piece *p = &r->p[r->n++];
        p->kind = K_POLY;
        p->span = 2 * DEG(atan2f(30 * t->cy, 1 - 0.02f * t->cy * 900)); p->a0 = wrap360(-p->span / 2);
        int nv = 0;
        p->v[nv][0] = 0; p->v[nv][1] = 0; nv++;
        for (int a = -30; a <= 30; a += 5) {
          float al = (float)a, cx = 0.02f * t->cy;
          p->v[nv][0] = t->tf * (1 - cx * al * al); p->v[nv][1] = t->tf * t->cy * al; nv++;
        }
        p->nv = (unsigned char)nv;
        if (t->tr > 0) add_piece_seg(r, -t->tr, 0, 0, 0);
      } else add_piece_seg(r, -t->tr, 0, t->tf, 0);
    } else build_azimuth(v, mask, i, r);
  }
}

const Region *dp_regions(const Vessel *v, uint32_t mask) {
  if (!reg) {
    reg = malloc(sizeof(Region) * MAX_THR);
    if (!reg) return NULL;
    reg_v = NULL;
  }
  uint32_t s = vsum(v);
  if (reg_v != v || reg_mask != mask || reg_sum != s) { build(v, mask); reg_v = v; reg_mask = mask; reg_sum = s; }
  return reg;
}

/* ------------------------------------------------------------------ projection onto the pieces */
static float d2(const float a[2], const float b[2]) { float x = a[0] - b[0], y = a[1] - b[1]; return x * x + y * y; }

static void seg(const float a[2], const float b[2], const float p[2], float o[2]) {
  float dx = b[0] - a[0], dy = b[1] - a[1], l = dx * dx + dy * dy;
  float t = l > 0 ? ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / l : 0;
  if (t < 0) t = 0;
  if (t > 1) t = 1;
  o[0] = a[0] + t * dx; o[1] = a[1] + t * dy;
}

static void ray(float ang, float R, const float p[2], float o[2]) {
  float ux = cosf(RAD(ang)), uy = sinf(RAD(ang));
  float t = p[0] * ux + p[1] * uy;
  if (t < 0) t = 0;
  if (t > R) t = R;
  o[0] = t * ux; o[1] = t * uy;
}

static void proj_piece(const Piece *p, const float f[2], float o[2]) {
  switch (p->kind) {
  case K_SEG: seg(p->v[0], p->v[1], f, o); return;
  case K_DISK: {
    float r2 = f[0] * f[0] + f[1] * f[1];
    o[0] = f[0]; o[1] = f[1];
    if (r2 > p->r * p->r) { float k = p->r / sqrtf(r2); o[0] *= k; o[1] *= k; }
    return;
  }
  case K_SECT: {
    float r2 = f[0] * f[0] + f[1] * f[1];
    if (r2 < 1e-12f) { o[0] = o[1] = 0; return; }
    float th = wrap360(DEG(atan2f(f[1], f[0])));
    if (wrap360(th - p->a0) <= p->span) {
      o[0] = f[0]; o[1] = f[1];
      if (r2 > p->r * p->r) { float k = p->r / sqrtf(r2); o[0] *= k; o[1] *= k; }
      return;
    }
    float o1[2], o2[2];
    ray(p->a0, p->r, f, o1);
    ray(p->a0 + p->span, p->r, f, o2);
    if (d2(o1, f) <= d2(o2, f)) { o[0] = o1[0]; o[1] = o1[1]; } else { o[0] = o2[0]; o[1] = o2[1]; }
    return;
  }
  default: {                                                           /* a convex polygon, corners in order */
    int n = p->nv, in = 1;
    for (int k = 0; k < n && in; k++) {
      const float *a = p->v[k], *b = p->v[(k + 1) % n];
      if ((b[0] - a[0]) * (f[1] - a[1]) - (b[1] - a[1]) * (f[0] - a[0]) < -1e-6f) in = 0;
    }
    if (in) { o[0] = f[0]; o[1] = f[1]; return; }
    float bd = 1e30f, q[2];
    for (int k = 0; k < n; k++) {
      seg(p->v[k], p->v[(k + 1) % n], f, q);
      float dd = d2(q, f);
      if (dd < bd) { bd = dd; o[0] = q[0]; o[1] = q[1]; }
    }
  }
  }
}

/* piece -1: the whole set (the nearest of the pieces); s: the ventilation loss (the set x s) */
static void project(const Region *r, int piece, float s, float f[2]) {
  if (r->n == 0) { f[0] = f[1] = 0; return; }
  float g[2] = { f[0] / s, f[1] / s }, o[2], best[2] = { 0, 0 }, bd = 1e30f;
  if (piece < 0 && r->n > 1 && (g[0] != 0 || g[1] != 0)) {
    /* the pieces of an azimuth (or a rudder's fan) are wedges from 0,0 side by side: a point in a piece's wedge is
       nearest to that piece (the wedges next to it are beyond the edge they share). Only between wedges (in a
       forbidden sector) the nearest of them all. */
    float th = wrap360(DEG(atan2f(g[1], g[0])));
    for (int k = 0; k < r->n; k++) {
      const Piece *p = &r->p[k];
      if ((p->kind == K_SECT || p->kind == K_POLY) && p->span > 0 && wrap360(th - p->a0) <= p->span) { piece = k; break; }
      if (p->kind == K_DISK) { piece = k; break; }
    }
  }
  for (int k = piece < 0 ? 0 : piece; k < (piece < 0 ? r->n : piece + 1); k++) {
    proj_piece(&r->p[k], g, o);
    float dd = d2(o, g);
    if (dd < bd) { bd = dd; best[0] = o[0]; best[1] = o[1]; }
  }
  f[0] = best[0] * s; f[1] = best[1] * s;
}

/* ------------------------------------------------------------------ the allocation */
static int alloc_core(const Vessel *v, uint32_t mask, const float tau[3], Alloc *a, int maxit, const signed char *pc,
                      const float *beta, const Region *rg) {
  int idx[MAX_THR], n = 0;
  for (int i = 0; i < v->nthr; i++) {
    a->beta[i] = beta ? beta[i] : 1;
    if (mask >> i & 1) idx[n++] = i;
    else a->f[i][0] = a->f[i][1] = 0;
  }
  float wm = 2.0f / v->lpp;                                            /* the moment weighed by half the length */
  float T[3] = { tau[0], tau[1], tau[2] };
  float tn = sqrtf(T[0] * T[0] + T[1] * T[1] + (wm * T[2]) * (wm * T[2]));
  if (n == 0) {
    a->res = tn > 1e-3f ? 1 : 0; a->ok = a->res <= TOL; a->power_ok = 1; a->iters = 0;
    for (int i = 0; i < v->nthr; i++) a->use[i] = 0;
    return a->ok;
  }
  float M[3][3] = { { 0 } };                                           /* Lipschitz: W B B^T W's largest eigenvalue */
  for (int k = 0; k < n; k++) {
    const Thruster *t = &v->thr[idx[k]];
    float cx[3] = { 1, 0, -t->y * wm }, cy[3] = { 0, 1, t->x * wm };
    for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) M[r][c] += cx[r] * cx[c] + cy[r] * cy[c];
  }
  float e[3] = { 1, 1, 1 }, lam = 1;
  for (int it = 0; it < 24; it++) {
    float q[3];
    for (int r = 0; r < 3; r++) q[r] = M[r][0] * e[0] + M[r][1] * e[1] + M[r][2] * e[2];
    lam = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]);
    if (lam < 1e-9f) break;
    for (int r = 0; r < 3; r++) e[r] = q[r] / lam;
  }
  float step = 1.0f / (lam + EPS);

  float f[MAX_THR][2], z[MAX_THR][2], sc[MAX_THR];
  for (int k = 0; k < n; k++) {
    int i = idx[k];
    sc[k] = a->beta[i] > 1e-4f ? a->beta[i] : 1e-4f;
    f[k][0] = a->f[i][0]; f[k][1] = a->f[i][1];
    project(&rg[i], pc[i], sc[k], f[k]);
  }
  {                                                                     /* a warm start worse than none: none */
    float r[3] = { -T[0], -T[1], -T[2] };
    for (int k = 0; k < n; k++) {
      const Thruster *t = &v->thr[idx[k]];
      r[0] += f[k][0]; r[1] += f[k][1]; r[2] += t->x * f[k][1] - t->y * f[k][0];
    }
    if (sqrtf(r[0] * r[0] + r[1] * r[1] + (wm * r[2]) * (wm * r[2])) > tn)
      for (int k = 0; k < n; k++) { f[k][0] = f[k][1] = 0; project(&rg[idx[k]], pc[idx[k]], sc[k], f[k]); }
  }
  for (int k = 0; k < n; k++) { z[k][0] = f[k][0]; z[k][1] = f[k][1]; }
  float tk = 1, rel = 1, last_rel = 1e9f, rel32 = 1e9f;
  int it;
  for (it = 0; it < maxit; it++) {
    float r[3] = { -T[0], -T[1], -T[2] };
    for (int k = 0; k < n; k++) {
      const Thruster *t = &v->thr[idx[k]];
      r[0] += z[k][0]; r[1] += z[k][1]; r[2] += t->x * z[k][1] - t->y * z[k][0];
    }
    if ((it & 7) == 0) {
      float rr = sqrtf(r[0] * r[0] + r[1] * r[1] + (wm * r[2]) * (wm * r[2]));
      rel = tn > 1e-3f ? rr / tn : rr;
      if (rel < TOL * 0.3f) break;
      if ((it & 31) == 0) {                                             /* plainly not held: far, and stuck */
        if (it >= 128 && rel > 0.25f && rel > 0.97f * rel32) break;
        rel32 = rel;
      }
      if (rel > last_rel) tk = 1;                                         /* restart the momentum */
      last_rel = rel;
    }
    r[2] *= wm * wm;
    float tk1 = 0.5f * (1.0f + sqrtf(1.0f + 4.0f * tk * tk)), mom = (tk - 1.0f) / tk1;
    for (int k = 0; k < n; k++) {
      const Thruster *t = &v->thr[idx[k]];
      float g0 = r[0] - t->y * r[2] + EPS * z[k][0];
      float g1 = r[1] + t->x * r[2] + EPS * z[k][1];
      float nf[2] = { z[k][0] - step * g0, z[k][1] - step * g1 };
      project(&rg[idx[k]], pc[idx[k]], sc[k], nf);
      z[k][0] = nf[0] + mom * (nf[0] - f[k][0]);
      z[k][1] = nf[1] + mom * (nf[1] - f[k][1]);
      f[k][0] = nf[0]; f[k][1] = nf[1];
    }
    tk = tk1;
  }
  dp_iters += (uint32_t)it;
  float r[3] = { -T[0], -T[1], -T[2] };
  for (int k = 0; k < n; k++) {
    const Thruster *t = &v->thr[idx[k]];
    r[0] += f[k][0]; r[1] += f[k][1]; r[2] += t->x * f[k][1] - t->y * f[k][0];
  }
  float rr = sqrtf(r[0] * r[0] + r[1] * r[1] + (wm * r[2]) * (wm * r[2]));
  a->res = tn > 1e-3f ? rr / tn : (rr > 1e-3f ? 1 : 0);
  a->iters = it;
  float pw[MAX_GRP + 1] = { 0 };
  for (int k = 0; k < n; k++) {
    int i = idx[k];
    const Thruster *t = &v->thr[i];
    a->f[i][0] = f[k][0]; a->f[i][1] = f[k][1];
    float m = sqrtf(f[k][0] * f[k][0] + f[k][1] * f[k][1]);
    float cap = (t->type == T_PROP && f[k][0] < 0 ? t->tr : t->tf) * sc[k];
    a->use[i] = cap > 0 ? m / cap : 0;
    if (a->use[i] > 1) a->use[i] = 1;
    float kw = t->kw > 0 ? t->kw : t->kn / KN_KW[t->type];
    pw[t->grp] += kw * a->use[i] * sqrtf(a->use[i]);                   /* power as the thrust^1.5 */
  }
  for (int i = 0; i < v->nthr; i++) if (!(mask >> i & 1)) a->use[i] = 0;
  a->power_ok = 1;
  for (int g = 1; g <= MAX_GRP; g++) if (v->gen[g] > 0 && pw[g] > 0.9f * v->gen[g]) a->power_ok = 0;
  a->ok = a->res <= TOL && a->power_ok;
  return a->ok;
}

#define MAX_COMBO 64
#define NEAR      0.15f                 /* the whole sets came this near: try the combinations of pieces */

int dp_alloc_b(const Vessel *v, uint32_t mask, const float tau[3], Alloc *a, int maxit, const float *beta) {
  const Region *rg = dp_regions(v, mask);
  signed char pc[MAX_THR];
  memset(pc, -1, sizeof pc);
  dp_solves++;
  if (!rg) { memset(a, 0, sizeof *a); a->res = 1; return 0; }
  int warm = 0;
  for (int i = 0; i < v->nthr; i++) if (a->f[i][0] != 0 || a->f[i][1] != 0) warm = 1;
  if (alloc_core(v, mask, tau, a, maxit, pc, beta, rg)) return 1;
  if (warm) {                                                          /* a warm start may sit in a corner: cold */
    Alloc c;
    memset(&c, 0, sizeof c);
    if (alloc_core(v, mask, tau, &c, maxit, pc, beta, rg) || c.res < a->res) *a = c;
    if (a->ok) return 1;
  }
  if (a->res > NEAR || !a->power_ok) return 0;                         /* far: no combination will hold it */
  int nc[MAX_THR], k = 0, combos = 1;
  for (int i = 0; i < v->nthr; i++)
    if (mask >> i & 1 && rg[i].n > 1 && combos * rg[i].n <= MAX_COMBO) { nc[k++] = i; combos *= rg[i].n; }
  Alloc best = *a;
  if (k == 0) {                                                        /* convex: go on from where it stopped */
    alloc_core(v, mask, tau, a, maxit, pc, beta, rg);
    if (!a->ok && best.res < a->res) *a = best;
    return a->ok;
  }
  for (int c = 0; c < combos; c++) {
    int x = c;
    for (int j = 0; j < k; j++) { pc[nc[j]] = (signed char)(x % rg[nc[j]].n); x /= rg[nc[j]].n; }
    Alloc t;
    memset(&t, 0, sizeof t);
    alloc_core(v, mask, tau, &t, maxit, pc, beta, rg);
    if (t.ok || t.res < best.res) best = t;
    if (t.ok) break;
  }
  *a = best;
  return a->ok;
}

int dp_alloc(const Vessel *v, uint32_t mask, const float tau[3], Alloc *a, int maxit) {
  return dp_alloc_b(v, mask, tau, a, maxit, NULL);
}

uint32_t dp_case_mask(const Vessel *v, int c) {
  uint32_t m = 0;
  for (int i = 0; i < v->nthr; i++) if (c == 0 || v->thr[i].grp != c) m |= 1u << i;
  return m;
}

int dp_held(const Vessel *v, uint32_t mask, const Env *e, float from, Alloc *a) {
  float tau[3], beta[MAX_THR];
  dp_force(v, e, from, tau);
  tau[0] = -tau[0]; tau[1] = -tau[1]; tau[2] = -tau[2];             /* the thrusters push against the weather */
  dp_vent(v, e, from, beta);
  return dp_alloc_b(v, mask, tau, a, dp_maxit, beta);
}

int dp_number(const Vessel *v, uint32_t mask, float from, Alloc *a) {
  for (int bf = 1; bf < NBF; bf++) if (!dp_held(v, mask, &DP_TABLE[bf], from, a)) return bf - 1;
  return NBF - 1;
}

#define WIND_MAX 40.0f

float dp_limit_wind(const Vessel *v, uint32_t mask, float from, float cur, int num, Alloc *a) {
  float lo = 0, hi = WIND_MAX;
  int steps = 13;
  Env e;
  if (cur < 0 && num >= 0) {                                           /* between the steps the number gave */
    lo = DP_TABLE[num].wind;
    hi = num < NBF - 1 ? DP_TABLE[num + 1].wind : WIND_MAX;
    steps = 10;
  } else {
    e = dp_env_wind(0);
    if (cur >= 0) e.cur = cur;
    if (!dp_held(v, mask, &e, from, a)) return 0;
  }
  e = dp_env_wind(hi);
  if (cur >= 0) e.cur = cur;
  if (dp_held(v, mask, &e, from, a)) return hi;
  Alloc keep = *a;
  for (int s = 0; s < steps; s++) {
    float mid = 0.5f * (lo + hi);
    e = dp_env_wind(mid);
    if (cur >= 0) e.cur = cur;
    *a = keep;
    if (dp_held(v, mask, &e, from, a)) { lo = mid; keep = *a; } else hi = mid;
  }
  *a = keep;
  return lo;
}

static int in_bow(int h) { return h <= 3 || h >= NHEAD - 3; }          /* within 30 degrees of the bow */

void dp_analyse(const Vessel *v, Result *r, void (*progress)(int, int)) {
  memset(r, 0, sizeof *r);
  uint32_t s0 = dp_solves, i0 = dp_iters;
  r->ncase = 1 + v->ngrp;
  int total = r->ncase * NHEAD, done = 0;
  for (int c = 0; c < r->ncase; c++) {
    uint32_t m = dp_case_mask(v, c);
    Alloc a;
    memset(&a, 0, sizeof a);
    for (int h = 0; h < NHEAD && !dp_abort; h++) {
      float from = (float)(h * 10);
      int num = dp_number(v, m, from, &a);
      r->num[c][h] = (signed char)num;
      r->wind[c][h] = dp_limit_wind(v, m, from, -1, num, &a);
      if (progress) progress(++done, total);
    }
  }
  r->A = r->B = 99;
  r->C = r->D = v->ngrp > 1 ? 99 : -1;
  for (int h = 0; h < NHEAD; h++) {
    int w = 99; float ww = 99;
    for (int c = 1; c < r->ncase; c++) {
      if (r->num[c][h] < w) w = r->num[c][h];
      if (r->wind[c][h] < ww) ww = r->wind[c][h];
    }
    r->wnum[h] = (signed char)(w == 99 ? r->num[0][h] : w);
    r->wwind[h] = ww == 99 ? r->wind[0][h] : ww;
    if (r->num[0][h] < r->B) r->B = r->num[0][h];
    if (in_bow(h) && r->num[0][h] < r->A) r->A = r->num[0][h];
    if (v->ngrp > 1) {
      if (r->wnum[h] < r->D) r->D = r->wnum[h];
      if (in_bow(h) && r->wnum[h] < r->C) r->C = r->wnum[h];
    }
  }
  r->solves = dp_solves - s0;
  r->iters = dp_iters - i0;
}

/* the map: the limiting wind for each heading and each current from 0 to 2 m/s (NCUR steps), the rest of the
   weather as Table 2-1 has it for the wind. map[h * NCUR + c], m/s. */
void dp_map(const Vessel *v, uint32_t mask, float *map, void (*progress)(int, int)) {
  Alloc a;
  memset(&a, 0, sizeof a);
  int done = 0;
  for (int h = 0; h < NHEAD; h++)
    for (int c = 0; c < NCUR && !dp_abort; c++) {
      map[h * NCUR + c] = dp_limit_wind(v, mask, (float)(h * 10), (float)c * (2.0f / (NCUR - 1)), -1, &a);
      if (progress) progress(++done, NHEAD * NCUR);
    }
}

/* ------------------------------------------------------------------ the vessel file (.DPV), plain text
   key=value lines; # starts a comment. The names of DNV-ST-0111 Table A-2 and A-3.
     name=EXAMPLE DSV   lpp=110  b=22  t=7             Lpp, breadth at the water line, summer draught (m)
     afw=420  alw=1500  xlw=5                          wind: A_F,wind, A_L,wind (m2), X_L,air (m from Lpp/2)
     alc=770  xlc=0                                    current: A_L,current (m2), X_L,current (default lpp*t, 0)
     los=114  xlos=1  bowdeg=34  awlaft=1150           waves: L_OS, X_LOS (m), bow angle (degrees), A_WLaft (m2)
     skeg=-40,0                                        the aft-most point of a skeg or gondola (x, y m), up to 2
     gen=1,6000                                        kW a redundancy group generates (none: no limit)
     thr=NAME,TYPE,x,y,kW,kN,group,fzFrom,fzTo,OPTS,D,z,Ar,k1
        TYPE TUN AZI AZD PROP; kW: MCR brake power in DP; kN: nominal thrust if known (0: from kW and D)
        OPTS: R rudder, C CPP, D ducted, I rounded tunnel inlet, B broken inlet, P pod, X contra-rotating
        D propeller diameter (m), z above the base line (m), Ar rudder area behind the propeller (m2), k1 (1.1)
     cw= cc= cd= site= sitecur=                        the PC's level 2 data: read by tools/dpcap, skipped here */
static char *trim(char *s) {
  while (*s == ' ' || *s == '\t') s++;
  char *e = s + strlen(s);
  while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) *--e = 0;
  return s;
}

static int split(char *s, char *out[], int max) {
  int n = 0;
  for (;;) {
    char *c = strchr(s, ',');
    if (c) *c = 0;
    if (n < max) out[n++] = trim(s);
    if (!c) break;
    s = c + 1;
  }
  return n;
}

static int ieq(const char *a, const char *b) {
  for (; *a && *b; a++, b++) { char x = *a, y = *b; if (x >= 'a' && x <= 'z') x -= 32; if (y >= 'a' && y <= 'z') y -= 32;
    if (x != y) return 0; }
  return *a == *b;
}

int dp_parse(Vessel *v, const char *text, char *err, int errn) {
  memset(v, 0, sizeof *v);
  v->alc = v->los = v->bow = v->cwlaft = -1;
  int have_xlos = 0;
  float awlaft = -1;
  strcpy(v->name, "vessel");
  char line[160];
  int ln = 0;
  const char *p = text;
  while (*p) {
    int k = 0;
    while (*p && *p != '\n') { if (k < (int)sizeof line - 1) line[k++] = *p; p++; }
    if (*p) p++;
    line[k] = 0;
    ln++;
    char *h = strchr(line, '#');
    if (h) *h = 0;
    char *s = trim(line);
    if (!*s) continue;
    char *eq = strchr(s, '=');
    if (!eq) { snprintf(err, (size_t)errn, "line %d: no '='", ln); return -1; }
    *eq = 0;
    char *key = trim(s), *val = trim(eq + 1);
    float f = (float)atof(val);
    if (ieq(key, "name")) { strncpy(v->name, val, sizeof v->name - 1); v->name[sizeof v->name - 1] = 0; }
    else if (ieq(key, "lpp")) v->lpp = f;
    else if (ieq(key, "b")) v->b = f;
    else if (ieq(key, "t")) v->t = f;
    else if (ieq(key, "afw")) v->afw = f;
    else if (ieq(key, "alw")) v->alw = f;
    else if (ieq(key, "xlw")) v->xlw = f;
    else if (ieq(key, "alc")) v->alc = f;
    else if (ieq(key, "xlc")) v->xlc = f;
    else if (ieq(key, "los")) v->los = f;
    else if (ieq(key, "xlos")) { v->xlos = f; have_xlos = 1; }
    else if (ieq(key, "bowdeg")) v->bow = RAD(f);
    else if (ieq(key, "awlaft")) awlaft = f;
    else if (ieq(key, "skeg")) {
      char *F[2];
      if (split(val, F, 2) != 2 || v->nskeg >= MAX_SKEG) { snprintf(err, (size_t)errn, "line %d: skeg=x,y (2 at most)", ln); return -1; }
      v->skx[v->nskeg] = (float)atof(F[0]); v->sky[v->nskeg] = (float)atof(F[1]); v->nskeg++;
    }
    else if (ieq(key, "gen")) {
      char *F[2];
      if (split(val, F, 2) != 2 || atoi(F[0]) < 1 || atoi(F[0]) > MAX_GRP) {
        snprintf(err, (size_t)errn, "line %d: gen=group,kW", ln); return -1; }
      v->gen[atoi(F[0])] = (float)atof(F[1]);
    }
    else if (ieq(key, "thr")) {
      if (v->nthr >= MAX_THR) { snprintf(err, (size_t)errn, "line %d: at most %d thrusters", ln, MAX_THR); return -1; }
      char *F[14];
      int n = split(val, F, 14);
      if (n < 7) { snprintf(err, (size_t)errn, "line %d: thr=NAME,TYPE,x,y,kW,kN,group", ln); return -1; }
      Thruster *t = &v->thr[v->nthr];
      memset(t, 0, sizeof *t);
      strncpy(t->name, F[0], sizeof t->name - 1);
      t->type = -1;
      for (int i = 0; i < T_CNT; i++) if (ieq(F[1], DP_TYPE[i])) t->type = i;
      if (t->type < 0) { snprintf(err, (size_t)errn, "line %d: type %s? (TUN AZI AZD PROP)", ln, F[1]); return -1; }
      t->x = (float)atof(F[2]); t->y = (float)atof(F[3]);
      t->kw = (float)atof(F[4]); t->kn = (float)atof(F[5]);
      t->grp = atoi(F[6]);
      if (n > 8) { t->fz_a = wrap360((float)atof(F[7])); t->fz_b = wrap360((float)atof(F[8])); }
      if (n > 9) for (const char *o = F[9]; *o; o++) {
        char c = *o >= 'a' ? *o - 32 : *o;
        if (c == 'R') t->rudder = 1;
        if (c == 'C') t->cpp = 1;
        if (c == 'D') t->duct = 1;
        if (c == 'I') t->rounded = 1;
        if (c == 'B') t->broken = 1;
        if (c == 'P') t->pod = 1;
        if (c == 'X') t->contra = 1;
      }
      if (n > 10) t->d = (float)atof(F[10]);
      if (n > 11) t->z = (float)atof(F[11]);
      if (n > 12) t->ar = (float)atof(F[12]);
      if (n > 13) t->k1 = (float)atof(F[13]);
      if (t->kw <= 0 && t->kn <= 0) { snprintf(err, (size_t)errn, "line %d: kW or kN needed", ln); return -1; }
      v->nthr++;
    }
    else if (ieq(key, "cw") || ieq(key, "cc") || ieq(key, "cd") || ieq(key, "site") || ieq(key, "sitecur")) {
      /* the PC's level 2 data (coefficient tables, a site's weather): not used here */
    }
    else { snprintf(err, (size_t)errn, "line %d: %s?", ln, key); return -1; }
  }
  if (v->lpp <= 0 || v->b <= 0 || v->t <= 0) { snprintf(err, (size_t)errn, "lpp, b and t are needed"); return -1; }
  if (v->nthr == 0) { snprintf(err, (size_t)errn, "no thrusters"); return -1; }
  if (v->alc < 0) v->alc = v->lpp * v->t;
  if (v->los <= 0) { v->los = v->lpp; v->assumed |= VA_LOS; }
  if (!have_xlos) v->assumed |= VA_XLOS;
  if (v->bow <= 0) { v->bow = RAD(35.0f); v->assumed |= VA_BOW; }
  if (awlaft > 0) v->cwlaft = awlaft / (v->lpp / 2 * v->b);
  else { v->cwlaft = 1; v->assumed |= VA_CWL; }
  dp_prepare(v);
  return 0;
}

/* a made-up DSV, about 120 m: not any real vessel */
const char DP_EXAMPLE[] =
  "# DPCAP47 vessel file. A made-up DSV of about 120 m: not any real vessel.\n"
  "name=EXAMPLE DSV\n"
  "lpp=110\nb=22\nt=7\n"
  "afw=420\nalw=1500\nxlw=5\n"
  "alc=770\nxlc=0\n"
  "los=114\nxlos=1\nbowdeg=34\nawlaft=1150\n"
  "skeg=-38,0\n"
  "# thr=NAME,TYPE,x,y,kW,kN,group,fzFrom,fzTo,OPTS,D,z,Ar,k1\n"
  "thr=BT1,TUN,48,0,1500,0,1,0,0,,2.4,3.0\n"
  "thr=BT2,TUN,45,0,1500,0,2,0,0,,2.4,3.0\n"
  "thr=BAZ,AZD,40,0,1500,0,3,80,100,,2.6,-1.0\n"
  "thr=AZP,AZD,-50,-6,3000,0,1,0,0,,3.6,1.6\n"
  "thr=AZS,AZD,-50,6,3000,0,2,0,0,,3.6,1.6\n"
  "thr=ST,TUN,-42,0,1000,0,3,0,0,,2.0,3.0\n";

void dp_example(Vessel *v) {
  char e[64];
  dp_parse(v, DP_EXAMPLE, e, sizeof e);
}

int dp_sizeof(int what) {
  switch (what) {
  case 0: return (int)sizeof(Thruster);
  case 1: return (int)sizeof(Vessel);
  case 2: return (int)sizeof(Alloc);
  case 3: return (int)sizeof(Result);
  case 5: return (int)sizeof(AbsResult);
  case 6: return (int)sizeof(Region);
  default: return (int)sizeof(Env);
  }
}

/* ------------------------------------------------------------------ ABS: SKP, Section 9 of the DPS Guide
   The same as tools/dpcap/methods.py's abs_skp (make pccheck compares them): Table 1's weather with a fixed current,
   the weather's force (DNV level 1's formulas: ABS points to API RP 2SK / OCIMF, not in here) x (1 + margin), the ABS
   bollard pull T0 = K (P D)^(2/3) (K 848 open, 1250 ducted) less the current, EFC = 1 - (K1 / PA)^n 0.11 Vc, where
   the vessel file gives a diameter (tunnels keep the DNV level 1 thrust), no ventilation loss. SKP = the mean over
   the 36 directions of P(the limiting wind is not exceeded). */
const AbsRow ABS_TABLE1[NABS] = {
  {  0.00f,  0.00f,  0.0f,   0.0f }, {  1.28f,  5.30f,  2.5f,  14.3f }, {  1.78f,  6.26f,  5.0f,  35.7f },
  {  2.44f,  7.32f,  7.5f,  56.9f }, {  3.21f,  8.41f, 10.0f,  73.3f }, {  4.09f,  9.49f, 12.5f,  85.2f },
  {  5.07f, 10.56f, 15.0f,  92.5f }, {  6.12f, 11.61f, 17.5f,  96.6f }, {  7.26f, 12.64f, 20.0f,  98.4f },
  {  8.47f, 13.65f, 22.5f,  99.3f }, {  9.75f, 14.65f, 25.0f,  99.7f }, { 11.09f, 15.62f, 27.5f,  99.9f },
  { 12.50f, 16.58f, 30.0f,  99.9f }, { 13.97f, 17.53f, 32.5f, 100.0f }, { 15.49f, 18.46f, 35.0f, 100.0f },
};

float dp_abs_p(float w) {
  if (w >= ABS_TABLE1[NABS - 1].wind) return 100.0f;
  for (int i = 1; i < NABS; i++)
    if (w <= ABS_TABLE1[i].wind) {
      const AbsRow *a = &ABS_TABLE1[i - 1], *b = &ABS_TABLE1[i];
      return a->p + (w - a->wind) / (b->wind - a->wind) * (b->p - a->p);
    }
  return 100.0f;
}

Env dp_env_abs(float w, float cur) {
  Env e = { w, 0, 0, cur };
  if (w <= 0) return e;
  for (int i = 1; i < NABS; i++)
    if (w <= ABS_TABLE1[i].wind) {
      const AbsRow *a = &ABS_TABLE1[i - 1], *b = &ABS_TABLE1[i];
      float t = (w - a->wind) / (b->wind - a->wind);
      e.hs = a->hs + t * (b->hs - a->hs);
      e.tp = a->tp + t * (b->tp - a->tp);
      return e;
    }
  e.hs = ABS_TABLE1[NABS - 1].hs;
  e.tp = ABS_TABLE1[NABS - 1].tp;
  return e;
}

void dp_abs_prepare(Vessel *v, float cur) {
  for (int i = 0; i < v->nthr; i++) {
    Thruster *t = &v->thr[i];
    if (t->type == T_TUN || t->d <= 0 || t->kw <= 0) continue;
    int ducted = t->type == T_AZD || (t->type == T_PROP && t->duct);
    float K = ducted ? 1250.0f : 848.0f, K1 = ducted ? 400.0f : 10.0f, n = ducted ? 0.25f : 0.28f;
    float T0 = K * powf(t->kw * t->d, 2.0f / 3.0f) / 1000.0f;
    float pa = t->kw / (PI_F * t->d * t->d / 4);
    float efc = 1 - powf(K1 / pa, n) * 0.11f * cur;
    if (efc < 0) efc = 0;
    float ratio = t->tf > 0 ? t->tr / t->tf : 0;
    t->tf = T0 * efc;
    t->tr = t->tf * ratio;
  }
}

static int abs_held(const Vessel *v, uint32_t mask, float w, float from, float cur, float margin, Alloc *a) {
  Env e = dp_env_abs(w, cur);
  float keep = dp_dyn, tau[3];
  dp_dyn = 1 + margin;
  dp_force(v, &e, from, tau);
  dp_dyn = keep;
  tau[0] = -tau[0]; tau[1] = -tau[1]; tau[2] = -tau[2];
  return dp_alloc(v, mask, tau, a, dp_maxit);
}

float dp_abs_limit(const Vessel *v, uint32_t mask, float from, float cur, float margin, Alloc *a) {
  if (!abs_held(v, mask, 1e-6f, from, cur, margin, a)) return 0;
  int n = 0;
  for (int i = 1; i < NABS; i++) {
    if (abs_held(v, mask, ABS_TABLE1[i].wind, from, cur, margin, a)) n = i;
    else break;
  }
  float lo = ABS_TABLE1[n].wind, hi = n + 1 < NABS ? ABS_TABLE1[n + 1].wind : WIND_MAX;
  if (n + 1 >= NABS && abs_held(v, mask, hi, from, cur, margin, a)) return hi;
  for (int s = 0; s < 12; s++) {
    float mid = 0.5f * (lo + hi);
    if (abs_held(v, mask, mid, from, cur, margin, a)) lo = mid; else hi = mid;
  }
  return lo;
}

static float skp_of(const float *w) {
  float s = 0;
  for (int h = 0; h < NHEAD; h++) s += dp_abs_p(w[h]);
  return s / NHEAD;
}

void dp_abs(const Vessel *vin, float cur_kn, float margin, AbsResult *r, void (*progress)(int, int)) {
  static Vessel v;                                                     /* the ABS thrust: a copy */
  v = *vin;
  memset(r, 0, sizeof *r);
  r->cur_kn = cur_kn;
  r->margin = margin;
  float cur = cur_kn * 0.514444f;
  dp_abs_prepare(&v, cur);
  for (int i = 0; i < v.nthr; i++) r->tf[i] = v.thr[i].tf;
  uint32_t s0 = dp_solves, all = dp_case_mask(&v, 0);
  int ng = v.ngrp > 1 ? v.ngrp : 0, total = (1 + v.nthr + ng) * NHEAD, done = 0;
  Alloc a;
  for (int c = 0; c < 1 + v.nthr + ng; c++) {
    uint32_t m = c == 0 ? all : c <= v.nthr ? all & ~(1u << (c - 1)) : dp_case_mask(&v, c - v.nthr);
    float *w = c == 0 ? r->intact : c <= v.nthr ? r->single[c - 1] : r->group[c - v.nthr];
    memset(&a, 0, sizeof a);
    for (int h = 0; h < NHEAD && !dp_abort; h++) {
      w[h] = dp_abs_limit(&v, m, (float)(h * 10), cur, margin, &a);
      if (progress) progress(++done, total);
    }
  }
  r->a = skp_of(r->intact);
  r->b = -1; r->c = 101; r->d = -1;
  for (int i = 0; i < v.nthr; i++) {
    float s = r->skp_single[i] = skp_of(r->single[i]);
    if (s > r->b) { r->b = s; r->b_thr = i; }
    if (s < r->c) { r->c = s; r->c_thr = i; }
  }
  if (ng) {
    r->d = 101;
    for (int g = 1; g <= ng; g++) {
      float s = r->skp_group[g] = skp_of(r->group[g]);
      if (s < r->d) { r->d = s; r->d_grp = g; }
    }
  }
  r->solves = dp_solves - s0;
}
