/* passage.c - NAV47 GPS: the passage monitored on the plan. See passage.h.
 * A geodesic leg: the cross-track and along-track distances on the sphere of the same arc (the azimuths and the
 * distances from geod_inverse: well under 0.5 % off on the ellipsoid near the track), the course to steer the
 * geodesic's azimuth at the along-track point. A rhumb leg (or a parallel): on the Mercator plane of the ellipsoid
 * (x the longitude, y the isometric latitude), where it is straight; the offset scaled by the parallel's radius.
 * Bearing and distance to the waypoint: geod_inverse, exact.
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#include "passage.h"
#include <math.h>
#include <string.h>

#define D2R (M_PI / 180)
#define RE  6371008.8

void psg_init(Passage *p) {
  memset(p, 0, sizeof *p);
  p->xte_lim = 0.25; p->arr_r = 0.2; p->auto_adv = 1; p->leg = 1;
  p->next_crs = p->ttg_user = p->ttg_final = NAN;
}

static double wrap180(double a) { a = fmod(a + 540.0, 360.0) - 180.0; return a; }
static double wrap360(double a) { a = fmod(a, 360.0); return a < 0 ? a + 360 : a; }

/* the vessel against leg i: xte (m, + right), atd (m), the leg's length (m) and its course at the vessel */
static void leg_geom(const route_t *R, const struct geod_geodesic *g, int i, double lat, double lon,
                     double *xte, double *atd, double *len, double *cts) {
  const wpt_t *a = &R->w[i - 1], *b = &R->w[i];
  if (b->kind == SEG_GEOD) {
    double s12, az1, az2, s13, a13, a3;
    geod_inverse(g, a->lat, a->lon, b->lat, b->lon, &s12, &az1, &az2);
    geod_inverse(g, a->lat, a->lon, lat, lon, &s13, &a13, &a3);
    double d = (a13 - az1) * D2R, x = asin(sin(s13 / RE) * sin(d));
    double c = cos(x), t = c > 1e-12 ? cos(s13 / RE) / c : 1;
    if (t > 1) t = 1;
    if (t < -1) t = -1;
    double at = acos(t) * RE;
    if (cos(d) < 0) at = -at;
    *xte = x * RE; *atd = at; *len = s12;
    double s = at < 0 ? 0 : at > s12 ? s12 : at;
    struct geod_geodesicline l;
    double la, lo, az;
    geod_inverseline(&l, g, a->lat, a->lon, b->lat, b->lon, 0);
    geod_position(&l, s, &la, &lo, &az);
    *cts = wrap360(az);
    return;
  }
  /* rhumb: the Mercator plane, the longitudes unrolled from a's */
  double ya = rhumb_psi(&R->rh, a->lat * D2R), yb = rhumb_psi(&R->rh, b->lat * D2R), yp = rhumb_psi(&R->rh, lat * D2R);
  double xb = wrap180(b->lon - a->lon) * D2R, xp = wrap180(lon - a->lon) * D2R;
  double dx = xb, dy = yb - ya, px = xp, py = yp - ya, L = hypot(dx, dy);
  double s12, azi;
  if (rhumb_inverse(&R->rh, a->lat, a->lon, b->lat, b->lon, &s12, &azi) != 0) { s12 = b->leg_m; azi = b->crs_in; }
  *len = s12; *cts = wrap360(azi);
  if (L < 1e-12) { *xte = 0; *atd = 0; return; }
  double perp = (px * dy - py * dx) / L, along = (px * dx + py * dy) / (L * L);
  /* the foot of the perpendicular on the Mercator plane: its latitude from its isometric latitude */
  double yf = ya + along * dy, e = R->rh.e, phi = atan(sinh(yf));
  for (int k = 0; k < 12; k++) phi = atan(sinh(yf + e * atanh(e * sin(phi))));
  double flat = phi / D2R, flon = a->lon + along * xb / D2R;
  double sp = sin(phi), nu = R->g->a / sqrt(1 - e * e * sp * sp);
  *xte = perp * nu * cos(phi);                    /* the plane's scale at the foot */
  double sf, af;
  if (fabs(dy) < 1e-12) *atd = along * s12;       /* a parallel: the distance is in proportion */
  else if (rhumb_inverse(&R->rh, a->lat, a->lon, flat, flon, &sf, &af) == 0) *atd = along < 0 ? -sf : sf;
  else *atd = along * s12;
}

static void find_user(Passage *p, const route_t *R) {
  int i = p->leg, u = 0;
  while (i < R->n - 1 && !(R->w[i].flags & WPT_USER)) i++;
  p->to_user = i;
  for (int k = 0; k <= i; k++) if (R->w[k].flags & WPT_USER) u++;
  p->user_no = u;
}

