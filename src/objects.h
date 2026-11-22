/* objects.h - NAV47: your objects (platforms, rigs, wellheads, SPMs, wrecks, buoys, lights, ports) and their keep-out
 * zones, from /NAV47/OBJECTS.CSV: the web page's objects file (Route tab, "objects.csv"), the same columns
 *   type, name, lat, lon[, zone_m[, orient[, face_m[, side_m]]]]   or   name, lat, lon
 * (a header row may name the columns in any order; positions as 27.2 or 27 12.0N). Drawn on CHART, VOYAGE, the live
 * chart and BOW; the GPS warns inside a zone. Your file stays on your calculator. GPL-3.0-or-later. */
#ifndef NAV47_OBJECTS_H
#define NAV47_OBJECTS_H
#include "map47.h"

#define OBJ_FILE "/NAV47/OBJECTS.CSV"
#define OBJ_MAX  300

enum { OB_PLATFORM, OB_RIG, OB_WELL, OB_SPM, OB_WRECK, OB_BUOY, OB_LIGHT, OB_PORT, OB_N };

typedef struct {
  double lat, lon;
  float  zone;                 /* m, 0 none */
  float  orient, a, b;         /* the platform's outline: face 0's bearing, its length, the other side (m); orient < 0 none */
  unsigned char type;
  char   name[21];
} Obj;

int  obj_parse(const char *text);         /* replaces the list: the objects read */
int  obj_load(void);                      /* OBJ_FILE: the objects read, -1 no file */
int  obj_count(void);
const Obj *obj_get(int i);
const Obj *obj_inside(double lat, double lon);   /* the first zone the point is in, NULL none */
void obj_chart(const map_t *m);           /* the symbols, zones (dotted), outlines to scale, names when there is room */
/* BOW: proj maps a point to the screen (0 behind) */
typedef int (*obj_proj_fn)(double lat, double lon, double hdg, float *x, float *y);
void obj_bow(obj_proj_fn proj, double hdg, double lat, double lon);

#endif
