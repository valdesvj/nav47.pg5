/* Host test driver: prints machine-readable results checked by verify.py */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "geodesic.h"
#include "geo_ellipsoids.h"
#include "geo_rhumb.h"
#include "geo_route.h"

static void put(const char *line, void *ctx) { printf("%s%s\n", (const char *)ctx, line); }

static route_t R;

int main(void)
{
  struct geod_geodesic g;
  rhumb_t rh;
  int e, i, rc;
  static const double cases[][4] = {
    { 40.64, -73.78, 51.47, -0.46 },     /* JFK - LHR */
    { -33.92, 18.42, -37.82, 144.96 },   /* Cape Town - Melbourne */
    { 10.0, 20.0, 10.0, 60.0 },          /* parallel */
    { 10.0, 20.0, 10.0000001, 60.0 },    /* nearly parallel */
    { 0.0, 0.0, 0.0, 179.0 },            /* equator */
    { 60.0, 5.0, 61.0, -170.0 },         /* across the antimeridian */
    { -89.0, 0.0, 89.0, 10.0 },          /* near pole to pole */
    { 25.0, 50.0, 26.5, 56.3 },          /* Gulf */
  };

  /* 1. rhumb inverse + direct round trip on every ellipsoid */
  for (e = 0; e < ELL_COUNT; e++) {
    double rf = ellipsoids[e].rf;
    geod_init(&g, ellipsoids[e].a, rf ? 1.0 / rf : 0.0);
    rhumb_init(&rh, &g);
    for (i = 0; i < (int)(sizeof cases / sizeof cases[0]); i++) {
      double s, a, la, lo;
      rhumb_inverse(&rh, cases[i][0], cases[i][1], cases[i][2], cases[i][3], &s, &a);
      rc = rhumb_direct(&rh, cases[i][0], cases[i][1], a, s, &la, &lo);
      printf("RH,%d,%d,%.17g,%.17g,%.17g,%.17g,%d\n", e, i, s, a, la,
             remainder(lo, 360.0), rc);
    }
  }

  geod_init(&g, ellipsoids[ELL_WGS84].a, 1.0 / ellipsoids[ELL_WGS84].rf);

  /* 2. composite Cape Town -> Melbourne, limit 45S, split every 10 deg */
  route_init(&R, &g);
  rc = route_add_leg(&R, -33.92, 18.42, -37.82, 144.96, SAIL_COMPOSITE, 45.0,
                     SPLIT_DLON, 10.0);
  route_finish(&R);
  printf("RC,composite,%d,%d\n", rc, R.n);
  for (i = 0; i < R.n; i++)
    printf("CW,%d,%.15f,%.15f,%.6f,%.6f,%.6f,%.6f,%c,%d\n", i, R.w[i].lat,
           R.w[i].lon, R.w[i].leg_m, R.w[i].cum_m, R.w[i].crs_in,
           R.w[i].crs_out, R.w[i].kind, R.w[i].flags);
  route_csv(&R, put, "CSV,");
  route_nmea(&R, "CPTMEL", put, "NMEA,");

  /* 3. plain geodesic JFK -> LHR, split into 100 NM legs */
  route_init(&R, &g);
  rc = route_add_leg(&R, 40.64, -73.78, 51.47, -0.46, SAIL_GEOD, 0,
                     SPLIT_DIST, 100 * NM);
  route_finish(&R);
  printf("RC,dist,%d,%d\n", rc, R.n);
  for (i = 0; i < R.n; i++)
    printf("DW,%d,%.15f,%.15f,%.6f,%.6f\n", i, R.w[i].lat, R.w[i].lon,
           R.w[i].leg_m, R.w[i].cum_m);

  /* 4. multi-leg passage: rhumb + geodesic in 4 legs, across 180 */
  route_init(&R, &g);
  rc = route_add_leg(&R, 35.0, 140.0, 40.0, 170.0, SAIL_RHUMB, 0, SPLIT_NONE, 0);
  rc |= route_add_leg(&R, 0, 0, 48.0, -125.0, SAIL_GEOD, 0, SPLIT_LEGS, 4);
  route_finish(&R);
  printf("RC,multi,%d,%d\n", rc, R.n);
  for (i = 0; i < R.n; i++)
    printf("MW,%d,%.15f,%.15f,%.6f,%.6f,%c\n", i, R.w[i].lat, R.w[i].lon,
           R.w[i].leg_m, R.w[i].cum_m, R.w[i].kind);

  /* 5. rhumb into the pole must be refused */
  geod_init(&g, 6378137.0, 1 / 298.257223563);
  rhumb_init(&rh, &g);
  { double la, lo; rc = rhumb_direct(&rh, 80, 0, 45, 2.0e6, &la, &lo);
    printf("POLE,%d\n", rc); }
  printf("SIZEOF,route_t,%u\n", (unsigned)sizeof(route_t));
  return 0;
}
