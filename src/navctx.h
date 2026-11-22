/* navctx.h - NAV47: what src/main.c lends the GPS pages (src/gpsui.c): the time, SET's position, the plan, the
 * almanac's bodies, the chart. GPL-3.0-or-later. */
#ifndef NAV47_NAVCTX_H
#define NAV47_NAVCTX_H
#include "geodesic.h"
#include "geo_route.h"
#include "views.h"

double nav_ut(void);                       /* the UT shown (JD) */
double nav_tz_shown(void);                 /* hours added for the times shown (0: UT) */
void   nav_header(const char *name);       /* the top bar */
void   nav_pos(double *lat, double *lon);  /* SET's position */
void   nav_follow(double lat, double lon); /* SET's position = this (the almanac, the tides, the zones follow) */
const route_t *nav_route(double *plan_kn); /* the plan (built if needed), NULL without a route; the plan's speed */
const struct geod_geodesic *nav_ell(void);
int    nav_bodies(VBody *b);               /* the views' 8 bodies now, at SET's position (Hc, Zn) */
void   nav_beep(void);                     /* a short beep */
void   nav_chart_live(int back);           /* the CHART, live, back to the page given */
const char *nav_wp_name(int i);            /* ROUTE's waypoint i's name, "" none */

#endif
