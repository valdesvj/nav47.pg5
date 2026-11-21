/* dpcap.h - DPCAP47: DP station-keeping capability after DNV-ST-0111 (Edition December 2021), level 1.
 * Plain C, no DMCP: the same file runs on the DM42n, in the PC tests and in the PC program (libdpcap.so).
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later.
 *
 * Axes: x forward from midships (Lpp/2), y to STARBOARD, z up from the base line; moments positive bow to starboard
 * (x Fy - y Fx). DNV-ST-0111 has y to port: the formulas are turned over in dpcap.c. Forces in kN, moments in kNm,
 * lengths in m. A weather direction "from" is where it comes from, relative to the bow, clockwise: 0 head on, 90 from
 * starboard (as DNV's Figure 3-3). A thrust direction is where the thrust pushes the vessel: 0 ahead, 90 to starboard
 * (DNV's actuator angle anticlockwise from ahead: ours = 360 - DNV's). */
#ifndef DPCAP_H
#define DPCAP_H
#include <stdint.h>

#define MAX_THR  12
#define MAX_GRP  6                      /* redundancy groups 1..MAX_GRP */
#define MAX_SKEG 2
#define NHEAD    36                     /* 10 degrees */
#define NBF      12                     /* Beaufort 0..11 */
#define NCUR     17                     /* the map: current 0 .. 2 m/s in 0.125 */
#define MAX_CASE (MAX_GRP + 1)          /* 0 intact, g: group g lost */

enum { T_TUN, T_AZI, T_AZD, T_PROP, T_CNT };   /* tunnel, azimuth/pod open, azimuth/pod ducted, shaft line propeller */

/* flags: what the file did not give, so DPCAP47 assumed */
#define TF_NO_D   1                     /* no diameter: thrust from kW x a provisional kN/kW, no ventilation loss */
#define TF_NO_AR  2                     /* a rudder without its area: no rudder force */
#define VA_LOS    1                     /* the vessel: Los = Lpp assumed */
#define VA_XLOS   2                     /*             xLos = 0 */
#define VA_BOW    4                     /*             bow angle 35 degrees */
#define VA_CWL    8                     /*             CWLaft = 1 */

typedef struct {
  char  name[8];
  int   type;
  float x, y, z;                        /* m: ahead of midships, to starboard, above the base line (3.8.3) */
  float kw;                             /* the MCR brake power in DP, kW (P_B, 3.9.2) */
  float kn;                             /* nominal thrust ahead, kN, if given (0: from kW and D) */
  int   grp;                            /* redundancy group 1.. */
  float fz_a, fz_b;                     /* a forbidden zone of thrust directions fz_a .. fz_b clockwise; equal: none */
  int   rudder, cpp, duct, rounded, broken, pod, contra;
  float d;                              /* propeller diameter, m */
  float ar, k1;                         /* rudder: area behind the propeller (m2), profile coefficient k1 (Table 3-5) */
  /* derived by dp_prepare */
  float tnom;                           /* nominal thrust ahead, kN (3.9.2) */
  float tf, tr;                         /* effective ahead (or both ways, a tunnel), astern: x beta_misc 0.9, kN */
  float cy;                             /* rudder: Cy a degree (3.10.1), 0: no rudder force */
  int   flags;
} Thruster;

typedef struct {
  char  name[32];
  float lpp, b, t;                      /* m: Lpp, breadth at the water line, summer load line draught */
  float afw, alw, xlw;                  /* wind: frontal and side areas (m2), the side area's centre (m from Lpp/2) */
  float alc, xlc;                       /* current: underwater side area (m2), its centre */
  float los, xlos;                      /* the length under water (m), the position of Los/2 (3.7) */
  float bow;                            /* bow angle, radians (3.7, Figure 3-2) */
  float cwlaft;                         /* water plane area coefficient aft of midships, 0.85 .. 1.15 */
  int   nskeg;
  float skx[MAX_SKEG], sky[MAX_SKEG];   /* the aft-most point of each skeg or gondola (x, y) */
  int   assumed;                        /* VA_ flags */
  int   nthr;
  Thruster thr[MAX_THR];
  int   ngrp;                           /* highest group number used */
  float gen[MAX_GRP + 1];               /* kW a group generates (0: no limit); 10 % is kept (3.12.3) */
} Vessel;

typedef struct { float wind, hs, tp, cur; } Env;

typedef struct {
  float f[MAX_THR][2];                  /* the allocation, kN */
  float res;                            /* residual / demand: 0 = held */
  float use[MAX_THR];                   /* |f| / effective capacity */
  float beta[MAX_THR];                  /* the ventilation loss factor used */
  int   ok;                             /* held (residual and power) */
  int   power_ok;
  int   iters;
} Alloc;

typedef struct {
  int   ncase;                          /* 1 + groups */
  signed char num[MAX_CASE][NHEAD];     /* DP capability number a heading (0..11) */
  float wind[MAX_CASE][NHEAD];          /* limiting wind, m/s (40 = more than we look at) */
  signed char wnum[NHEAD];              /* the worst single failure a heading */
  float wwind[NHEAD];
  int   A, B, C, D;                     /* -1: NA */
  uint32_t solves, iters;
} Result;

