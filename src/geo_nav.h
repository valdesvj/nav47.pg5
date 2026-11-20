#ifndef GEO_NAV_H
#define GEO_NAV_H
#include "geodesic.h"
#include "geo_ellipsoids.h"
#include "geo_rhumb.h"
#include "geo_route.h"

extern struct geod_geodesic geo_ell;   /* active ellipsoid */
extern rhumb_t geo_rh;
extern route_t *geo_routep;            /* generated plan, 128 waypoints, on the heap */
#define geo_route (*geo_routep)
int  geo_route_alloc(void);           /* 1 = ready (allocates on first call) */
extern int geo_ell_index;

void geo_set_ellipsoid(int e);         /* call once at start */
int  geo_save_csv(const char *path);   /* 0 = ok */
int  geo_leg_cs(double lat1, double lon1, double lat2, double lon2, double lim,
                 double *crs, double *nm);      /* composite leg, 0 = ok */
#endif
