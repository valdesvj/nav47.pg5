#include <string.h>
#include <dmcp.h>
#include "geo_nav.h"

struct geod_geodesic geo_ell;
rhumb_t geo_rh;
route_t geo_route;
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
