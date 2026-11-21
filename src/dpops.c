/* dpops.c - NAV47 DP OPS (see dpops.h). Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#include "dpops.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#define D2R 0.017453292519943295f
#define KN  0.514444f                   /* m/s a knot */
#define NSAMP 11                        /* points along the hull for the lee */

const char *const DPO_SIDE[DPS_N] = { "STERN", "STBD", "PORT", "BOW" };

/* a made-up vessel of the Mermaid Asiana's size (95 m x 22 m): three bow tunnels, two azimuths aft. Not her data. */
const char DPO_EXAMPLE_VESSEL[] =
  "# NAV47 DP OPS example: a made-up DP2 vessel, 95 m x 22 m (not any real vessel)\n"
  "name=EXAMPLE DP2 95M\n"
  "lpp=88\nb=22\nt=6.2\n"
  "afw=360\nalw=1250\nxlw=10\n"
  "alc=545\nxlc=0\n"
  "los=92\nxlos=1\nbowdeg=32\n"
  "gen=1,4200\ngen=2,4200\n"
  "# thr=NAME,TYPE,x,y,kW,kN,group,fzFrom,fzTo,OPTS,D,z,Ar,k1\n"
  "thr=BT1,TUN,40,0,880,0,1,0,0,,2.0,2.6\n"
  "thr=BT2,TUN,37.5,0,880,0,2,0,0,,2.0,2.6\n"
  "thr=BT3,TUN,35,0,880,0,1,0,0,,2.0,2.6\n"
  "thr=AZP,AZD,-40,-5,2200,0,1,0,0,,2.8,1.4\n"
  "thr=AZS,AZD,-40,5,2200,0,2,0,0,,2.8,1.4\n";

const char DPO_EXAMPLE_PLAT[] =
  "NAV47 DPOPS 1\n"
  "# an example: a made-up platform\n"
  "PLATFORM EXAMPLE PQ,26.7000,50.5000\n"
  "SIZE 40 30\nORIENT 030\nHEIGHT 45\nSOLID 0\n"
  "FACE AUTO\nSIDE STBD\nPARALLEL 1\nSTANDOFF 10\nTOL 30\nSHELTER 0.3 5\nLOA 95\n"
  "LIM TUN 30\n";

static float wrap(float a) { a = fmodf(a, 360.0f); return a < 0 ? a + 360 : a; }

void dpo_default(DpPlat *p) {
  memset(p, 0, sizeof *p);
  char e[8];
  dpo_parse(DPO_EXAMPLE_PLAT, p, e, sizeof e);
}

static int ieq(const char *a, const char *b) {
  while (*a && *b) if (toupper((unsigned char)*a++) != toupper((unsigned char)*b++)) return 0;
  return !*a && !*b;
}

