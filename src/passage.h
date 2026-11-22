/* passage.h - NAV47 GPS: the passage monitored on the plan (geo47's route_t): the active leg, the cross-track error,
 * the bearing and distance to the next point, the next user waypoint and the final one, their ETAs, the arrival and
 * the advance to the next leg. No DMCP: the PC tests use it as it is. GPL-3.0-or-later. */
#ifndef NAV47_PASSAGE_H
#define NAV47_PASSAGE_H
#include "geodesic.h"
#include "geo_route.h"

#define PSG_NM 1852.0

enum { PSG_A_XTE = 1, PSG_A_ARRIVE = 2, PSG_A_FINAL = 4 };   /* alarm bits */

typedef struct {
  /* settings */
  double xte_lim;          /* nm */
  double arr_r;            /* nm: the arrival circle */
  int    auto_adv;         /* 1: the next leg at the arrival circle or past the waypoint's bisector */
  /* state */
  int    active;           /* 0: not monitoring */
  int    leg;              /* the active leg: w[leg - 1] -> w[leg] (1 .. n - 1) */
  int    done;             /* the final waypoint reached */
  /* the outputs (psg_update) */
  double xte;              /* nm, + the vessel is right of the track, - left */
  double atd;              /* nm along the leg from its start */
  double leg_nm;           /* the leg's length */
  double brg, dtg;         /* to w[leg]: degrees true, nm */
  double cts;              /* the leg's course at the vessel (the course to steer on the track) */
  double next_crs;         /* the next leg's course at w[leg] (NAN: the last) */
  int    to_user;          /* the next user waypoint (a WPT_USER point, index in w) */
  int    user_no;          /* its number among the user waypoints (1 = the departure) */
  double dtg_user, dtg_final;   /* nm along the plan */
  double kn;               /* the speed used for the times */
  double ttg_user, ttg_final;   /* hours, NAN: no speed */
  int    alarm;            /* PSG_A_* now */
  int    event;            /* PSG_A_ARRIVE / PSG_A_FINAL: set by the update that advanced (the caller beeps, clears) */
} Passage;

void psg_init(Passage *p);
/* start on the leg nearest the position (the one it is on, else the one ahead of the nearest point) */
void psg_start(Passage *p, const route_t *R, const struct geod_geodesic *g, double lat, double lon);
/* a new position: kn the speed over ground (<= 0.5: plan_kn is used for the times) */
void psg_update(Passage *p, const route_t *R, const struct geod_geodesic *g, double lat, double lon, double kn, double plan_kn);
void psg_step(Passage *p, const route_t *R, int dir);        /* NEXT (+1) / PREV (-1) by hand */
/* the point on the active leg nearest the vessel (for drawing), and the leg's course there */
void psg_track_point(const Passage *p, const route_t *R, const struct geod_geodesic *g, double *lat, double *lon);

#endif
