#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <dmcp.h>
#include "geo_nav.h"

struct geod_geodesic geo_ell;
rhumb_t geo_rh;
route_t *geo_routep;

int geo_route_alloc(void)
{
  if (!geo_routep) geo_routep = (route_t *)calloc(1, sizeof(route_t));
  return geo_routep != NULL;
}
int geo_ell_index = ELL_WGS84;

void geo_set_ellipsoid(int e)
{
  double rf;
  if (e < 0 || e >= ELL_COUNT) e = ELL_WGS84;
  geo_ell_index = e;
  rf = ellipsoids[e].rf;
  geod_init(&geo_ell, ellipsoids[e].a, rf ? 1.0 / rf : 0.0);
  rhumb_init(&geo_rh, &geo_ell);
}

static void put_file(const char *s, void *ctx)
{
  UINT bw;
  f_write((FIL *)ctx, s, strlen(s), &bw);
  f_write((FIL *)ctx, "\r\n", 2, &bw);
}

int geo_save_csv(const char *path)
{
  FIL f;
  int rc = -1;
  if (!geo_routep) return -1;
  sys_disk_write_enable(1);
  if (f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
    const char *b = strrchr(path, '/');      /* folder missing: */
    path = b ? b : path;                      /* fall back to the disk root */
    if (f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
      sys_disk_write_enable(0);
      return -1;
    }
  }
  {
    route_csv(&geo_route, put_file, &f);
    f_close(&f);
    rc = 0;
  }
  sys_disk_write_enable(0);
  return rc;
}

/* geo47 PATCH: one composite leg (geodesic - parallel - geodesic) for the ROUTE page.
 * Uses geo_route as scratch.  Falls back to the geodesic when CS is not possible. */
int geo_leg_cs(double lat1, double lon1, double lat2, double lon2, double lim,
               double *crs, double *nm)
{
  int rc = ROUTE_EFULL;
  if (geo_route_alloc()) {
    route_init(&geo_route, &geo_ell);
    rc = route_add_leg(&geo_route, lat1, lon1, lat2, lon2, SAIL_COMPOSITE, lim, SPLIT_NONE, 0);
  }
  if (rc != ROUTE_OK || !geo_routep || geo_route.n < 2) {
    double s, a1, a2;
    geod_inverse(&geo_ell, lat1, lon1, lat2, lon2, &s, &a1, &a2);
    *nm = s / NM;
    *crs = fmod(a1 + 360.0, 360.0);
    return rc;
  }
  *crs = geo_route.w[0].crs_out;
  *nm = geo_route.w[geo_route.n - 1].cum_m / NM;
  return ROUTE_OK;
}