void psg_start(Passage *p, const route_t *R, const struct geod_geodesic *g, double lat, double lon) {
  p->active = R->n >= 2; p->done = 0; p->alarm = 0; p->event = 0; p->leg = 1;
  if (!p->active) return;
  int best = -1;
  double bx = 1e30, dmin = 1e30;
  int near = 0;
  for (int i = 1; i < R->n; i++) {
    double x, at, len, c;
    leg_geom(R, g, i, lat, lon, &x, &at, &len, &c);
    if (at >= -0.1 * PSG_NM && at <= len + 0.1 * PSG_NM && fabs(x) < bx) { bx = fabs(x); best = i; }
  }
  for (int i = 0; i < R->n; i++) {
    double s, a1, a2;
    geod_inverse(g, lat, lon, R->w[i].lat, R->w[i].lon, &s, &a1, &a2);
    if (s < dmin) { dmin = s; near = i; }
  }
  if (best < 0 || dmin < bx) best = near + 1 < R->n ? near + 1 : R->n - 1;   /* nearer a point than any leg */
  p->leg = best;
}

void psg_step(Passage *p, const route_t *R, int dir) {
  if (R->n < 2) return;
  p->leg += dir;
  if (p->leg < 1) p->leg = 1;
  if (p->leg > R->n - 1) p->leg = R->n - 1;
  p->done = 0;
  p->active = 1;
}

void psg_update(Passage *p, const route_t *R, const struct geod_geodesic *g, double lat, double lon, double kn, double plan_kn) {
  p->alarm = 0;
  if (!p->active || R->n < 2) return;
  if (p->leg < 1) p->leg = 1;
  if (p->leg > R->n - 1) p->leg = R->n - 1;
  for (int guard = 0; guard < R->n; guard++) {
    double x, at, len, c, s, a1, a2;
    leg_geom(R, g, p->leg, lat, lon, &x, &at, &len, &c);
    const wpt_t *b = &R->w[p->leg];
    geod_inverse(g, lat, lon, b->lat, b->lon, &s, &a1, &a2);
    p->xte = x / PSG_NM; p->atd = at / PSG_NM; p->leg_nm = len / PSG_NM;
    p->cts = c; p->brg = wrap360(a1); p->dtg = s / PSG_NM;
    int arrived = !p->done && (p->dtg <= p->arr_r || p->atd >= p->leg_nm);
    if (!arrived || !p->auto_adv) { if (arrived) p->alarm |= PSG_A_ARRIVE; break; }
    if (p->leg >= R->n - 1) { p->done = 1; p->event = PSG_A_FINAL; break; }
    if (b->flags & WPT_USER) p->event = PSG_A_ARRIVE;
    p->leg++;                                       /* the next leg, and look again */
  }
  if (p->leg < R->n - 1) {
    double s, a1, a2;
    const wpt_t *b = &R->w[p->leg], *c = &R->w[p->leg + 1];
    if (c->kind == SEG_GEOD) { geod_inverse(g, b->lat, b->lon, c->lat, c->lon, &s, &a1, &a2); p->next_crs = wrap360(a1); }
    else p->next_crs = wrap360(b->rl_crs);
  } else p->next_crs = NAN;
  find_user(p, R);
  double rest = R->w[p->leg].cum_m;
  p->dtg_user = p->dtg + (R->w[p->to_user].cum_m - rest) / PSG_NM;
  p->dtg_final = p->dtg + (R->w[R->n - 1].cum_m - rest) / PSG_NM;
  if (p->done) p->dtg_user = p->dtg_final = 0;
  p->kn = kn > 0.5 ? kn : plan_kn;
  p->ttg_user = p->kn > 0.1 ? p->dtg_user / p->kn : NAN;
  p->ttg_final = p->kn > 0.1 ? p->dtg_final / p->kn : NAN;
  if (!p->done && fabs(p->xte) > p->xte_lim) p->alarm |= PSG_A_XTE;
  if (p->done) p->alarm |= PSG_A_FINAL;
}

void psg_track_point(const Passage *p, const route_t *R, const struct geod_geodesic *g, double *lat, double *lon) {
  const wpt_t *a = &R->w[p->leg - 1], *b = &R->w[p->leg];
  double s = p->atd * PSG_NM;
  if (s < 0) s = 0;
  if (s > p->leg_nm * PSG_NM) s = p->leg_nm * PSG_NM;
  if (b->kind == SEG_GEOD) {
    struct geod_geodesicline l;
    double az;
    geod_inverseline(&l, g, a->lat, a->lon, b->lat, b->lon, 0);
    geod_position(&l, s, lat, lon, &az);
  } else if (rhumb_direct(&R->rh, a->lat, a->lon, a->rl_crs, s, lat, lon) != 0) {
    *lat = b->lat; *lon = b->lon;
  }
}
