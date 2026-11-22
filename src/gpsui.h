/* gpsui.h - NAV47 GPS: the pages FIX, SATS, NAV, BOW and GPS SET, the live layer of the CHART, MOB, the anchor watch,
 * the alarms. GPL-3.0-or-later. */
#ifndef NAV47_GPSUI_H
#define NAV47_GPSUI_H
#include <stdint.h>
#include "map47.h"

enum { GP_FIX, GP_SATS, GP_NAV, GP_BOW, GP_SET, GP_N };

int  gps_init(void);                      /* 1 ok (the state on the heap) */
int  gps_poll(uint32_t now);              /* reads the NMEA, a step a second: 1 = something new (redraw) */
int  gps_anim(int page);                  /* the page animates (ms between frames), 0 no */
void gps_draw(int page, uint32_t frame);
int  gps_key(int page, int key);          /* the key used: 1; 0 not (the caller's default) */
int  gps_editing(void);                   /* typing in a field (the EDIT menu) */
void gps_cfg_write(char *o);              /* NAV47.CFG lines */
int  gps_cfg_line(const char *l);         /* 1 = a GPS line */
void gps_start(void);                     /* after the CFG: the input on if it was */
int  gps_ut(double *jd);                  /* UT from the GPS (GPS SET: UT GPS on, a fresh time): 1 */
int  gps_live(double *lat, double *lon, double *hdg);   /* the vessel's position (GPS or DR): 1 */
void gps_chart_layer(const map_t *m, uint32_t frame, int follow);   /* the live CHART: track, ship, XTE lines */
const char *gps_alarm_text(void);         /* the alarm now, NULL none */
void gps_mob(void);                       /* man overboard: the position now */

#endif
