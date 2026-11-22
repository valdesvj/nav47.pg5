/* navfix.h - NAV47 GPS: where the vessel is, from the GPS or by dead reckoning, and the current (set and drift).
 * No DMCP: the PC tests use it as it is. GPL-3.0-or-later. */
#ifndef NAV47_NAVFIX_H
#define NAV47_NAVFIX_H
#include <stdint.h>
#include "nmea.h"

enum { POS_AUTO, POS_DR, POS_OFF };                   /* the setting: GPS (DR when lost), DR only, off (SET's) */
enum { SRC_NONE, SRC_GPS, SRC_DR, SRC_SET };          /* where the position comes from now */

typedef struct {
  int    mode;              /* POS_* */
  double dr_crs;            /* the DR's course typed, degrees (< 0: the gyro, else the last COG) */
  double dr_kn;             /* the DR's speed typed, knots (<= 0: the log, else the last SOG) */
  int    timeout;           /* s without a fix before the DR starts */
} NavFixCfg;

typedef struct {
  int      src;             /* SRC_* */
  int      q;               /* the GPS's quality (Q_*) when src is GPS */
  double   lat, lon;
  double   sog, cog;        /* over the ground: the GPS's, or the DR's course and speed (NAN unknown) */
  double   hdg, stw;        /* the gyro and the log (NAN unknown / stale) */
  double   set, drift;      /* the current: towards, knots (NAN unknown); smoothed */
  char     dr_crs_src, dr_kn_src;   /* the DR's: 'G' gyro, 'C' last COG, 'M' typed; 'L' log, 'S' last SOG, 'M' typed */
  uint32_t last_fix_ms;     /* 0: never */
  double   dr_err_nm, dr_err_brg;   /* at the fix after a DR: how far the DR was (NAN none) */
  uint32_t dr_err_ms;
  /* inside */
  uint32_t t_ms;            /* the last update */
  double   ce, cn;          /* the current's filtered east and north, knots */
  int      have_c;
  int      dr_from_fix;     /* the DR started from a fix (not from SET's position) */
} NavFix;

void navfix_init(NavFix *f);
/* a step: g the GPS (NULL: none), set_lat / set_lon SET's position (the DR's start without a fix, and POS_OFF) */
void navfix_update(NavFix *f, const GpsState *g, const NavFixCfg *c, double set_lat, double set_lon, uint32_t now);
const char *navfix_src_name(const NavFix *f);         /* "DGPS" "GPS" "RTK" "DR" "SET" "--" */

#endif