int dpo_parse(const char *text, DpPlat *p, char *err, int errn) {
  char line[96];
  int ln = 0, have = 0;
  float keep_loa = p->loa;
  memset(p, 0, sizeof *p);
  p->len = 40; p->wid = 30; p->height = 45; p->face = -1; p->standoff = 10; p->tol = 30; p->par = 1; p->smin = 0.3f; p->slen = 5;
  p->loa = keep_loa;
  strcpy(p->name, "PLATFORM");
  err[0] = 0;
  while (*text) {
    int n = 0;
    while (*text && *text != '\n' && n < (int)sizeof line - 1) line[n++] = *text++;
    while (*text && *text != '\n') text++;
    if (*text) text++;
    line[n] = 0;
    ln++;
    char *c = strchr(line, '#');
    if (c) *c = 0;
    while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == ' ' || line[n - 1] == '\t')) line[--n] = 0;
    char *s = line;
    while (*s == ' ' || *s == '\t') s++;
    if (!*s) continue;
    char key[16];
    int k = 0;
    while (*s && *s != ' ' && *s != '\t' && k < 15) key[k++] = *s++;
    key[k] = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (ieq(key, "NAV47")) { have = 1; continue; }
    if (ieq(key, "PLATFORM")) {
      char *a = strchr(s, ','), *b = a ? strchr(a + 1, ',') : NULL;
      if (!a || !b) { snprintf(err, (size_t)errn, "line %d: PLATFORM name,lat,lon", ln); return -1; }
      *a = 0;
      snprintf(p->name, sizeof p->name, "%s", s);
      p->lat = atof(a + 1); p->lon = atof(b + 1);
    } else if (ieq(key, "SIZE")) {
      if (sscanf(s, "%f %f", &p->len, &p->wid) == 1) p->wid = p->len;   /* SIZE 40: a square */
    } else if (ieq(key, "ORIENT")) p->orient = wrap((float)atof(s));
    else if (ieq(key, "HEIGHT")) p->height = (float)atof(s);
    else if (ieq(key, "SOLID")) p->solid = atoi(s) != 0;
    else if (ieq(key, "FACE")) p->face = ieq(s, "AUTO") ? -1 : atoi(s) & 3;
    else if (ieq(key, "SIDE")) {
      p->side = DPS_STERN;
      for (int i = 0; i < DPS_N; i++) if (ieq(s, DPO_SIDE[i])) p->side = i;
    } else if (ieq(key, "STANDOFF")) p->standoff = (float)atof(s);
    else if (ieq(key, "TOL")) p->tol = (float)atof(s);
    else if (ieq(key, "PARALLEL")) p->par = atoi(s) != 0;
    else if (ieq(key, "SHELTER")) sscanf(s, "%f %f", &p->smin, &p->slen);
    else if (ieq(key, "LOA")) p->loa = (float)atof(s);
    else if (ieq(key, "VESSEL")) snprintf(p->vessel, sizeof p->vessel, "%s", s);
    else if (ieq(key, "LIM")) {
      if (p->nlim >= MAX_THR) continue;
      char nm[8] = { 0 };
      float pc = 100;
      if (sscanf(s, "%7s %f", nm, &pc) != 2) { snprintf(err, (size_t)errn, "line %d: LIM NAME %%", ln); return -1; }
      strcpy(p->limname[p->nlim], nm); p->limpct[p->nlim] = pc; p->nlim++;
    } else { snprintf(err, (size_t)errn, "line %d: %s?", ln, key); return -1; }
  }
  if (!have) { snprintf(err, (size_t)errn, "not a NAV47 DPOPS file"); return -1; }
  if (p->len < 1) p->len = 1;
  if (p->wid < 1) p->wid = 1;
  if (p->standoff < 1) p->standoff = 1;
  if (p->tol < 0) p->tol = 0;
  if (p->tol > 90) p->tol = 90;
  if (p->smin < 0) p->smin = 0;
  if (p->smin > 1) p->smin = 1;
  return 0;
}

int dpo_write(const DpPlat *p, char *buf, int n) {
  int k = snprintf(buf, (size_t)n,
    "NAV47 DPOPS 1\nPLATFORM %s,%.5f,%.5f\nSIZE %.1f %.1f\nORIENT %03.0f\nHEIGHT %.0f\nSOLID %d\n",
    p->name, p->lat, p->lon, p->len, p->wid, p->orient, p->height, p->solid);
  if (p->face < 0) k += snprintf(buf + k, (size_t)(n - k), "FACE AUTO\n");
  else k += snprintf(buf + k, (size_t)(n - k), "FACE %d\n", p->face);
  k += snprintf(buf + k, (size_t)(n - k), "SIDE %s\nPARALLEL %d\nSTANDOFF %.1f\nTOL %.0f\nSHELTER %.2f %.1f\n",
                DPO_SIDE[p->side], p->par, p->standoff, p->tol, p->smin, p->slen);
  if (p->loa > 0) k += snprintf(buf + k, (size_t)(n - k), "LOA %.1f\n", p->loa);
  if (p->vessel[0]) k += snprintf(buf + k, (size_t)(n - k), "VESSEL %s\n", p->vessel);
  for (int i = 0; i < p->nlim; i++) k += snprintf(buf + k, (size_t)(n - k), "LIM %s %.0f\n", p->limname[i], p->limpct[i]);
  return k;
}

