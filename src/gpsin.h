/* gpsin.h - NAV47 GPS: NMEA in on the USB serial console's channel 2 (src/sercon.c, both builds; the simulator's
 * test/sim/sercon_sim.c replays a log, NAV47_NMEA=file; the web page sends its lines there). A PC, a Pi or the web
 * page is the bridge (tools/nmea_bridge.py): it writes 0x12 + the sentence + '\n'. GPL-3.0-or-later. */
#ifndef NAV47_GPSIN_H
#define NAV47_GPSIN_H

#define GPSIN_CH 2

int  gpsin_on(void);                 /* 1 = on (TinyUSB: 0 without USB power) */
void gpsin_off(void);
int  gpsin_is_on(void);
int  gpsin_line(char *buf, int n);   /* the next sentence without its CR LF: its length, 0 none waiting */
void gpsin_counts(int *lines, int *ovf);
int  usb_any_on(void);               /* the screen to the PC or the GPS: the USB runs (no sleep in the TinyUSB build) */

#endif
