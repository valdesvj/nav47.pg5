/* app.h - NAV47: what src/main.c and its page groups share (tide_ui.c, route_ui.c, dp_ui.c): the state on the heap,
 * the pages, and each file's functions and variables the others use. Not a library interface: the program's own.
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#ifndef NAV47_APP_H
#define NAV47_APP_H
#include <main.h>
#include <dmcp.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "astro.h"
#include "almdata.h"
#include "gfx.h"
#include "tide.h"
#include "tidefile.h"
#include "zones.h"
#include "c47scr.h"
#include "views.h"
#include "ntext.h"
#include "route.h"
#include "usbview.h"
#include "usbser.h"
#include "moon47.h"
#include "geo_nav.h"
#include "map47.h"
#include "dpops.h"
#include "gpsin.h"
#include "gpsui.h"
#include "navctx.h"
#include "objects.h"
#include "wpdb.h"
#include "alpha.h"
#include "ports.h"

#define DIR       "/NAV47"
#define TIDEDIR   "/NAV47/TIDES"
#define CFGFILE   "/NAV47/NAV47.CFG"
#define ROUTEFILE "/NAV47/ROUTE.TXT"
#define MENU_Y    (LCD_Y - LCD_MENU_LINES)     /* 208 */
#define LH        9                            /* tiny font line pitch */

/* ------------------------------------------------------------------ */
/*  State (heap: a DMCP5 program has 8 KB of static RAM)              */
/* ------------------------------------------------------------------ */
enum { PG_VALM, PG_VSPLIT, PG_VSKY, PG_VALLSKY, PG_VMOON, PG_VANIM, PG_VSKY2,
       PG_TIDE, PG_STATION, PG_ROUTE, PG_ZONES, PG_SET, PG_FILES, PG_INFO, PG_PICK, PG_PLAN, PG_CMP, PG_CHART, PG_VOY, PG_ETA, PG_ETAT, PG_DP, PG_DPIN, PG_DPABS,
       PG_GFIX, PG_GSATS, PG_GNAV, PG_GBOW, PG_GSET, PG_WPDB, PG_GCOMP, PG_GDEV };
#define IS_GPS(p) ((p) >= PG_GFIX && (p) <= PG_GSET)
#define IS_VIEW(p) ((p) <= PG_VSKY2)

typedef struct {
  double lat, lon;          /* degrees, N+ E+ */
  double tz;                /* the calculator clock's offset from UT, hours */
  int    live;              /* 1: time from the clock, 0: the fixed UT below */
  double fixed_jd;          /* UT */
  double wdir, wkn;         /* wind (from), knots, for FLOW */
  int    tsrc, treg, tst;   /* tide station: source (0 public, 1 private, 2 file), region, station */
  int    tauto;             /* 1: TIDE and T&F take the stations nearest the position (AUTO) */
  int    zone, city;        /* ZONES stop and city */
  char   files[4][40];      /* loaded tide files, reloaded at start */
  int    nfiles;
  int    south;             /* MOON47: the view from the south */
  int    gc;                /* ROUTE: great circle instead of rhumb line */
  int    lt;                /* the screens' times: 1 local (the clock TZ; a tide station's own zone), 0 UT */
  int    dcauto, dsauto;    /* DP OPS: the current from FLOW, the sea from the wind (DNV); else typed: */
  double dcset, dckn, dsfrom, dhs, dtp;
  double dut1;              /* UT1 - UTC, seconds (IERS; the web page gets it): the almanac at UT1 */
} Cfg;

typedef struct { int src, reg, st; } Row;
#define MAXROWS 400
#define MAXWP   100

/* COMPASS: an azimuth's observation and the deviation cards (0 the original, 1 the corrected), in Nav (the heap) */
#define DEVN 36
typedef struct {
  int loaded;
  int man; double lat, lon;                 /* the position: 0 the GPS (else SET's), 1 typed */
  int fixed; double jd;                     /* the time: 0 now, 1 typed or taken at the gyro bearing */
  int body;                                 /* VBody id: 0 Sun, -1 Moon, -2..-5 planets, 1..58 stars */
  int has_gb, has_gh, has_ch;               /* given: the gyro bearing, the gyro heading (else the NMEA's), the compass course */
  double gbrg, ghdg, chdg;
  int sel, edit;                            /* the line; 1 typing */
  int card, cur, cedit, confirm;            /* CARD: which, its row, typing (1 heading, 2 deviation), a key to press again */
  double newh;                              /* ADD: the heading typed */
  int n[2]; double h[2][DEVN], d[2][DEVN];
  char msg[48];
} CompassS;

