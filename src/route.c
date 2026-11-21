/* route.c - NAV47 ROUTE: course and distance of each leg on the ellipsoid (geo47 PATCH):
 * the rhumb line exact on the ellipsoid (geo_rhumb.c) and the great circle as a geodesic
 * (Karney, geographiclib-c).  mer_parts() is kept: Bowditch Table 6 formula, WGS84 spheroid.
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#include <math.h>
#include "route.h"
#include "geo_nav.h"

#define D2R (3.141592653589793238462643383279502884 / 180.0)

double mer_parts(double lat) {
  double s = sin(lat * D2R);
  return 7915.7045 * log10(tan((45.0 + lat / 2.0) * D2R)) - 23.0133 * s - 0.0514 * s * s * s;
}

void leg_rl(double lat1, double lon1, double lat2, double lon2, double *crs, double *nm) {
  double s, a;
  if (rhumb_inverse(&geo_rh, lat1, lon1, lat2, lon2, &s, &a) != RHUMB_OK) { *crs = 0; *nm = 0; return; }
  *nm = s / NM;
  *crs = fmod(a + 360.0, 360.0);
}

void leg_gc(double lat1, double lon1, double lat2, double lon2, double *crs, double *nm) {
  double s, a1, a2;
  geod_inverse(&geo_ell, lat1, lon1, lat2, lon2, &s, &a1, &a2);
  *nm = s / NM;
  *crs = fmod(a1 + 360.0, 360.0);
}
