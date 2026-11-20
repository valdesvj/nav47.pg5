#ifndef GEO_NAV_H
#define GEO_NAV_H
#include "geodesic.h"
#include "geo_ellipsoids.h"
#include "geo_rhumb.h"
#include "geo_route.h"

extern struct geod_geodesic geo_ell;   /* active ellipsoid */
extern rhumb_t geo_rh;
extern route_t geo_route;              /* generated plan, 128 waypoints */
extern int geo_ell_index;

void geo_set_ellipsoid(int e);         /* call once at start */
int  geo_save_csv(const char *path);   /* 0 = ok */
#endif