typedef struct {
  Cfg cfg;
  Al al;                    /* the views' almanac (c47screen.py Almanac) */
  double vjd;               /* its time */
  int valid;
  Row rows[MAXROWS];        /* the station list */
  TDay tday;
  TRegion *fr;              /* regions loaded from files */
  int nfr;
  FlowAtlas ffl;            /* atlas loaded from a file */
  int have_ffl;
  char msg[64];
  double wlat[MAXWP], wlon[MAXWP];   /* ROUTE waypoints */
  char wname[MAXWP][WP_NAME];        /* their names ("": none) */
  signed char wsail[MAXWP];          /* the leg arriving at each: 0 the route's sailing, 1 RL, 2 GC, 3 CS */
  int nwp;
  double leg_crs[MAXWP], leg_nm[MAXWP];   /* ROUTE: each leg's course and distance (cache) */
  CompassS cmp;                      /* COMPASS and its deviation cards */
} Nav;

/* main.c: the state, the time, the formats, the files, the keys */
extern Nav * N;
extern int page, menu_page, shift;
extern double toff;
extern int pick_cursor, flow_hw_step, flow_day_step;
extern int st_day_off, pick_back;
extern int cmp_edit;
extern double cmp_a;
extern char cmp_msg[32];
extern double voy_kn;
extern double voy_etd0;
extern int wp_cur, wp_edit, wp_new;
extern double wp_lat;
extern char ebuf[16];
extern int elen;
double ut_now(void);
double show_tz(void);
const char *show_name(void);
void fmt_lat(char *s, double v);
void fmt_lon(char *s, double v);
void fmt_hm(char *s, double h);
void fmt_off(char *s, double o);
void bar_at(double ut, const char *name);
void fmt_dm3(char *s, double v, int lat);
void header(const char *name);
void fmt_dmn(char *s, double deg, int ddig, char hemi);
void fmt_dmn2(char *s, double deg, int ddig, char hemi);   /* to 0.01' (the route's points) */
double parse_dm(const char *s);
char *read_text(const char *path);
void cfg_save(void);
void go(int p);
void hint(const char *s);                 /* the help line: small, at the bottom, left */
/* compass_ui.c: COMPASS (the gyro error and the deviation from a body's azimuth) and CARD (the deviation cards) */
void compass_open(int back);
void draw_compass(void);
void draw_devcard(void);
int  compass_key(int key);                /* COMPASS or CARD: 1 used */
void compass_fn(int fn);
const char **compass_menu(void);
int  compass_editing(void);
void hint_r(const char *s);               /* a short status there, right */
int digit(int key);
void step_time(double hours);
static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int imax(int a, int b) { return a > b ? a : b; }

/* tide_ui.c: the station list (STATNS), T&F (FLOW), TIDE */
const TStation *nearest_station(double lat, double lon, int want, double *nm, int *src, int *reg, int *st);
void draw_pick(void);
void pick_move(int d);
void pick_choose(void);
void pick_open(void);
const FlowAtlas *atlas(void);
const TStation *find_station(const char *name, int *region_src, int *region);
void arrow(int cx, int cy, double set, double len);
double st_local(const TStation *st, double ut, long *n, double *h);
void draw_tide(void);
void station_step(int d);
void draw_station(void);

/* route_ui.c: ROUTE, PLAN, COMPARE, CHART, VOYAGE, ETA, ETA TABLE */
extern double cs_lim;
extern int legs_dirty, plan_dirty, plan_cur;
extern char plan_msg[48];
void route_save(void);
void route_load(void);
void route_insert(double lat, double lon);
void route_delete(void);
void draw_route(void);
void wp_start_edit(void);
void key_wp(int key);
void plan_build(void);
void draw_plan(void);
void plan_nmea(void);
void plan_key(int key);
void draw_cmp(void);
void cmp_edit_key(int key);
void cmp_key(int key);
extern int chart_back, chart_refit, chart_globe;
extern int chart_live, chart_follow;
extern uint32_t anim_frame;
void chart_open(int back);
void draw_chart(void);
void chart_key(int key);
extern int voy_play, voy_edit;
extern double voy_etd;
void draw_voy(void);
void voy_edit_key(int key);
void voy_key(int key);
extern int eta_edit, eta_keep_eta;
double etd_now(void);
void draw_eta(void);
void eta_edit_key(int key);
void eta_key(int key);
extern double etat_base;
extern int etat_sel;
void draw_etat(void);
void etat_key(int key);
void voy_tick(void);
void voy_open(int back);
int  leg_sail(int i);                     /* the sailing of the leg arriving at waypoint i: 0 RL 1 GC 2 CS */
void leg_sail_cycle(int i);               /* that leg: the route's -> RL -> GC -> CS -> the route's */
extern char route_name[24];
int  rlib_active(void);                   /* the routes' library asks (ROUTE: SAVE's name, replace?, NEW?) */
int  rlib_key(int key);
int  rlib_draw(void);
const char *const *rlib_menu(void);
void rlib_save_start(void);
void rlib_new_start(void);
void rlib_load(void);

/* dp_ui.c: DP OPS, ABS SKP, INPUT */
void dp_enter(void);
void draw_dp(void);
void dp_key(int key);
void dp_soft(int fn);
void draw_dpabs(void);
void dpabs_key(int key);
void draw_dpin(void);
void dpin_key(int key);
int dp_editing(void);                 /* INPUT is typing a value (the EDIT menu) */

#endif
