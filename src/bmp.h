/* bmp.h - NAV47: the vessel's pictures from BMP files on the calculator's disk. GPL-3.0-or-later. */
#ifndef NAV47_BMP_H
#define NAV47_BMP_H
#include <stdint.h>

#define BOW_FILE  "/NAV47/BOW.BMP"     /* BOW: over the bow from the bridge or the helideck; up to 400 x 150, at the bottom */
#define SHIP_FILE "/NAV47/SHIP.BMP"    /* the CHART: the vessel from above, the bow up; up to 64 x 160 */

typedef struct { int w, h, bpr; uint8_t *ink, *mask; } Pic;   /* 1 bit a pixel, the rows top-down */

int  pic_load(Pic *p, const char *path, int maxw, int maxh);   /* 1 ok, 0 no file, < 0 an error (pic_err) */
void pic_free(Pic *p);
int  pic_get(const Pic *p, const uint8_t *a, int x, int y);
const char *pic_err(int rc);

#endif
