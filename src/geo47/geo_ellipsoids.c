#include "geo_ellipsoids.h"

ellipsoid_t ellipsoids[ELL_COUNT] = {
  { "WGS84",  6378137.0,   298.257223563 },
  { "GRS80",  6378137.0,   298.257222101 },
  { "INTL24", 6378388.0,   297.0         },   /* Hayford / ED50 */
  { "CLRK66", 6378206.4,   294.978698214 },   /* NAD27 */
  { "BESSEL", 6377397.155, 299.1528128   },
  { "KRASS",  6378245.0,   298.3         },
  { "SPHERE", 6371008.7714, 0.0          },   /* IUGG mean radius R1 */
  { "CUSTOM", 6378137.0,   298.257223563 },
};
