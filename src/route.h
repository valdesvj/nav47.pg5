/* route.h - NAV47 ROUTE: passage planning, the course and distance of each leg. GPL-3.0-or-later. */
#ifndef NAV47_ROUTE_H
#define NAV47_ROUTE_H

double mer_parts(double lat);              /* meridional parts, minutes (Bowditch, WGS84 spheroid) */
/* rhumb line (Mercator sailing) and great circle (initial course): course degrees true, distance nm */
void   leg_rl(double lat1, double lon1, double lat2, double lon2, double *crs, double *nm);
void   leg_gc(double lat1, double lon1, double lat2, double lon2, double *crs, double *nm);

#endif
