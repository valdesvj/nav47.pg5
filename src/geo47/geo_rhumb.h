/* rhumb.h - ellipsoidal rhumb line (loxodrome) for NAV47
 *
 * Shares the active struct geod_geodesic (a, f).  Meridian arcs are taken
 * from geod_inverse()/geod_direct() along a meridian, which GeographicLib
 * solves exactly, so no separate meridian-arc series is needed.
 * Oblate or spherical ellipsoids only (0 <= f < 0.01).
 * Angles in degrees, distances in metres.
 */
#ifndef GEO_RHUMB_H
#define GEO_RHUMB_H

#include "geodesic.h"

typedef struct {
  const struct geod_geodesic *g;
  double e;           /* first eccentricity */
  double e2;
} rhumb_t;

enum { RHUMB_OK = 0, RHUMB_POLE = -1, RHUMB_BADARG = -2 };

void rhumb_init(rhumb_t *r, const struct geod_geodesic *g);

/* isometric latitude psi(phi), radians in, radians out */
double rhumb_psi(const rhumb_t *r, double phi);

/* Inverse: course azi12 (deg, constant) and distance s12 (m).
 * The shorter of the two rhumbs (|dlon| <= 180) is used. */
int rhumb_inverse(const rhumb_t *r, double lat1, double lon1,
                  double lat2, double lon2, double *s12, double *azi12);

/* Direct: lon2 is NOT reduced to [-180,180] so tracks stay continuous.
 * Returns RHUMB_POLE if the rhumb would reach or pass a pole. */
int rhumb_direct(const rhumb_t *r, double lat1, double lon1,
                 double azi12, double s12, double *lat2, double *lon2);

#endif
