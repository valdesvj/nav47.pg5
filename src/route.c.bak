/* route.c - NAV47 ROUTE: the rhumb line by Mercator sailing with the meridional parts of the spheroid (Bowditch,
 * American Practical Navigator, Table 6 formula), and the great circle on the sphere (1' = 1 nm) with its initial
 * course. Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#include <math.h>
#include "route.h"

#define D2R (3.141592653589793238462643383279502884 / 180.0)

double mer_parts(double lat) {
  double s = sin(lat * D2R);
  return 7915.7045 * log10(tan((45.0 + lat / 2.0) * D2R)) - 23.0133 * s - 0.0514 * s * s * s;
}

static double dlon_min(double lon1, double lon2) {   /* the shorter way, minutes, E+ */
  double d = fmod(lon2 - lon1, 360.0);
  if (d > 180.0) d -= 360.0;
  if (d < -180.0) d += 360.0;
  return d * 60.0;
}

void leg_rl(double lat1, double lon1, double lat2, double lon2, double *crs, double *nm) {
  double dl = (lat2 - lat1) * 60.0, dlo = dlon_min(lon1, lon2);
  if (fabs(dl) < 1e-6) {                              /* parallel sailing */
    *nm = fabs(dlo) * cos(lat1 * D2R);
    *crs = dlo > 0 ? 90.0 : dlo < 0 ? 270.0 : 0.0;
    return;
  }
  double c = atan2(dlo, mer_parts(lat2) - mer_parts(lat1));
  *nm = fabs(dl / cos(c));
  *crs = fmod(c / D2R + 360.0, 360.0);
}

void leg_gc(double lat1, double lon1, double lat2, double lon2, double *crs, double *nm) {
  double p1 = lat1 * D2R, p2 = lat2 * D2R, dlo = dlon_min(lon1, lon2) / 60.0 * D2R;
  double a = sin((p2 - p1) / 2), b = sin(dlo / 2);
  double h = a * a + cos(p1) * cos(p2) * b * b;
  *nm = 2.0 * asin(sqrt(fmin(1.0, h))) / D2R * 60.0;
  double c = atan2(sin(dlo) * cos(p2), cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dlo));
  *crs = fmod(c / D2R + 360.0, 360.0);
}