/* the LIM lines: types first (TUN AZI AZD PROP, AZ both azimuths), then thrusters by name */
void dpo_limits(const Vessel *v, DpPlat *p) {
  for (int i = 0; i < MAX_THR; i++) p->lim[i] = 1;
  for (int pass = 0; pass < 2; pass++)
    for (int k = 0; k < p->nlim; k++)
      for (int i = 0; i < v->nthr; i++) {
        const Thruster *t = &v->thr[i];
        int ty = ieq(p->limname[k], DP_TYPE[t->type]) || (ieq(p->limname[k], "AZ") && (t->type == T_AZI || t->type == T_AZD));
        if (pass == 0 ? ty : ieq(p->limname[k], t->name)) {
          float f = p->limpct[k] / 100;
          p->lim[i] = f < 0.01f ? 0.01f : f > 1 ? 1 : f;
        }
      }
}

float dpo_tol(const DpPlat *p) { return p->par && (p->side == DPS_STBD || p->side == DPS_PORT) ? 0 : p->tol; }
float dpo_loa(const Vessel *v, const DpPlat *p) { return p->loa > 0 ? p->loa : v->lpp * 1.07f; }

void dpo_waves_auto(DpWx *w) {
  float ms = w->wkn * KN;
  int b = 0;
  while (b < NBF - 1 && DP_TABLE[b + 1].wind <= ms) b++;
  if (b >= NBF - 1) { w->hs = DP_TABLE[NBF - 1].hs; w->tp = DP_TABLE[NBF - 1].tp; return; }
  float a = DP_TABLE[b].wind, c = DP_TABLE[b + 1].wind, f = c > a ? (ms - a) / (c - a) : 0;
  w->hs = DP_TABLE[b].hs + f * (DP_TABLE[b + 1].hs - DP_TABLE[b].hs);
  w->tp = DP_TABLE[b].tp + f * (DP_TABLE[b + 1].tp - DP_TABLE[b].tp);
  if (w->hs <= 0) w->tp = 0;
}

/* ------------------------------------------------------------------ the geometry */
float dpo_face_normal(const DpPlat *p, int face) { return wrap(p->orient + 90.0f * (float)(face & 3)); }
static float face_depth(const DpPlat *p, int face) { return (face & 1) ? p->wid / 2 : p->len / 2; }

/* the heading square to the face for the side that faces it */
static float nominal(const DpPlat *p, int face) {
  static const float add[DPS_N] = { 0, 90, -90, 180 };
  return wrap(dpo_face_normal(p, face) + add[p->side]);
}

static void outline(const Vessel *v, const DpPlat *p, float o[7][2]) {   /* vessel frame: x forward, y starboard */
  float L = dpo_loa(v, p), B = v->b, h = L / 2;
  float pts[7][2] = { { -h, -B / 2 }, { h - 0.18f * L, -B / 2 }, { h - 0.06f * L, -B * 0.3f }, { h, 0 },
                      { h - 0.06f * L, B * 0.3f }, { h - 0.18f * L, B / 2 }, { -h, B / 2 } };
  memcpy(o, pts, sizeof pts);
}

static void to_world(float hdg, float px, float py, float x, float y, float *wx, float *wy) {
  float s = sinf(hdg * D2R), c = cosf(hdg * D2R);
  *wx = px + x * s + y * c;                   /* forward (sin h, cos h), starboard (cos h, -sin h) */
  *wy = py + x * c - y * s;
}

