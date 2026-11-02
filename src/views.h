/* views.h - NAV47: Almanac 47's C47 views (python/native/c47screen21.py) on the C47 screen layer. GPL-3.0-or-later. */
#ifndef NAV47_VIEWS_H
#define NAV47_VIEWS_H
#include "astro.h"

#define NBODY 8

typedef struct {                     /* c47screen.py Almanac: one JD and position */
  double j, lat, lon;
  Sun sun;
  double moon[4];                    /* gha dec hp sd */
  double planets[5][4];              /* [1..4]: gha dec sha hp (PLN3) */
  double times[5];                   /* NTWA RISE TRAN SET NTWP (UT hours) */
  double illum, age;
  double star[59][3];                /* gha dec sha, [n] for n = 1..58 */
  unsigned char have_star[59];
} Al;

typedef struct { int id; double gha, dec, hc, zn; } VBody;   /* id 0 Sun, -1 Moon, -2..-5 planets, n star */

void al_compute(Al *al, double j, double lat, double lon);
int  vbodies(Al *al, VBody *out);                             /* the 8 bodies of the views */

void view_menu(Al *al, const char *const *items, int nitems);
void view_almanac(Al *al);
void view_chart(Al *al);
int  view_sky(Al *al, int frame);     /* frame of the name loop (0..); returns the number of frames */
void view_split(Al *al);
void view_anim(Al *al, int frames, double step, int upto);   /* frames 0..upto drawn (the trail) */
void view_allsky(Al *al);

#endif
