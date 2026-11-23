/* ports.h - NAV47: the world's ports on the CHART, from /NAV47/PORTS.CSV (name, lat, lon, country; the web page's
 * Port to port card writes it: NGA's World Port Index). Read in pieces when the view changes, the ports in view kept
 * on the heap (150 at most): no RAM of its own. GPL-3.0-or-later. */
#ifndef NAV47_PORTS_H
#define NAV47_PORTS_H
#include "map47.h"

#define PORTS_FILE "/NAV47/PORTS.CSV"
void ports_chart(const map_t *m);            /* the ports in view: a square, their names when 40 or fewer */
int  ports_shown(void);                      /* the ports in view at the last drawing, -1 no PORTS.CSV */
#endif