int dpo_place(const Vessel *v, const DpPlat *p, int face, float hdg, float *px, float *py) {
  float nb = dpo_face_normal(p, face) * D2R, nx = sinf(nb), ny = cosf(nb), hd = face_depth(p, face);
  float o[7][2];
  outline(v, p, o);
  /* the working point (the stern, a side's middle, the bow) on the face's centre line */
  float wpx = 0, wpy = 0, L = dpo_loa(v, p);
  switch (p->side) {
    case DPS_STERN: wpx = -L / 2; break;
    case DPS_BOW: wpx = L / 2; break;
    case DPS_STBD: wpy = v->b / 2; break;
    default: wpy = -v->b / 2; break;
  }
  float wx, wy;
  to_world(hdg, 0, 0, wpx, wpy, &wx, &wy);
  float along = wx * ny - wy * nx;            /* the working point's offset along the face: put it on the centre line */
  float x0 = -along * ny, y0 = along * nx, dmin = 1e9f;
  for (int i = 0; i < 7; i++) {
    float ax, ay;
    to_world(hdg, x0, y0, o[i][0], o[i][1], &ax, &ay);
    float d = ax * nx + ay * ny - hd;
    if (d < dmin) dmin = d;
  }
  float push = p->standoff - dmin;
  *px = x0 + push * nx; *py = y0 + push * ny;
  return 1;
}

void dpo_hull(const Vessel *v, const DpPlat *p, float hdg, float px, float py, float xy[7][2]) {
  float o[7][2];
  outline(v, p, o);
  for (int i = 0; i < 7; i++) to_world(hdg, px, py, o[i][0], o[i][1], &xy[i][0], &xy[i][1]);
}

/* where a ray from (x, y) going (ux, uy) first meets the platform (m), -1: never */
static float ray_hit(const DpPlat *p, float x, float y, float ux, float uy) {
  float o = p->orient * D2R, a0x = sinf(o), a0y = cosf(o), a1x = cosf(o), a1y = -sinf(o);
  float q[2] = { x * a0x + y * a0y, x * a1x + y * a1y }, d[2] = { ux * a0x + uy * a0y, ux * a1x + uy * a1y };
  float h[2] = { p->len / 2, p->wid / 2 }, t0 = -1e9f, t1 = 1e9f;
  for (int k = 0; k < 2; k++) {
    if (fabsf(d[k]) < 1e-9f) { if (fabsf(q[k]) > h[k]) return -1; continue; }
    float a = (-h[k] - q[k]) / d[k], b = (h[k] - q[k]) / d[k];
    if (a > b) { float t = a; a = b; b = t; }
    if (a > t0) t0 = a;
    if (b < t1) t1 = b;
  }
  if (t1 < 0 || t1 < t0) return -1;
  return t0 > 0 ? t0 : 0;
}

/* the platform's lee on the hull: the force left (0..1) and where its centre moves (m, vessel x) */
static float lee(const Vessel *v, const DpPlat *p, float hdg, float px, float py, float from, float *xc) {
  float L = dpo_loa(v, p), D = p->slen * p->height, ux = sinf(from * D2R), uy = cosf(from * D2R);
  float sw = 0, sx = 0;
  for (int i = 0; i < NSAMP; i++) {
    float X = -L / 2 + L * ((float)i + 0.5f) / NSAMP, wx, wy;
    to_world(hdg, px, py, X, 0, &wx, &wy);
    float t = D > 0 ? ray_hit(p, wx, wy, ux, uy) : -1, w = 1;
    if (t >= 0 && t < D) w = p->smin + (1 - p->smin) * t / D;
    sw += w; sx += w * X;
  }
  *xc = sw > 0 ? sx / sw : 0;
  return sw / NSAMP;
}

/* ------------------------------------------------------------------ the balance */
static int alloc(Vessel *v, uint32_t mask, const float tau[3], const float *beta, Alloc *a) {
  memset(a, 0, sizeof *a);
  return dp_alloc_b(v, mask, tau, a, dp_maxit, beta);
}