/* a thruster's possible forces: a union of convex pieces (DNV 3.10, 3.11: rudders, forbidden zones, flushing) */
#define MAXP 8
#define MAXV 44
enum { K_SEG, K_DISK, K_SECT, K_POLY };
typedef struct {
  unsigned char kind, nv;
  float a0, span, r;                    /* K_DISK: r; K_SECT: a0 .. a0 + span (<= 180 degrees), r */
  float v[MAXV][2];                     /* K_SEG: v[0] .. v[1]; K_POLY: nv corners in order (convex, with 0,0) */
} Piece;
typedef struct { int n; Piece p[MAXP]; } Region;

extern const Env DP_TABLE[NBF];         /* DNV-ST-0111 Table 2-1 */
extern const char *const DP_TYPE[T_CNT];
extern const char DP_EXAMPLE[];         /* the example vessel file */
extern float dp_dyn;                    /* dynamic factor, 1.25 at level 1 */
extern volatile int dp_abort;           /* set it to stop dp_analyse, dp_map, dp_abs at the next direction */
extern int dp_maxit;                    /* iterations an allocation may take (600) */
extern uint32_t dp_solves, dp_iters;    /* counters: allocations and their iterations */

int   dp_parse(Vessel *v, const char *text, char *err, int errn);   /* 0 ok */
void  dp_example(Vessel *v);
void  dp_prepare(Vessel *v);           /* the nominal and effective thrust (3.9), the rudders (3.10) */
Env   dp_env_wind(float wind);         /* Table 2-1 between its steps (Hs, Tp, current by the wind) */
void  dp_force(const Vessel *v, const Env *e, float from_deg, float tau[3]);   /* 3.5 - 3.7, x the dynamic factor */
void  dp_force_parts(const Vessel *v, const Env *e, float from_deg, float out[9]);   /* wind, current, waves; x 1 */
void  dp_vent(const Vessel *v, const Env *e, float from_deg, float beta[MAX_THR]);   /* 3.9.4, a thruster */
const Region *dp_regions(const Vessel *v, uint32_t mask);   /* the sets, for the working thrusters in mask */
/* allocate tau, the force the thrusters must give: mask = the thrusters that work (bit i); beta: each one's
   ventilation loss (NULL: none). a: warm start in, result out. dp_alloc: no ventilation (the ABS method, tests). */
int   dp_alloc_b(const Vessel *v, uint32_t mask, const float tau[3], Alloc *a, int maxit, const float *beta);
int   dp_alloc(const Vessel *v, uint32_t mask, const float tau[3], Alloc *a, int maxit);
uint32_t dp_case_mask(const Vessel *v, int c);   /* c 0 intact, g: group g lost */
int   dp_held(const Vessel *v, uint32_t mask, const Env *e, float from_deg, Alloc *a);
int   dp_number(const Vessel *v, uint32_t mask, float from_deg, Alloc *a);
float dp_limit_wind(const Vessel *v, uint32_t mask, float from_deg, float cur_override, int num, Alloc *a);
/* the whole level 1 analysis; progress(done, total) may be NULL */
void  dp_analyse(const Vessel *v, Result *r, void (*progress)(int, int));
/* the map: limiting wind a heading (36) and current (0..2 m/s, NCUR), map[h * NCUR + c] */
void  dp_map(const Vessel *v, uint32_t mask, float *map, void (*progress)(int, int));

/* ABS Guide for Dynamic Positioning Systems, Section 9: station keeping performance, SKP(a, b, c, d, e, f) */
#define NABS 15
typedef struct { float hs, tp, wind, p; } AbsRow;   /* Table 1, North Sea: Hs m, Tp s, wind m/s 1-min mean, P % */
extern const AbsRow ABS_TABLE1[NABS];
typedef struct {
  float cur_kn, margin;                 /* e: current (kn); the margin on the weather (0.2) */
  float intact[NHEAD];                  /* limiting wind (1-min mean, m/s) a direction */
  float single[MAX_THR][NHEAD];         /* each single thruster lost */
  float group[MAX_GRP + 1][NHEAD];      /* each group lost, 1.. */
  float skp_single[MAX_THR], skp_group[MAX_GRP + 1];
  float a, b, c, d;                     /* SKP %, d < 0: no groups */
  int   b_thr, c_thr, d_grp;            /* the least and the most effective thruster, the worst group */
  float tf[MAX_THR];                    /* the thrust ABS gives each (kN) */
  uint32_t solves;
} AbsResult;
float dp_abs_p(float wind);            /* Table 1: P (%) the wind (1-min) is not exceeded */
Env   dp_env_abs(float wind, float cur_ms);
void  dp_abs_prepare(Vessel *v, float cur_ms);   /* the ABS bollard pull where a diameter is given */
float dp_abs_limit(const Vessel *v, uint32_t mask, float from, float cur_ms, float margin, Alloc *a);
void  dp_abs(const Vessel *v, float cur_kn, float margin, AbsResult *r, void (*progress)(int, int));

/* the sizes of the structures, so a program on the PC (ctypes) can check its copy of them */
int   dp_sizeof(int what);             /* 0 Thruster, 1 Vessel, 2 Alloc, 3 Result, 4 Env, 5 AbsResult, 6 Region */
#endif
