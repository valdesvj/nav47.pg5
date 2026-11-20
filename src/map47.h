/* map47.h - NAV47 CHART: Mercator on the ellipsoid, the coastline (COAST_x.BIN from tools/prep_coast.py) streamed
 * from the disk in 512 B chunks, and the route drawn over it. Float maths (the M33's FPU), double only for geodesics.
 * GPL-3.0-or-later. */
#ifndef NAV47_MAP47_H
#define NAV47_MAP47_H
#include "geodesic.h"

typedef struct {
  int   x0, y0, w, h;      /* the chart's box on the LCD */
  float lon0, psi0;        /* the centre: longitude (deg, may run past 180) and isometric latitude (rad) */
  float k;                 /* pixels per radian of longitude */
  float e;                 /* the ellipsoid's eccentricity */
} map_t;

enum { MAP_SOLID, MAP_THICK, MAP_DASH, MAP_DOT };

void  map_init(map_t *m, int x0, int y0, int w, int h, const struct geod_geodesic *g);
float map_psi(const map_t *m, double lat);                  /* isometric latitude, lat clamped to +-85 */
double map_lat(const map_t *m, float psi);                  /* its inverse */
void  map_xy(const map_t *m, double lat, double lon, float *x, float *y);
void  map_fit(map_t *m, double lat_s, double lat_n, double lon_w, double lon_e);   /* lon_w < lon_e, unrolled */
void  map_zoom(map_t *m, float f);                          /* f > 1 zooms in */
void  map_pan(map_t *m, float fx, float fy);                /* in parts of the box: +x east, +y north */
float map_span_lon(const map_t *m);                         /* the width in degrees of longitude */

/* a line straight on the chart (a rhumb line, a parallel); the longitudes are taken the short way round */
void  map_seg(const map_t *m, double la1, double lo1, double la2, double lo2, int style);
/* a geodesic, drawn as short straight pieces */
void  map_geod(const map_t *m, const struct geod_geodesic *g, double la1, double lo1, double la2, double lo2, int style);
void  map_mark(const map_t *m, double lat, double lon, int kind);   /* 0 a dot, 1 a box, 2 a ring */
int   map_grid(const map_t *m);                             /* graticule and labels; returns the step (deg) */
/* the coastline file: -1 no file (or no memory), else the polygons drawn */
int   map_coast(const map_t *m, const char *path);

#endif
