/* navfix.c - NAV47 GPS: the position source. See navfix.h.
 * AUTO: the GPS's fix while it is younger than the timeout, then the DR from the last fix. DR: from the position
 * held (the GPS's, else SET's) with the course of the gyro (or typed, or the last COG) and the speed of the log (or
 * typed, or the last SOG): the classic DR, no current. When the fix comes back, how far the DR was is kept a while.
 * The current: the ground vector (COG, SOG) less the water vector (gyro, log or the speed typed), filtered (30 s).
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#include "navfix.h"
#include <math.h>
#include <string.h>

#define D2R (M_PI / 180)
#define FRESH 5000u

static int fresh(uint32_t t, uint32_t now, uint32_t ttl) { return t && now - t < ttl; }

void navfix_init(NavFix *f) {
  memset(f, 0, sizeof *f);
  f->sog = f->cog = f->hdg = f->stw = f->set = f->drift = f->dr_err_nm = f->dr_err_brg = NAN;
}

static void dist(double la1, double lo1, double la2, double lo2, double *nm, double *brg) {
  double dl = fmod(lo2 - lo1 + 540.0, 360.0) - 180.0;
  double x = dl * cos((la1 + la2) / 2 * D2R), y = la2 - la1;
  *nm = 60 * hypot(x, y);
  *brg = fmod(atan2(x, y) / D2R + 360.0, 360.0);
}

void navfix_update(NavFix *f, const GpsState *g, const NavFixCfg *c, double set_lat, double set_lon, uint32_t now) {
  double dt = f->t_ms ? (now - f->t_ms) / 1000.0 : 0;
  if (dt < 0 || dt > 3600) dt = 0;
  f->t_ms = now ? now : 1;
  f->hdg = g && fresh(g->hdg_ms, now, FRESH) ? g->hdg : NAN;
  f->stw = g && fresh(g->stw_ms, now, FRESH) ? g->stw : NAN;
  int prev = f->src;

  if (c->mode == POS_OFF) {
    f->src = SRC_SET; f->lat = set_lat; f->lon = set_lon; f->sog = f->cog = NAN; f->q = Q_NONE;
    return;
  }
  uint32_t to = (uint32_t)(c->timeout > 0 ? c->timeout : 10) * 1000u;
  int gps_ok = g && fresh(g->fix_ms, now, to);
  if (c->mode == POS_AUTO && gps_ok) {
    if (prev == SRC_DR) { dist(f->lat, f->lon, g->lat, g->lon, &f->dr_err_nm, &f->dr_err_brg); f->dr_err_ms = f->t_ms; }
    f->src = SRC_GPS; f->q = nmea_qual(g);
    if (!g->have_fix) f->q = Q_NONE;               /* the last fix held, inside the timeout */
    f->lat = g->lat; f->lon = g->lon;
    if (fresh(g->vel_ms, now, FRESH)) { f->sog = g->sog; f->cog = g->cog; }
    f->last_fix_ms = g->fix_ms;
    /* the current: ground - water */
    double wkn = !isnan(f->stw) ? f->stw : c->dr_kn > 0 ? c->dr_kn : NAN;
    if (!isnan(f->hdg) && !isnan(wkn) && !isnan(f->sog) && !isnan(f->cog) && fresh(g->vel_ms, now, FRESH)) {
      double e = f->sog * sin(f->cog * D2R) - wkn * sin(f->hdg * D2R);
      double n = f->sog * cos(f->cog * D2R) - wkn * cos(f->hdg * D2R);
      if (!f->have_c) { f->ce = e; f->cn = n; f->have_c = 1; }
      else { double a = 1 - exp(-dt / 30.0); f->ce += a * (e - f->ce); f->cn += a * (n - f->cn); }
      f->drift = hypot(f->ce, f->cn);
      f->set = fmod(atan2(f->ce, f->cn) / D2R + 360.0, 360.0);
    } else if (isnan(f->hdg)) { f->have_c = 0; f->set = f->drift = NAN; }
    return;
  }

  /* dead reckoning */
  if (prev != SRC_DR) {
    if (prev != SRC_GPS) { f->lat = set_lat; f->lon = set_lon; dt = 0; }   /* never a fix: from SET's position */
    else dt = f->last_fix_ms && now - f->last_fix_ms < 3600000u ? (now - f->last_fix_ms) / 1000.0 : 0;   /* from the fix's time */
  }
  f->src = SRC_DR; f->q = Q_NONE;
  double crs = NAN, kn = NAN;
  if (c->dr_crs >= 0) { crs = c->dr_crs; f->dr_crs_src = 'M'; }
  else if (!isnan(f->hdg)) { crs = f->hdg; f->dr_crs_src = 'G'; }
  else if (!isnan(f->cog)) { crs = f->cog; f->dr_crs_src = 'C'; }
  else f->dr_crs_src = 0;
  if (c->dr_kn > 0) { kn = c->dr_kn; f->dr_kn_src = 'M'; }
  else if (!isnan(f->stw)) { kn = f->stw; f->dr_kn_src = 'L'; }
  else if (!isnan(f->sog)) { kn = f->sog; f->dr_kn_src = 'S'; }
  else f->dr_kn_src = 0;
  if (!isnan(crs) && !isnan(kn) && dt > 0) {
    double d = kn * dt / 3600.0, cl = cos(f->lat * D2R);
    f->lat += d * cos(crs * D2R) / 60.0;
    f->lon += d * sin(crs * D2R) / (60.0 * (cl > 0.01 ? cl : 0.01));
    if (f->lat > 89.9) f->lat = 89.9;
    if (f->lat < -89.9) f->lat = -89.9;
    f->lon = fmod(f->lon + 540.0, 360.0) - 180.0;
  }
  f->cog = crs; f->sog = kn;
}

const char *navfix_src_name(const NavFix *f) {
  switch (f->src) {
  case SRC_GPS: return f->q == Q_NONE ? "GPS?" : nmea_qual_name(f->q);
  case SRC_DR: return "DR";
  case SRC_SET: return "SET";
  }
  return "--";
}
