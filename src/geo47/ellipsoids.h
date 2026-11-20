/* ellipsoids.h - reference ellipsoid presets for NAV47 (geo47 module)
 * Values: a in metres, inverse flattening rf (rf = 0 means sphere).
 */
#ifndef ELLIPSOIDS_H
#define ELLIPSOIDS_H

typedef struct {
  const char *name;   /* short label for the soft-key / menu (<= 8 chars) */
  double a;           /* equatorial radius, m */
  double rf;          /* 1/f, 0 = sphere */
} ellipsoid_t;

enum {
  ELL_WGS84 = 0, ELL_GRS80, ELL_INTL24, ELL_CLRK66, ELL_BESSEL,
  ELL_KRASS, ELL_SPHERE, ELL_CUSTOM, ELL_COUNT
};

extern ellipsoid_t ellipsoids[ELL_COUNT];   /* ELL_CUSTOM is writable */

#endif
