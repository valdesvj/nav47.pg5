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
/* the nearest objects: distance and bearing from the vessel, and the closest approach on her course (CPA, TCPA) */
typedef struct { const Obj *o; double nm, brg, cpa, tcpa; int warn; } ObjNear;   /* tcpa hours (< 0: opening); warn: CPA in its zone, within 30 min */
int  obj_near(double lat, double lon, double cog, double sog, double max_nm, ObjNear *out, int max);
/* BOW: the objects within 8 nm as seen from the helideck (20 m): jackets and deck modules in perspective, to their
   outline (or 50 m square), the farthest first; the view: row = hor + kper / forward (nm), column = 200 + foc * right / forward */
void obj_bow(double lat, double lon, double hdg, int hor, double kper, double foc);
#endif
