/* zones.h - NAV47 time zones (Zones 47 data: IANA tzdata). GPL-3.0-or-later. */
#ifndef NAV47_ZONES_H
#define NAV47_ZONES_H

enum { DST_NONE = 0, DST_RULE = 1, DST_IRREGULAR = 2 };

typedef struct { int month, n, wday; double hour; } DstRule;   /* n 5 = last, wday 0 = Sunday, hour: standard time */

typedef struct {
  const char *name, *cc, *tzid;
  double dst;                       /* DST shift, hours */
  int kind;
  DstRule start, end;
} City;

typedef struct { double off; int col, n; const City *c; } Zone;

extern const char *const TZDATA_VERSION;
extern const Zone ZONES[];
extern const int NZONES;
extern const int MAP_NCOL, MAP_NROW;
extern const unsigned long long MAP_COLS[];      /* column j, bit i = row i (latitude 81 - 3i), 1 = land */

/* the offset (hours, DST included) of a city at the UT instant jd_ut; *on = 1 when DST is in force */
double city_offset(const Zone *z, const City *c, double jd_ut, int *on);
/* the stop nearest a clock offset (as ZONE47: standard, or now with DST) */
int zone_nearest(double tz, double jd_ut);

#endif