/* a thruster's force as a fraction of its full effective thrust (the ventilation loss counted) */
static float use_of(const Vessel *v, int i, const Alloc *a, const float *tf, const float *tr, const float *beta) {
  const Thruster *t = &v->thr[i];
  float m = sqrtf(a->f[i][0] * a->f[i][0] + a->f[i][1] * a->f[i][1]);
  float cap = (t->type == T_PROP && a->f[i][0] < 0 ? tr[i] : tf[i]) * (beta ? beta[i] : 1);
  return cap > 0 ? m / cap : 0;
}

void dpo_eval(Vessel *v, const DpPlat *p, const DpWx *w, int face, float hdg, DpRes *r) {
  memset(r, 0, sizeof *r);
  r->hdg = wrap(hdg); r->face = face;
  r->geo = dpo_place(v, p, face, r->hdg, &r->px, &r->py);
  float ow[9], oc[9], ov[9];
  Env ew = { w->wkn * KN, 0, 0, 0 }, ec = { 0, 0, 0, w->ckn * KN }, ev = { 0, w->hs, w->tp, 0 };
  float rw = wrap(w->wfrom - r->hdg), rc = wrap(w->cset + 180 - r->hdg), rv = wrap(w->vfrom - r->hdg);
  dp_force_parts(v, &ew, rw, ow);
  dp_force_parts(v, &ec, rc, oc);
  dp_force_parts(v, &ev, rv, ov);
  float xc;
  r->wleft = lee(v, p, r->hdg, r->px, r->py, w->wfrom, &xc);
  float fy = ow[1] * r->wleft;
  ow[0] *= r->wleft; ow[2] = ow[2] * r->wleft + fy * xc; ow[1] = fy;
  r->vleft = 1;
  if (p->solid && w->hs > 0) {
    r->vleft = lee(v, p, r->hdg, r->px, r->py, w->vfrom, &xc);
    fy = ov[7] * r->vleft;
    ov[6] *= r->vleft; ov[8] = ov[8] * r->vleft + fy * xc; ov[7] = fy;
  }
  for (int k = 0; k < 3; k++) r->env[k] = dp_dyn * (ow[k] + oc[3 + k] + ov[6 + k]);
  /* the world: forward (sin h, cos h), starboard (cos h, -sin h) */
  float s = sinf(r->hdg * D2R), c = cosf(r->hdg * D2R);
  r->fworld[0] = r->env[0] * s + r->env[1] * c;
  r->fworld[1] = r->env[0] * c - r->env[1] * s;
  r->fmag = sqrtf(r->fworld[0] * r->fworld[0] + r->fworld[1] * r->fworld[1]);
  r->fto = wrap(atan2f(r->fworld[0], r->fworld[1]) / D2R);
  float nb = dpo_face_normal(p, face) * D2R;
  r->blow = r->fworld[0] * sinf(nb) + r->fworld[1] * cosf(nb);
  /* the thrusters push against it */
  float tau[3] = { -r->env[0], -r->env[1], -r->env[2] }, beta[MAX_THR], tf[MAX_THR], tr[MAX_THR];
  dp_vent(v, &ev, rv, beta);
  for (int i = 0; i < v->nthr; i++) { tf[i] = v->thr[i].tf; tr[i] = v->thr[i].tr; }
  float lim[MAX_THR];
  for (int i = 0; i < v->nthr; i++) lim[i] = p->lim[i] > 0 ? p->lim[i] : 1;
  Alloc a;
  /* intact, inside the working limits */
  for (int i = 0; i < v->nthr; i++) { v->thr[i].tf = tf[i] * lim[i]; v->thr[i].tr = tr[i] * lim[i]; }
  r->held_lim = alloc(v, dp_case_mask(v, 0), tau, beta, &a);
  for (int i = 0; i < v->nthr; i++) { v->thr[i].tf = tf[i]; v->thr[i].tr = tr[i]; }
  if (r->held_lim) r->held_full = 1;
  else r->held_full = alloc(v, dp_case_mask(v, 0), tau, beta, &a);   /* what she needs beyond the limits */
  r->worst = 0; r->worst_thr = 0;
  for (int i = 0; i < v->nthr; i++) { r->f[i][0] = a.f[i][0]; r->f[i][1] = a.f[i][1]; }
  for (int i = 0; i < v->nthr; i++) {
    r->use[i] = use_of(v, i, &a, tf, tr, beta);
    float q = r->use[i] / lim[i];
    if (q > r->worst) { r->worst = q; r->worst_thr = i; }
  }
  /* each single failure: a redundancy group lost, the others at full thrust */
  r->fail_held = 1; r->fail_grp = 0; r->fail_use = 0;
  int worst_bad = 0;
  for (int g = 1; g <= MAX_GRP; g++) {
    int any = 0;
    for (int i = 0; i < v->nthr; i++) if (v->thr[i].grp == g) any = 1;
    if (!any) continue;
    int ok = alloc(v, dp_case_mask(v, g), tau, beta, &a);
    float mx = 0;
    for (int i = 0; i < v->nthr; i++) { float u = use_of(v, i, &a, tf, tr, beta); if (u > mx) mx = u; }
    if (!ok) mx += 1 + a.res;                                     /* not held: worse than any held case */
    if (!ok) r->fail_held = 0;
    if (!r->fail_grp || mx > r->fail_use) {
      r->fail_grp = g; r->fail_use = mx; worst_bad = !ok;
      for (int i = 0; i < v->nthr; i++) { r->fail_wuse[i] = use_of(v, i, &a, tf, tr, beta); r->ff[i][0] = a.f[i][0]; r->ff[i][1] = a.f[i][1]; }
    }
  }
  if (r->fail_use > 1 && !worst_bad) r->fail_use = 1;
  r->rank = r->held_lim ? (r->fail_held ? 3 : 2) : r->held_full ? 1 : 0;
}

