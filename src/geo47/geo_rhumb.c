/* rhumb.c - ellipsoidal rhumb line (loxodrome) for NAV47
 *
 * Math (phi latitude, psi isometric latitude, M meridian arc):
 *   psi(phi) = asinh(tan phi) - e * atanh(e sin phi)
 *   tan(azi) = dlon / dpsi          (course is constant)
 *   s        = dM / cos(azi)
 * Written in the numerically stable form
 *   r  = dM / dpsi                  (metres per radian of psi)
 *   x  = dlon * r,  y = dM
 *   azi = atan2(x, y),  s = hypot(x, y)
 * When |dpsi| is tiny, dM/dpsi -> nu(phi) cos(phi) (radius of the parallel),
 * so the parallel-sailing case needs no special branch.
 */
#include <math.h>
#include "geo_rhumb.h"

#define DEG (3.14159265358979323846 / 180.0)
#define PSI_TINY 1e-5          /* below this use the parallel radius */

void rhumb_init(rhumb_t *r, const struct geod_geodesic *g)
{
  r->g = g;
  r->e2 = g->f * (2.0 - g->f);
  r->e = sqrt(r->e2);
}

double rhumb_psi(const rhumb_t *r, double phi)
{
  return asinh(tan(phi)) - r->e * atanh(r->e * sin(phi));
}

/* signed meridian arc from lat1 to lat2 (m), exact via GeographicLib */
static double merid(const rhumb_t *r, double lat1, double lat2)
{
  double s;
  geod_inverse(r->g, lat1, 0.0, lat2, 0.0, &s, 0, 0);
  return lat2 >= lat1 ? s : -s;
}

/* radius of the parallel at phi (rad) */
static double par_radius(const rhumb_t *r, double phi)
{
  double sp = sin(phi);
  return r->g->a * cos(phi) / sqrt(1.0 - r->e2 * sp * sp);
}

/* r = dM/dpsi with the small-dpsi fallback */
static double ratio(const rhumb_t *r, double lat1, double lat2,
                    double dM, double dpsi)
{
  if (fabs(dpsi) < PSI_TINY)
    return par_radius(r, 0.5 * (lat1 + lat2) * DEG);
  return dM / dpsi;
}

int rhumb_inverse(const rhumb_t *r, double lat1, double lon1,
                  double lat2, double lon2, double *s12, double *azi12)
{
  double dlon, dM, dpsi, k, x;

  if (fabs(lat1) > 90 || fabs(lat2) > 90) return RHUMB_BADARG;
  dM = merid(r, lat1, lat2);
  if (fabs(lat1) == 90 || fabs(lat2) == 90) {      /* rhumb to a pole = meridian */
    *s12 = fabs(dM);
    *azi12 = dM >= 0 ? 0.0 : 180.0;
    return RHUMB_OK;
  }
  dlon = remainder(lon2 - lon1, 360.0) * DEG;      /* shorter way round */
  dpsi = rhumb_psi(r, lat2 * DEG) - rhumb_psi(r, lat1 * DEG);
  k = ratio(r, lat1, lat2, dM, dpsi);
  x = dlon * k;
  *s12 = hypot(x, dM);
  *azi12 = atan2(x, dM) / DEG;
  return RHUMB_OK;
}

int rhumb_direct(const rhumb_t *r, double lat1, double lon1,
                 double azi12, double s12, double *lat2, double *lon2)
{
  double sa = sin(azi12 * DEG), ca = cos(azi12 * DEG);
  double dM = s12 * ca, Mpole, dpsi, k;

  if (fabs(lat1) > 90 || s12 < 0) return RHUMB_BADARG;
  if (fabs(azi12) == 90) ca = 0, dM = 0;           /* exact parallel */
  if (fabs(azi12) == 180 || azi12 == 0) sa = 0;    /* exact meridian */

  Mpole = merid(r, lat1, dM >= 0 ? 90.0 : -90.0);  /* signed distance to pole */
  if (fabs(dM) > fabs(Mpole) || (fabs(dM) == fabs(Mpole) && sa != 0 && dM != 0))
    return RHUMB_POLE;

  geod_direct(r->g, lat1, 0.0, dM >= 0 ? 0.0 : 180.0, fabs(dM), lat2, 0, 0);
  if (fabs(*lat2) == 90) { *lon2 = lon1; return RHUMB_OK; }

  dpsi = rhumb_psi(r, *lat2 * DEG) - rhumb_psi(r, lat1 * DEG);
  k = ratio(r, lat1, *lat2, dM, dpsi);
  *lon2 = lon1 + (s12 * sa / k) / DEG;
  return RHUMB_OK;
}
