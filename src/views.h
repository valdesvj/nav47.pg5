/* views.h - NAV47: Almanac 47's views (c47screen21.py) and MOON47 above the DMCP menu bar. GPL-3.0-or-later. */
#ifndef NAV47_VIEWS_H
#define NAV47_VIEWS_H
#include "astro.h"

#define NBODY 8

typedef struct {                     /* c47screen.py Almanac: one JD and position */
  double j, lat, lon;
  double tz;                         /* the times shown: hours added to UT (0 for UT) */
  int local;                         /* the times are local (LT), not UT: the label */
  Sun sun;
  double moon[4];                    /* gha dec hp sd */
  double planets[5][4];              /* [1..4]: gha dec sha hp (PLN3) */
  double times[5];                   /* NTWA RISE TRAN SET NTWP (hours of the shown day, UT or local) */
  double illum, age;
  double star[59][3];                /* gha dec sha, [n] for n = 1..58 */
  unsigned char have_star[59];
} Al;

typedef struct { int id; double gha, dec, hc, zn; } VBody;   /* id 0 Sun, -1 Moon, -2..-5 planets, n star */

void al_compute(Al *al, double j, double lat, double lon, double tz);   /* tz: the shown time - UT, hours */
int  vbodies(Al *al, VBody *out);                             /* the 8 bodies of the views */

/* each draws the whole area above the menu bar (rows 0-207) */
void view_almanac(Al *al);
int  view_sky(Al *al, int frame);     /* frame of the name loop (0..); returns the number of frames */
void view_split(Al *al, int *first);   /* *first: the first body in the table (8 / 2 scroll it) */
void view_anim(Al *al, int frames, double step, int upto);   /* frames 0..upto drawn (the trail) */
void view_allsky(Al *al);
void view_moon(Al *al, int south);

/* the top bar of every page: jl the shown local JD (UT + tz), local: LT or UT, the position, the page's name */
void top_bar(double jl, int local, double lat, double lon, const char *name);

#endif
