/* moon47.h - NAV47: MOON47, the Moon's phase page (Almanac 47 python/moon47.py). GPL-3.0-or-later. */
#ifndef NAV47_MOON47_H
#define NAV47_MOON47_H

#define M47_LUN 29.530589

typedef struct {
  double jd, lit, age, hp, sd, elong;
  int index, south;
  double next_t[4];
  int next_k[4];                 /* 0 new, 1 first quarter, 2 full, 3 last quarter */
} Moon47;

void moon47_page(double jd, int south, Moon47 *p);
/* the disc, column dx (-r..r): up to 2 runs (a, b) of |y| rows; returns the number of runs */
int  moon47_disc_runs(int r, double age, int south, int dx, int runs[2][2]);

extern const char *const M47_NAMES[8];
extern const char *const M47_PHASES[4];

#endif
