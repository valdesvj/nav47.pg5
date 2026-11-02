/* tide.h - NAV47 tides, tidal streams and the FLOW atlas: the C twin of Tide 47's python/tide47ref.py.
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#ifndef NAV47_TIDE_H
#define NAV47_TIDE_H

#define NCONS 60                      /* tidecons.c checks it */
#define NUB 7
#define NFB 10

enum { TK_TIDE = 0, TK_RECT = 1, TK_ROT = 2 };

typedef struct { unsigned char k; double H, g; } TCon;          /* constituent index, amplitude, phase */

typedef struct {
  const char *name, *datum, *src;
  double lat, lon, tz;
  int dst_us, kind;
  double z0, v0, flood, ebb;
  const TCon *hc, *hv;
  int nhc, nhv;
} TStation;

typedef struct { const char *code, *title; int n; const TStation *st; } TRegion;

typedef struct {
  const char *ref, *source;
  double lat0, lon0;
  int step_min, nlat, nlon;
  double q;
  const unsigned char *cells;        /* nlat*nlon cells x 13 hours x (u, v) codes; first byte 0 = land */
} FlowAtlas;

/* constituent tables (tidecons.c) */
extern const char *const CON_NAME[NCONS];
extern const signed char CON_DOOD[NCONS][6];
extern const signed char CON_K90[NCONS];
extern const double CON_U[NCONS][NUB];
extern const double CON_F[NCONS][NFB];
extern const double TIDE_SPEED[6];
extern const double TK_CICW, TK_SISW, TK_K1, TK_K2, TK_DMM, TK_DMF, TK_DO1, TK_DJ1, TK_DOO1, TK_DM2, TK_DK1, TK_DK2;

/* compiled data: public (tidedata_pub.c) and private (build/gen/tidedata_priv.c, may be empty) */
extern const TRegion PUB_REGIONS[];
extern const int PUB_NREGIONS;
extern const TRegion PRIV_REGIONS[];
extern const int PRIV_NREGIONS;
extern const FlowAtlas FLOW_ATLAS;
extern const int FLOW_ATLAS_PRESENT;

/* results */
typedef struct { double t, val, set; int kind; } TEvent;        /* hours local, height m or kn, set deg, +1/-1 */
typedef struct {
  double tz;
  int nev;
  TEvent ev[8];
  double curve[73];                  /* every 20 minutes, 0..24 h */
} TDay;

long  jdn(int y, int m, int d);
void  cal(long n, int *y, int *m, int *d);
int   tide_dst(const TStation *st, long n);
void  tide_day(const TStation *st, long n, int stream, TDay *out);        /* day() / sday() */
void  stream_now(const TStation *st, long n, double hour, double *val, double *set);

/* FLOW (tide47ref flow_*) */
double flow_dm(double x);
int    flow_cell(const FlowAtlas *fl, double lat, double lon, double FU[13], double FV[13]);
double flow_spring(const TStation *ref);
void   flow_pick(const TStation *ref, long n, double target, long *on, int *om);
void   flow_step(const TStation *ref, long n, int m, int d, long *on, int *om);
void   flow_cycle(const TStation *ref, long n, int m, double *t, double *h, double *rng, double *fac);
void   flow_rows(const double FU[13], const double FV[13], double f, double wdir, double wkn, double U[13], double V[13]);
void   flow_now(const double U[13], const double V[13], double off, double *u, double *v);
void   setkn(double u, double v, double *set, double *kn);

#endif
