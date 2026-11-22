/* dpops.h - NAV47 DP OPS: can the vessel keep station a few metres off a platform, in today's wind, current and waves,
 * at which heading, inside the thrusters' working limits (a bow tunnel at 30 %), and after the worst single failure?
 * DPCAP47's engine (src/dp/dpcap.c: DNV-ST-0111 level 1's forces, its thrust allocation) with:
 *   - wind, current and waves from their own directions (DNV's level 1 puts them together; at a platform they are not)
 *   - the platform: a square or a rectangle, taller than the vessel (the default rule), so on some headings its lee
 *     takes the wind off part of the hull: the wind left rises from SHELTER's minimum at the platform to all of it at
 *     SHELTER's length (platform heights) downwind; a SOLID platform (a gravity base) shelters the waves the same way
 *   - the vessel placed with its working point (the middle of the starboard or port side, the stern, the bow) STANDOFF m
 *     off a face, on its centre line; parallel to the face (side to, the usual set-up) or turned within TOL degrees, then
 *     moved out along the centre line until no corner is closer than MINCLEAR; the heading with the most margin found
 *   - working limits: a thruster's thrust capped (LIM TUN 30) in the intact case; after a failure, full thrust
 * Static, as DNV's level 1 (x 1.25): not the vessel's own DP consequence analysis; planning, not operations.
 * Plain C, no DMCP: the calculator, the simulator, the web page (WebAssembly) and the PC tests share it.
 * Axes: the world x east, y north (m, the platform's centre at 0, 0); the vessel x forward, y to starboard.
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#ifndef DPOPS_H
#define DPOPS_H
#include "dp/dpcap.h"

enum { DPS_STERN, DPS_STBD, DPS_PORT, DPS_BOW, DPS_N };
extern const char *const DPO_SIDE[DPS_N];

typedef struct {
  char  name[24];
  double lat, lon;                      /* degrees */
  float len, wid;                       /* m: along ORIENT, across */
  float orient;                         /* the bearing face 0 looks to (its outward normal), degrees true */
  float height;                         /* m above the sea */
  int   solid;                          /* 1: shelters the waves too (a gravity base); 0: a jacket, the waves go through */
  int   face;                           /* the working face 0..3 (normals ORIENT + 90 k), -1 AUTO: the best */
  int   side;                           /* DPS_ */
  float standoff;                       /* m, the working point (a side's middle, the stern, the bow) off the face */
  float mincl;                          /* m, the least a corner may come to the face when the heading is turned (5) */
  float tol;                            /* degrees either side of square to the face */
  int   par;                            /* starboard or port side to: parallel to the face (the usual set-up; TOL not used) */
  float smin, slen;                     /* shelter: the wind left close in the lee (0.3), the lee's length (5 heights) */
  float loa;                            /* the vessel's length overall, m (0: Lpp x 1.07) */
  float lim[MAX_THR];                   /* working limit, a fraction of full thrust (1: none), after LIM lines */
  char  limname[MAX_THR][8];            /* LIM NAME % (NAME, a thruster, or TUN AZI AZD PROP: a type) */
  float limpct[MAX_THR];
  int   nlim;
  char  vessel[40];                     /* the vessel file, "" the example */
} DpPlat;

typedef struct {
  float wfrom, wkn;                     /* wind: from (true), knots (10-min mean at 10 m, as DNV) */
  float cset, ckn;                      /* current: setting towards (true), knots */
  float vfrom, hs, tp;                  /* waves: from (true), Hs m, Tp s (hs 0: none) */
} DpWx;

typedef struct {
  int   geo;                            /* 1: the hull is placed (0: a corner within 1 m of the face: not usable) */
  float clear;                          /* the hull's nearest point to the face, m (a turned heading: a corner, >= MINCLEAR) */
  float mid;                            /* the working point's distance off the face, m (>= STANDOFF) */
  float hdg;                            /* the heading tried, degrees true */
  int   face;
  float px, py;                         /* the vessel's midships, m from the platform's centre */
  float wleft;                          /* the wind force left after the platform's lee (1: no shelter) */
  float vleft;                          /* the waves' (solid platforms) */
  float env[3];                         /* the weather's force on the vessel, x 1.25 (kN, kNm; vessel frame) */
  float fworld[2], fmag, fto;           /* the same in the world (kN east, north), its size, where it pushes (true) */
  float blow;                           /* its part away from the face, kN: > 0 blow-off, < 0 blow-on */
  int   held_lim;                       /* intact, inside the working limits */
  int   held_full;                      /* intact, full thrust */
  float use[MAX_THR];                   /* intact: each thruster's thrust, a fraction of its full thrust */
  float worst;                          /* the highest use / limit */
  int   worst_thr;
  int   fail_held;                      /* every single failure (a redundancy group lost, full thrust) held */
  int   fail_grp;                       /* the worst group */
  float fail_use;                       /* its highest use */
  float fail_wuse[MAX_THR];             /* the thrusters' use after that failure */
  int   rank;                           /* 3 hold + failure, 2 hold in limits, 1 only beyond the limits, 0 no hold */
  float f[MAX_THR][2];                  /* intact: each thruster's force, kN (forward, starboard) */
  float ff[MAX_THR][2];                 /* after the worst failure */
} DpRes;

void dpo_default(DpPlat *p);
int  dpo_parse(const char *text, DpPlat *p, char *err, int errn);   /* DPOPS.TXT; 0 ok */
int  dpo_write(const DpPlat *p, char *buf, int n);                  /* DPOPS.TXT, the length */
void dpo_limits(const Vessel *v, DpPlat *p);                         /* the LIM lines on this vessel's thrusters */
float dpo_loa(const Vessel *v, const DpPlat *p);
float dpo_tol(const DpPlat *p);                                      /* the tolerance used: 0 parallel */
void dpo_waves_auto(DpWx *w);                                        /* Hs, Tp from DNV's Table 2-1 at the wind */
float dpo_face_normal(const DpPlat *p, int face);
int  dpo_place(const Vessel *v, const DpPlat *p, int face, float hdg, float *px, float *py);
float dpo_mid(const Vessel *v, const DpPlat *p, int face, float hdg, float px, float py);
float dpo_clear(const Vessel *v, const DpPlat *p, int face, float hdg, float px, float py);
void dpo_hull(const Vessel *v, const DpPlat *p, float hdg, float px, float py, float xy[7][2]);   /* the outline, world m */
void dpo_eval(Vessel *v, const DpPlat *p, const DpWx *w, int face, float hdg, DpRes *r);
int  dpo_best(Vessel *v, const DpPlat *p, const DpWx *w, float step, DpRes *best, void (*progress)(int, int));
#define DPO_WIND_TOP 64.0f
float dpo_limit_wind(Vessel *v, const DpPlat *p, const DpWx *w, int face, float hdg);   /* kn, in the limits, with its sea */
/* ABS SKP (the ABS DPS Guide, Section 9): dp_abs with the vessel's copy on the heap; 0 no memory */
int  dpo_abs(const Vessel *v, float cur_kn, float margin, AbsResult *r, void (*progress)(int, int));
extern const char DPO_EXAMPLE_VESSEL[];
extern const char DPO_EXAMPLE_PLAT[];
#endif
