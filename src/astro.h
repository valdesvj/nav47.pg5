/* astro.h - NAV47 astronomy: the C twin of Almanac 47's python/native/c47astro.py.
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later.
 * Time: JD in UT1. Angles in degrees. Latitude N+, longitude E+. */
#ifndef NAV47_ASTRO_H
#define NAV47_ASTRO_H

#define ASTRO_DT 69.2                 /* TT - UT1, seconds (as Almanac 47) */

typedef struct {
  double jd, T, R, lam, eps, aries, gha, dec, sd;   /* sd arcmin */
  double args[5], dpsi, deps;                        /* nutation arguments (deg), dpsi deps (arcsec) */
} Sun;

double jd_ymd(int y, int m, int d, double h);
void   jd_to_ymd(double jd, int *y, int *m, int *d, double *h);
double day0(double jd);

void   sun_at(Sun *s, double jd);                                           /* SUNA */
void   star_pos(const Sun *s, int n, double *gha, double *dec, double *sha);  /* STAR/STR2, n = 1..58 */
void   moon_pos(const Sun *s, double *gha, double *dec, double *hp, double *sd);  /* MOON/MOO2 */
void   planet_pos(const Sun *s, int p, double lt, double *gha, double *dec, double *sha, double *hp); /* PLAN, p 1..4, lt<=0: 2 passes */
void   planet_quick(const Sun *s, int p, double *gha, double *dec, double *dist);   /* PLN3 first step */
void   moon_quick(const Sun *s, double *gha, double *dec);                           /* MOOQ */
void   sun_fast(double jd, double *gha, double *dec);                                /* SUNF */
void   hcz(double lat, double lon, double dec, double gha, double *hc, double *zn);  /* CHZ */
double hczs(double lat, double lon, double dec, double gha, double *hc, double *zn); /* returns sin Hc */

enum { EV_RISE, EV_SET, EV_NTWA, EV_NTWP, EV_CTWA, EV_CTWP, EV_TRAN, EV_COUNT };
double sun_event(double j0, double lat, double lon, int kind);   /* UT hours, 99 = none */
void   moon_phase(const Sun *s, double *illum, double *age);     /* PHAS */

/* the angle helpers of c47astro (Python semantics: floored modulo, radians()) */
double pmod(double x, double y);
double dsin(double x);
double dcos(double x);
double dtan(double x);
double dasin(double x);
double datan2(double y, double x);

#endif