static float score(const DpRes *r) { return (float)r->rank * 10 - r->worst - (r->rank < 3 ? r->fail_use : 0) * 0.1f + (r->blow > 0 ? 0.02f : 0); }

int dpo_best(Vessel *v, const DpPlat *p, const DpWx *w, float step, DpRes *best, void (*progress)(int, int)) {
  int f0 = p->face < 0 ? 0 : p->face, f1 = p->face < 0 ? 3 : p->face, n = 0, tot;
  if (step < 1) step = 1;
  float tol = dpo_tol(p);
  int nh = (int)(2 * tol / step) + 1;
  tot = nh * (f1 - f0 + 1);
  float bs = -1e9f;
  DpRes r;
  memset(best, 0, sizeof *best);
  for (int f = f0; f <= f1; f++)
    for (int k = 0; k < nh; k++) {
      float h = nominal(p, f) - tol + step * (float)k;
      dpo_eval(v, p, w, f, h, &r);
      float sc = score(&r) - fabsf(step * (float)k - tol) * 1e-4f;   /* ties: nearest square to the face */
      if (sc > bs) { bs = sc; *best = r; }
      n++;
      if (progress) progress(n, tot);
      if (dp_abort) return n;
    }
  return n;
}

float dpo_limit_wind(Vessel *v, const DpPlat *p, const DpWx *w, int face, float hdg) {
  /* the wind raised from its direction with its sea (DNV Table 2-1), the current kept; up to 64 kn (Beaufort 11) */
  DpWx x = *w;
  DpRes r;
  float lo = 0, hi = DPO_WIND_TOP;
  x.wkn = hi; dpo_waves_auto(&x);
  dpo_eval(v, p, &x, face, hdg, &r);
  if (r.held_lim) return hi;
  x.wkn = 0; dpo_waves_auto(&x);
  dpo_eval(v, p, &x, face, hdg, &r);
  if (!r.held_lim) return 0;
  for (int i = 0; i < 10; i++) {
    x.wkn = (lo + hi) / 2; dpo_waves_auto(&x);
    dpo_eval(v, p, &x, face, hdg, &r);
    if (r.held_lim) lo = x.wkn; else hi = x.wkn;
  }
  return lo;
}
