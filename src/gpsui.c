/* gpsui.c - NAV47 GPS: the pages and the live chart. See gpsui.h.
 *   FIX   where the vessel is, the quality of the fix (GPS / DGPS / RTK / DR), DOP, DGPS station, GST errors, the
 *         time against the clock; the anchor watch (7), the gyro error from the Sun's azimuth (ENTER)
 *   SATS  the satellites in the sky (one symbol a system, filled when used) and their signal bars; 0 the Sun, the
 *         Moon, the planets and the stars on the same sky
 *   NAV   the passage: XTE bar, the next point and waypoint (BRG DTG CTS ETA), the final one, SOG COG HDG, set and drift;
 *         4 6 the previous / next leg, 5 start on the nearest leg, 0 the live CHART
 *   BOW   the view over the Asiana's bow from the helideck: the route a lane (XTE limits) to the horizon, the
 *         waypoints' flags, the turn ahead, the Sun or the Moon at its bearing, the sea moving at her speed
 *   GPS SET the settings; R/S on any GPS page: MOB
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#include <main.h>
#include <dmcp.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfx.h"
#include "ntext.h"
#include "c47scr.h"
#include "nmea.h"
#include "navfix.h"
#include "passage.h"
#include "gpsin.h"
#include "gpsui.h"
#include "navctx.h"
#include "objects.h"
#include "bmp.h"

#define D2R    (M_PI / 180)
#define TOP    16
#define BOT    207                        /* the last row above the menu bar */
#define TRAIL  240
#define LHT    9                          /* the tiny font's line */

enum { A_XTE = 1, A_ARR = 2, A_FINAL = 4, A_ANCHOR = 8, A_LOST = 16, A_MOB = 32, A_ZONE = 64, A_CPA = 128 };
enum { S_INPUT, S_MODE, S_FOLLOW, S_UT, S_TIMEOUT, S_DRCRS, S_DRKN, S_XTE, S_ARR, S_ADV, S_ANCR, S_ALARMS, S_MOB, S_N };

typedef struct {
  GpsState g;
  NavFix   f;
  Passage  p;
  NavFixCfg fc;
  int      input, follow, use_ut, alarms;
  double   anc_r;                          /* m */
  float    tla[TRAIL], tlo[TRAIL];         /* the track: a point every 10 s (ring) */
  int      tn, th;
  uint32_t trail_ms, step_ms, follow_ms, beep_ms;
  double   rot;                            /* the rate of turn, degrees a second (filtered) */
  double   last_hdg;
  int      mob; double mob_lat, mob_lon, mob_ut;
  int      anc; double anc_lat, anc_lon;
  int      alarm, ack, event;              /* A_* now, acknowledged, the one-shot (ARR, FINAL) */
  uint32_t event_ms;
  char     zone_name[24], cpa_name[24];
  double   cpa_min;
  int      sel, edit, elen;                /* GPS SET: the field, typing */
  char     ebuf[12];
  int      sat_sys, stars;                 /* SATS: the bars of one system (GNSS_N: all), the sky's bodies */
  int      gyro_edit; double gyro_obs, gyro_zn; int gyro_have;   /* FIX: the Sun's bearing by the gyro */
  int      had_route;
} Gps;
static Gps *P;

/* ------------------------------------------------------------------ the state, the settings */
int gps_init(void) {
  if (P) return 1;
  P = (Gps *)calloc(1, sizeof *P);
  if (!P) return 0;
  nmea_init(&P->g);
  navfix_init(&P->f);
  psg_init(&P->p);
  P->fc.mode = POS_AUTO; P->fc.dr_crs = -1; P->fc.dr_kn = 0; P->fc.timeout = 10;
  P->alarms = 1; P->anc_r = 80; P->sat_sys = GNSS_N; P->edit = -1;
  P->last_hdg = NAN;
#ifdef NAV47_WEB
  P->input = 1;                                        /* the web page: its GPS tab feeds the program, input on */
#endif
  return 1;
}

void gps_cfg_write(char *o) {
  if (!P) return;
  sprintf(o, "GPS %d %d %d %d %d %.1f %.2f %d\nPSG %.3f %.3f %d %.0f\n", P->input, P->fc.mode, P->follow, P->use_ut,
          P->fc.timeout, P->fc.dr_crs, P->fc.dr_kn, P->alarms, P->p.xte_lim, P->p.arr_r, P->p.auto_adv, P->anc_r);
}

int gps_cfg_line(const char *l) {
  if (!P) return 0;
  if (!strncmp(l, "GPS ", 4)) {
    sscanf(l + 4, "%d %d %d %d %d %lf %lf %d", &P->input, &P->fc.mode, &P->follow, &P->use_ut, &P->fc.timeout,
           &P->fc.dr_crs, &P->fc.dr_kn, &P->alarms);
    if (P->fc.mode < 0 || P->fc.mode > POS_OFF) P->fc.mode = POS_AUTO;
    return 1;
  }
  if (!strncmp(l, "PSG ", 4)) {
    sscanf(l + 4, "%lf %lf %d %lf", &P->p.xte_lim, &P->p.arr_r, &P->p.auto_adv, &P->anc_r);
    return 1;
  }
  return 0;
}

void gps_start(void) {
  if (P && P->input && !gpsin_on()) P->input = 0;
  static int pics;
  if (!pics) { pics = 1; gps_pics_load(NULL, 0); }
}

int gps_ut(double *jd) {
  if (!P || !P->use_ut || !P->g.utc_ms || sys_current_ms() - P->g.utc_ms > 5000) return 0;
  if (!nmea_jd(&P->g, jd)) return 0;
  *jd += (sys_current_ms() - P->g.utc_ms) / 86400000.0;
  return 1;
}

int gps_live(double *lat, double *lon, double *hdg) {
  if (!P || P->f.src == SRC_NONE || P->f.src == SRC_SET) return 0;
  *lat = P->f.lat; *lon = P->f.lon;
  *hdg = !isnan(P->f.hdg) ? P->f.hdg : !isnan(P->f.cog) ? P->f.cog : 0;
  return 1;
}

static double nm_brg(double la1, double lo1, double la2, double lo2, double *brg) {
  double s, a1, a2;
  geod_inverse(nav_ell(), la1, lo1, la2, lo2, &s, &a1, &a2);
  if (brg) *brg = fmod(a1 + 360.0, 360.0);
  return s / 1852.0;
}

void gps_mob(void) {
  if (!P) return;
  double la, lo;
  if (P->f.src == SRC_GPS || P->f.src == SRC_DR) { la = P->f.lat; lo = P->f.lon; } else nav_pos(&la, &lo);
  P->mob = 1; P->mob_lat = la; P->mob_lon = lo; P->mob_ut = nav_ut();
  P->ack &= ~A_MOB;
  nav_beep();
}

/* ------------------------------------------------------------------ a step a second */
static void alarms_step(uint32_t now) {
  int a = 0;
  if (P->p.active && (P->p.alarm & PSG_A_XTE)) a |= A_XTE;
  if (P->p.event) {                                    /* the arrival: a one-shot, shown 30 s or until ENTER */
    P->event = P->p.event == PSG_A_FINAL ? A_FINAL : A_ARR;
    P->event_ms = now; P->p.event = 0; P->ack &= ~P->event;
  }
  if (P->event && now - P->event_ms > 30000) P->event = 0;
  a |= P->event;
  if (P->anc && (P->f.src == SRC_GPS || P->f.src == SRC_DR) &&
      nm_brg(P->anc_lat, P->anc_lon, P->f.lat, P->f.lon, NULL) * 1852 > P->anc_r) a |= A_ANCHOR;
  if (P->fc.mode == POS_AUTO && P->input && P->f.src == SRC_DR && P->f.last_fix_ms) a |= A_LOST;
  if (P->mob) a |= A_MOB;
  P->zone_name[0] = 0;
  if (P->f.src == SRC_GPS || P->f.src == SRC_DR) {
    const Obj *o = obj_inside(P->f.lat, P->f.lon);
    if (o) { a |= A_ZONE; snprintf(P->zone_name, sizeof P->zone_name, "%s", o->name); }
    else {                                             /* a zone on her course, within 30 min */
      ObjNear z[4];
      int n = obj_near(P->f.lat, P->f.lon, P->f.cog, P->f.sog, 6, z, 4);
      for (int i = 0; i < n; i++)
        if (z[i].warn) { a |= A_CPA; snprintf(P->cpa_name, sizeof P->cpa_name, "%s", z[i].o->name); P->cpa_min = z[i].tcpa * 60; break; }
    }
  }
  P->ack &= a;                                         /* gone: armed again */
  int fresh = a & ~P->ack & ~P->alarm;
  if (fresh && P->alarms) nav_beep();
  else if ((a & ~P->ack & (A_ANCHOR | A_MOB | A_ZONE)) && P->alarms && now - P->beep_ms > 5000) { nav_beep(); P->beep_ms = now; }
  P->alarm = a;
}

int gps_poll(uint32_t now) {
  if (!P) return 0;
  char l[128];
  int got = 0;
  for (int k = 0; k < 48; k++) {                       /* drain channel 2 (256 bytes: three sentences) */
    if (gpsin_line(l, sizeof l) <= 0) break;
    nmea_feed(&P->g, l, now);
    got = 1;
  }
  if (P->step_ms && now - P->step_ms < 1000) return got;
  int first = !P->step_ms;
  P->step_ms = now ? now : 1;
  double sla, slo;
  nav_pos(&sla, &slo);
  navfix_update(&P->f, P->input ? &P->g : NULL, &P->fc, sla, slo, now);
  double h = !isnan(P->f.hdg) ? P->f.hdg : P->f.cog;   /* the rate of turn */
  if (!isnan(h) && !isnan(P->last_hdg)) {
    double d = fmod(h - P->last_hdg + 540.0, 360.0) - 180.0;
    P->rot += 0.4 * (d - P->rot);
  } else P->rot = 0;
  P->last_hdg = h;
  int live = P->f.src == SRC_GPS || P->f.src == SRC_DR;
  if (live && (P->tn == 0 || now - P->trail_ms >= 10000)) {
    P->tla[P->th] = (float)P->f.lat; P->tlo[P->th] = (float)P->f.lon;
    P->th = (P->th + 1) % TRAIL;
    if (P->tn < TRAIL) P->tn++;
    P->trail_ms = now;
  }
  double pk;
  const route_t *R = nav_route(&pk);
  if (R && live) {
    if (!P->had_route) { psg_start(&P->p, R, nav_ell(), P->f.lat, P->f.lon); }
    psg_update(&P->p, R, nav_ell(), P->f.lat, P->f.lon, P->f.sog, pk);
  } else if (!R) P->p.active = 0;
  P->had_route = R != NULL && live;
  if (P->follow && live && (first || now - P->follow_ms >= 60000)) { nav_follow(P->f.lat, P->f.lon); P->follow_ms = now; }
  alarms_step(now);
  return 1;
}

const char *gps_alarm_text(void) {
  static char s[48];
  if (!P) return NULL;
  int a = P->alarm & ~P->ack;
  if (!a) return NULL;
  if (a & A_MOB) { double b, d = nm_brg(P->f.lat, P->f.lon, P->mob_lat, P->mob_lon, &b); sprintf(s, "MOB %.2f nm %03.0f" NT_DEG, d, b); }
  else if (a & A_ZONE) snprintf(s, sizeof s, "IN THE ZONE: %s", P->zone_name);
  else if (a & A_ANCHOR) strcpy(s, "ANCHOR DRAGGING");
  else if (a & A_CPA) snprintf(s, sizeof s, "ZONE AHEAD: %s %.0f min", P->cpa_name, P->cpa_min);
  else if (a & A_XTE) sprintf(s, "OFF TRACK %.2f nm %s", fabs(P->p.xte), P->p.xte >= 0 ? "R" : "L");
  else if (a & A_FINAL) strcpy(s, "ARRIVED: THE FINAL WAYPOINT");
  else if (a & A_ARR) sprintf(s, "WAYPOINT: NOW TO WP%d", P->p.user_no);
  else if (a & A_LOST) strcpy(s, "NO FIX: DEAD RECKONING");
  else return NULL;
  return s;
}

/* ------------------------------------------------------------------ drawing helpers */
static void tiny(int x, int y, const char *s) { draw_text(&TINY, x, y, s, 0); }
static void tiny_r(int x, int y, const char *s) { draw_text_r(&TINY, x, y, s, 0); }

/* F21 scaled: the big digits */
static int big(int x, int y, const char *s, int k) {
  for (; *s; s++) {
    int c = (unsigned char)*s;
    if (c == 1) { gfx_circle(x + 2 * k, y + 4 * k, 2 * k, 1); x += 5 * k; continue; }
    const C47Glyph *g = &F21.g[c & 0x7f];
    for (int i = 0; i < g->n; i++) {
      const C47Col *col = &F21.c[g->ofs + i];
      unsigned m = col->m;
      for (int j = 0; m; j++, m >>= 1)
        if (m & 1) gfx_rect(x + col->x * k, y + (13 - g->yoff - j) * k, k, k, 1);
    }
    x += g->adv * k;
  }
  return x;
}
static int big_w(const char *s, int k) {
  int w = 0;
  for (; *s; s++) w += (*s == 1 ? 5 : F21.g[*s & 0x7f].adv) * k;
  return w;
}

static void fmt_pos(char *s, double v, int lat) {
  double a = fabs(v);
  int d = (int)a;
  double m = (a - d) * 60;
  if (m >= 59.9995) { d++; m = 0; }
  sprintf(s, lat ? "%02d" NT_DEG "%06.3f'%c" : "%03d" NT_DEG "%06.3f'%c", d, m, lat ? (v < 0 ? 'S' : 'N') : (v < 0 ? 'W' : 'E'));
}

static void fmt_eta(char *s, double hours) {
  if (isnan(hours) || hours > 999) { strcpy(s, "--:--"); return; }
  double j = nav_ut() + hours / 24 + nav_tz_shown() / 24;
  double h = fmod((j + 0.5) * 24, 24.0);
  if (h < 0) h += 24;
  int mi = (int)floor(h * 60 + 0.5) % 1440;
  long days = (long)floor(j + 0.5) - (long)floor(nav_ut() + nav_tz_shown() / 24 + 0.5);
  if (days > 0) sprintf(s, "%02d:%02d+%ld", mi / 60, mi % 60, days);
  else sprintf(s, "%02d:%02d", mi / 60, mi % 60);
}

static void fmt_ttg(char *s, double h) {
  if (isnan(h) || h > 9999) { strcpy(s, "--"); return; }
  long m = (long)floor(h * 60 + 0.5);
  if (m >= 1440) sprintf(s, "%ldd%02ldh", m / 1440, (m / 60) % 24);
  else sprintf(s, "%ld:%02ld", m / 60, m % 60);
}

static void fmt_deg(char *s, double v) { if (isnan(v)) strcpy(s, "---"); else sprintf(s, "%03.0f" NT_DEG, fmod(v + 360.0, 360.0)); }
static void fmt_kn(char *s, double v) { if (isnan(v)) strcpy(s, "--.-"); else sprintf(s, "%.1f", v); }

/* a filled polygon (scanlines, even-odd) */
static void fill_poly(const float *x, const float *y, int n, int v) {
  float y0 = 1e9f, y1 = -1e9f;
  for (int i = 0; i < n; i++) { if (y[i] < y0) y0 = y[i]; if (y[i] > y1) y1 = y[i]; }
  if (y0 < 0) y0 = 0;
  if (y1 > LCD_Y - 1) y1 = LCD_Y - 1;
  for (int r = (int)ceilf(y0); r <= (int)y1; r++) {
    float xs[16];
    int k = 0;
    float yc = r + 0.5f;
    for (int i = 0, j = n - 1; i < n; j = i++) {
      if ((y[i] > yc) != (y[j] > yc) && k < 16) xs[k++] = x[j] + (yc - y[j]) * (x[i] - x[j]) / (y[i] - y[j]);
    }
    for (int a = 1; a < k; a++) for (int b = a; b > 0 && xs[b] < xs[b - 1]; b--) { float t = xs[b]; xs[b] = xs[b - 1]; xs[b - 1] = t; }
    for (int a = 0; a + 1 < k; a += 2) {
      int xa = (int)lroundf(xs[a]), xb = (int)lroundf(xs[a + 1]);
      if (xa < 0) xa = 0;
      if (xb > LCD_X) xb = LCD_X;
      if (xb > xa) gfx_rect(xa, r, xb - xa, 1, v);
    }
  }
}
static void poly_line(const float *x, const float *y, int n, int closed) {
  for (int i = 0; i + 1 < n + (closed ? 1 : 0); i++) {
    int j = (i + 1) % n;
    gfx_line((int)lroundf(x[i]), (int)lroundf(y[i]), (int)lroundf(x[j]), (int)lroundf(y[j]), 1);
  }
}

static void alarm_bar(void) {
  const char *a = gps_alarm_text();
  if (!a) return;
  lcd_fill_rect(0, 0, LCD_X, 15, 1);                    /* over the top bar: black, the alarm in white */
  nt_inv(LCD_X / 2 - nt_w(a) / 2, 0, a);
  draw_text_r(&TINY, LCD_X - 2, 3, "ENTER ok", 1);
}

/* the source: an inverse tag */
static void src_tag(int x, int y) {
  char s[24];
  sprintf(s, " %s ", navfix_src_name(&P->f));
  nt_inv(x, y, s);
}

/* ------------------------------------------------------------------ FIX */
static void draw_fix(void) {
  char s[64], t[32];
  nav_header("FIX");
  NavFix *f = &P->f;
  GpsState *g = &P->g;
  uint32_t now = sys_current_ms();
  int y = TOP + 3;
  src_tag(4, y);
  int used = 0, seen = 0;
  for (int k = 0; k < GNSS_N; k++) { int u; seen += nmea_count(g, k, &u, now); used += u; }
  if (f->src == SRC_GPS) {
    sprintf(s, "%s  %d of %d sats  HDOP %.1f", g->fix_dim == 3 ? "3D" : g->fix_dim == 2 ? "2D" : "  ", g->nused ? g->nused : used, seen,
            isnan(g->hdop) ? 0 : g->hdop);
    nt(70, y, s);
  } else if (f->src == SRC_DR) {
    long sec = f->last_fix_ms ? (long)((now - f->last_fix_ms) / 1000) : -1;
    if (sec >= 0) sprintf(s, "no fix for %ld:%02ld", sec / 60, sec % 60); else strcpy(s, "never a fix: from SET");
    nt(50, y, s);
    char c[16], k[16];
    fmt_deg(c, f->cog); fmt_kn(k, f->sog);
    sprintf(s, "DR %s %c  %s kn %c", c, f->dr_crs_src ? f->dr_crs_src : '?', k, f->dr_kn_src ? f->dr_kn_src : '?');
    nt_r(LCD_X - 4, y, s);
  } else if (f->src == SRC_SET) nt(70, y, "GPS SET: POS OFF, SET's position");
  if (P->input && f->src != SRC_DR) {
    long age = g->rx_ms ? (long)((now - g->rx_ms) / 1000) : -1;
    if (age < 0) strcpy(s, "waiting for NMEA"); else if (age > 3) sprintf(s, "no data %lds", age); else s[0] = 0;
    if (s[0]) nt_r(LCD_X - 4, y, s);
  } else if (!P->input && f->src != SRC_SET) nt_r(LCD_X - 4, y, "input OFF");
  /* the position, big */
  y += 20;
  fmt_pos(s, f->lat, 1); big(6, y, s, 2);
  fmt_pos(s, f->lon, 0); big(6, y + 30, s, 2);
  /* motion, on the right */
  int X = 266, XR = LCD_X - 4, yy = y - 2;
  fmt_kn(t, f->sog); nt(X, yy, "SOG"); nt_r(XR, yy, t); yy += 16;
  fmt_deg(t, f->cog); nt(X, yy, "COG"); nt_r(XR, yy, t); yy += 16;
  fmt_deg(t, f->hdg); nt(X, yy, "HDG"); nt_r(XR, yy, t); yy += 16;
  fmt_kn(t, f->stw); nt(X, yy, "STW"); nt_r(XR, yy, t);
  /* quality */
  y += 64;
  gfx_rect(0, y - 3, LCD_X, 1, 1);
  int q = nmea_qual(g);
  char a[8], b[8], c[8];
  if (isnan(g->pdop)) strcpy(a, "-"); else sprintf(a, "%.1f", g->pdop);
  if (isnan(g->hdop)) strcpy(b, "-"); else sprintf(b, "%.1f", g->hdop);
  if (isnan(g->vdop)) strcpy(c, "-"); else sprintf(c, "%.1f", g->vdop);
  sprintf(s, "%-7s PDOP %s  HDOP %s  VDOP %s", nmea_qual_name(q), a, b, c);
  nt(4, y, s);
  y += 16;
  if (g->dgps_id >= 0 || !isnan(g->dgps_age)) sprintf(s, "DGPS station %04d  age %.0f s", g->dgps_id < 0 ? 0 : g->dgps_id, isnan(g->dgps_age) ? 0 : g->dgps_age);
  else strcpy(s, "no differential");
  nt(4, y, s);
  if (g->gst_ms && now - g->gst_ms < 15000) {
    sprintf(s, "err %.1f m", hypot(g->sig_lat, g->sig_lon));
    nt_r(LCD_X - 4, y, s);
  }
  y += 16;
  double jd;
  if (nmea_jd(g, &jd) && g->utc_ms && now - g->utc_ms < 5000) {
    double gu = jd + (now - g->utc_ms) / 86400000.0;
    double cl = nav_ut();
    double hh = fmod(gu - 0.5, 1.0) * 24;
    int sec = (int)floor(hh * 3600 + 0.5);
    sprintf(s, "GPS UT %02d:%02d:%02d  clock %+.0f s%s", sec / 3600 % 24, sec / 60 % 60, sec % 60, (cl - gu) * 86400, P->use_ut ? " (UT GPS)" : "");
  } else strcpy(s, "GPS UT: --");
  nt(4, y, s);
  y += 16;
  /* the anchor, the DR's error, the gyro error */
  if (P->anc) {
    double b, d = nm_brg(P->anc_lat, P->anc_lon, f->lat, f->lon, &b);
    sprintf(s, "ANCHOR %.0f m of %.0f m, %03.0f" NT_DEG, d * 1852, P->anc_r, b);
    if (P->alarm & A_ANCHOR) nt_inv(4, y, s); else nt(4, y, s);
  } else if (!isnan(f->dr_err_nm) && now - f->dr_err_ms < 600000) {
    sprintf(s, "the DR was %.2f nm %03.0f" NT_DEG " off", f->dr_err_nm, f->dr_err_brg);
    nt(4, y, s);
  }
  VBody bd[8];
  int nb = nav_bodies(bd);
  double szn = NAN, shc = NAN;
  for (int i = 0; i < nb; i++) if (bd[i].id == 0) { szn = bd[i].zn; shc = bd[i].hc; }
  if (P->gyro_edit) { sprintf(s, "SUN BRG by gyro: %s_", P->ebuf); nt_inv(4, y + 16, s); }
  else if (P->gyro_have && !isnan(szn)) {
    double e = fmod(szn - P->gyro_obs + 540.0, 360.0) - 180.0;
    sprintf(s, "SUN Zn %05.1f" NT_DEG " gyro %05.1f" NT_DEG ": ERR %.1f" NT_DEG " %s", szn, P->gyro_obs, fabs(e), e > 0 ? "LOW" : "HIGH");
    nt(4, y + 16, s);
  } else if (!isnan(szn) && shc > -1) {
    sprintf(s, "SUN Zn %05.1f" NT_DEG "  Hc %.1f" NT_DEG "  ENTER: its gyro bearing", szn, shc);
    nt(4, y + 16, s);
  }
  int nl, no;
  gpsin_counts(&nl, &no);
  sprintf(s, "%s  in %d  bad %lu  lost %d", P->input ? "USB NMEA ch 2" : "NMEA input off", nl, (unsigned long)g->n_bad, no);
  tiny(2, BOT - LHT + 1, s);
  tiny_r(LCD_X - 2, BOT - LHT + 1, "7 anchor  R/S MOB  0 SET");
  alarm_bar();
}

/* ------------------------------------------------------------------ SATS */
static void sat_symbol(int x, int y, int sys, int used) {
  switch (sys) {
  case GNSS_GPS: if (used) gfx_disc(x, y, 4, 1); else gfx_circle(x, y, 4, 1); break;
  case GNSS_GLO: if (used) gfx_rect(x - 3, y - 3, 7, 7, 1); else gfx_frame(x - 3, y - 3, 7, 7); break;
  case GNSS_GAL: {
    float px[4] = { (float)x, x + 4.5f, (float)x, x - 4.5f }, py[4] = { y - 4.5f, (float)y, y + 4.5f, (float)y };
    if (used) fill_poly(px, py, 4, 1); else poly_line(px, py, 4, 1);
    break;
  }
  case GNSS_BDS: {
    float px[3] = { (float)x, x + 4.5f, x - 4.5f }, py[3] = { y - 4.5f, y + 3.5f, y + 3.5f };
    if (used) fill_poly(px, py, 3, 1); else poly_line(px, py, 3, 1);
    break;
  }
  default:
    gfx_line(x - 3, y - 3, x + 3, y + 3, 1); gfx_line(x - 3, y + 3, x + 3, y - 3, 1);
    if (used) gfx_circle(x, y, 4, 1);
  }
}

static void draw_sats(void) {
  char s[48];
  nav_header("SATS");
  GpsState *g = &P->g;
  uint32_t now = sys_current_ms();
  const int cx = 100, cy = 110, R = 86;
  gfx_circle(cx, cy, R, 1);
  for (int e = 30; e < 90; e += 30) {
    int r = R * (90 - e) / 90;
    for (int a = 0; a < 360; a += 4) gfx_pixel(cx + (int)lround(r * sin(a * D2R)), cy - (int)lround(r * cos(a * D2R)), 1);
  }
  for (int a = 0; a < 360; a += 30) {
    int x = cx + (int)lround(R * sin(a * D2R)), y = cy - (int)lround(R * cos(a * D2R));
    int x2 = cx + (int)lround((R - 4) * sin(a * D2R)), y2 = cy - (int)lround((R - 4) * cos(a * D2R));
    gfx_line(x, y, x2, y2, 1);
  }
  tiny(cx - 2, cy - R - 1 + 2, "N");
  tiny(cx + R + 3, cy - 4, "E");
  tiny(cx - 2, cy + R - 7, "S");
  tiny(cx - R - 8, cy - 4, "W");
  gfx_pixel(cx, cy, 1);
  /* the Sun, the Moon, the planets, the stars: on the same sky (0) */
  if (P->stars) {
    VBody bd[8];
    int nb = nav_bodies(bd);
    for (int i = 0; i < nb; i++) {
      if (bd[i].hc < 0) continue;
      int r = (int)lround(R * (90 - bd[i].hc) / 90);
      int x = cx + (int)lround(r * sin(bd[i].zn * D2R)), y = cy - (int)lround(r * cos(bd[i].zn * D2R));
      if (bd[i].id == 0) { gfx_circle(x, y, 6, 1); gfx_pixel(x, y, 1); tiny(x + 8, y - 4, "SUN"); }
      else if (bd[i].id == -1) { gfx_circle(x, y, 5, 1); gfx_disc(x + 2, y, 4, 0); gfx_circle(x, y, 5, 1); tiny(x + 7, y - 4, "MOON"); }
      else {
        gfx_line(x - 3, y, x + 3, y, 1); gfx_line(x, y - 3, x, y + 3, 1); gfx_pixel(x - 1, y - 1, 1); gfx_pixel(x + 1, y + 1, 1);
        gfx_pixel(x - 1, y + 1, 1); gfx_pixel(x + 1, y - 1, 1);
      }
    }
  }
  int nsee = 0;
  for (int i = 0; i < g->nsat; i++) {
    NmeaSat *t = &g->sat[i];
    if (t->el < 0 || t->az < 0 || now - t->ms > NMEA_SAT_TTL) continue;
    nsee++;
    int r = R * (90 - t->el) / 90;
    int x = cx + (int)lround(r * sin(t->az * D2R)), y = cy - (int)lround(r * cos(t->az * D2R));
    gfx_disc(x, y, 6, 0);
    sat_symbol(x, y, t->sys, nmea_sat_used(g, t, now));
    sprintf(s, "%d", t->prn);
    tiny(x + 6, y - 2, s);
  }
  if (!nsee) nt_c(cx, cy - 8, P->input ? "no satellites yet" : "input OFF");
  /* the systems: used / seen, and the legend */
  int X = 200, y = TOP + 3;
  for (int k = 0; k < GNSS_N; k++) {
    int u, n = nmea_count(g, k, &u, now);
    if (!n && k >= GNSS_QZS) continue;
    sat_symbol(X + 5, y + 7, k, 1);
    sprintf(s, "%s %d/%d", gnss_name(k), u, n);
    if (P->sat_sys == k) nt_inv(X + 13, y, s); else nt(X + 13, y, s);
    X += 100;
    if (X > 320) { X = 200; y += 15; }
  }
  /* the signal bars */
  int bx = 210, by = 186, bh = 82, nbar = 0;
  int idx[NMEA_MAXSAT];
  for (int i = 0; i < g->nsat; i++) {
    NmeaSat *t = &g->sat[i];
    if (now - t->ms > NMEA_SAT_TTL) continue;
    if (P->sat_sys != GNSS_N && t->sys != P->sat_sys) continue;
    idx[nbar++] = i;
  }
  for (int a = 1; a < nbar; a++)                        /* by system, then number */
    for (int b = a; b > 0; b--) {
      NmeaSat *p = &g->sat[idx[b - 1]], *q = &g->sat[idx[b]];
      if (p->sys > q->sys || (p->sys == q->sys && p->prn > q->prn)) { int z = idx[b]; idx[b] = idx[b - 1]; idx[b - 1] = z; }
      else break;
    }
  int bw = nbar ? (LCD_X - 4 - bx) / nbar : 10;
  if (bw > 16) bw = 16;
  for (int dB = 10; dB <= 50; dB += 10) {
    int yy = by - dB * bh / 50;
    for (int x = bx; x < LCD_X - 4; x += 3) gfx_pixel(x, yy, 1);
    if (dB % 20 == 10) { sprintf(s, "%d", dB); tiny_r(bx - 2, yy - 4, s); }
  }
  for (int k = 0; k < nbar; k++) {
    NmeaSat *t = &g->sat[idx[k]];
    int h = t->snr > 0 ? t->snr * bh / 50 : 0;
    if (h > bh) h = bh;
    int x = bx + k * bw;
    if (nmea_sat_used(g, t, now)) gfx_rect(x + 1, by - h, bw - 2, h, 1);
    else { gfx_frame(x + 1, by - h, bw - 2, h + 1); }
    if (bw >= 12) { sprintf(s, "%d", t->prn % 100); tiny(x + 1, by + 2, s); }
    else if (k == 0 || g->sat[idx[k - 1]].sys != t->sys) {   /* narrow: the system's name under its bars */
      gfx_rect(x, by + 1, 1, 4, 1);
      tiny(x + 2, by + 2, gnss_name(t->sys));
    }
  }
  gfx_rect(bx, by, LCD_X - 4 - bx, 1, 1);
  tiny(2, BOT - LHT + 1, P->stars ? "0 satellites only" : "0 + Sun Moon stars");
  tiny_r(LCD_X - 2, BOT - LHT + 1, "4 6 system  bars dB-Hz, filled: used");
  alarm_bar();
}

/* ------------------------------------------------------------------ NAV */
static void xte_bar(int x0, int y0, int w, double xte, double lim) {
  int cx = x0 + w / 2;
  double full = lim * 2;                               /* the bar: +- two limits */
  gfx_frame(x0, y0, w, 18);
  for (int k = -4; k <= 4; k++) {
    int x = cx + k * (w / 2 - 2) / 4;
    gfx_rect(x, y0 + (k % 2 ? 13 : 11), 1, k % 2 ? 5 : 7, 1);
  }
  for (int sgn = -1; sgn <= 1; sgn += 2) {             /* the limits */
    int x = cx + sgn * (w / 2 - 2) / 2;
    gfx_rect(x - 1, y0 + 1, 3, 16, 1);
  }
  gfx_rect(cx, y0 + 1, 1, 16, 1);
  double f = xte / full;
  if (f > 1) f = 1;
  if (f < -1) f = -1;
  int bx = cx + (int)lround(f * (w / 2 - 6));          /* the vessel: a triangle */
  float px[3] = { (float)bx, bx + 6.f, bx - 6.f }, py[3] = { (float)y0 - 8, (float)y0 + 3, (float)y0 + 3 };
  fill_poly(px, py, 3, 1);
}

static void draw_nav(void) {
  char s[64], t[32];
  Passage *p = &P->p;
  NavFix *f = &P->f;
  double pk;
  const route_t *R = nav_route(&pk);
  if (!R) nav_header("NAV");
  else {
    sprintf(s, "NAV  LEG %d/%d", p->leg, R->n - 1);
    nav_header(s);
  }
  int y = TOP + 3;
  /* left: to the next point, the next user waypoint, the final */
  if (R && p->active) {
    if (p->done) nt_inv(6, y, "ARRIVED");
    else {
      int user = R->w[p->leg].flags & WPT_USER;
      const char *wn = nav_wp_name(p->user_no - 1);
      if (wn[0]) snprintf(s, sizeof s, user ? "TO %.12s" : "TO %.12s (pt %d)", wn, p->leg + 1);
      else sprintf(s, user ? "TO WP%d" : "TO WP%d (pt %d)", p->user_no, p->leg + 1);
      nt_inv(6, y, s);
    }
    y += 17;                                            /* 36: to the next point */
    fmt_deg(t, p->brg); nt(4, y, "BRG"); nt_r(96, y, t);
    sprintf(t, "%.2f", p->dtg); nt(106, y, "DTG"); nt_r(200, y, t);
    y += 16;                                            /* 52 */
    fmt_deg(t, p->cts); nt(4, y, "CTS"); nt_r(96, y, t);
    if (!isnan(p->next_crs)) { fmt_deg(t, p->next_crs); nt(106, y, "NEXT"); nt_r(200, y, t); }
    y += 16;                                            /* 68: the next waypoint, the final one: distance, ETA */
    { const char *wn = nav_wp_name(p->user_no - 1);
      fmt_eta(t, p->ttg_user);
      if (wn[0]) snprintf(s, sizeof s, "%.7s %.1f nm", wn, p->dtg_user); else sprintf(s, "WP%d %.1f nm", p->user_no, p->dtg_user);
      nt(4, y, s); nt_r(200, y, t); }
    y += 16;                                            /* 84 */
    fmt_eta(t, p->ttg_final); sprintf(s, "FINAL %.1f nm", p->dtg_final); nt(4, y, s); nt_r(200, y, t);
    y += 17;                                            /* 101: the times to go, the speed */
    char a1[16], a2[16];
    fmt_ttg(a1, p->ttg_user); fmt_ttg(a2, p->ttg_final);
    snprintf(s, sizeof s, "TTG %s / %s at %.1f kn", a1, a2, p->kn);
    tiny(4, y, s);
  } else if (R) nt(6, y + 18, "5: start the passage");
  else {                                               /* no route: where she is, and how to give her one */
    char a[24];
    nt_inv(6, y, "NO ROUTE");
    fmt_pos(a, f->lat, 1); nt(4, y + 20, a);
    fmt_pos(a, f->lon, 0); nt(4, y + 36, a);
    tiny(4, y + 58, "ROUTE: two waypoints or more,");
    tiny(4, y + 67, "or WPTS (1 on ROUTE)");
  }
  /* right: the vessel */
  int X = 214, XR = LCD_X - 4;
  y = TOP + 3;
  src_tag(X, y);
  if (P->mob) {                                         /* MOB: on the top row, always in sight */
    double b, d = nm_brg(f->lat, f->lon, P->mob_lat, P->mob_lon, &b);
    sprintf(s, "MOB %03.0f" NT_DEG " %.2f", b, d);
    nt_inv(XR - nt_w(s), y, s);
  } else if (f->src == SRC_GPS) { sprintf(s, "%d sats", P->g.nused); tiny_r(XR, y + 4, s); }
  y += 18;
  fmt_kn(t, f->sog); nt(X, y, "SOG"); big(XR - big_w(t, 2), y - 1, t, 2);
  y += 30;
  fmt_deg(t, f->cog); nt(X, y, "COG"); nt_r(XR, y, t); y += 16;
  fmt_deg(t, f->hdg); nt(X, y, "HDG"); nt_r(XR, y, t); y += 16;
  if (!isnan(f->set)) { sprintf(t, "%03.0f" NT_DEG " %.1f kn", f->set, f->drift); nt(X, y, "SET"); nt_r(XR, y, t); }
  else if (!isnan(f->hdg) && !isnan(f->cog)) { double d = fmod(f->cog - f->hdg + 540.0, 360.0) - 180.0; sprintf(t, "%.1f" NT_DEG " %s", fabs(d), d >= 0 ? "STBD" : "PORT"); nt(X, y, "DRIFT"); nt_r(XR, y, t); }
  y += 16;
  gfx_rect(206, TOP, 1, 104, 1);
  /* the nearest objects: BRG, distance, CPA in how long (inverse: its zone on the course) */
  ObjNear z[3];
  int nz = obj_count() ? obj_near(f->lat, f->lon, f->cog, f->sog, 20, z, 3) : 0;
  if (nz) {
    gfx_rect(0, 121, LCD_X, 1, 1);
    for (int i = 0; i < nz; i++) {
      char c[24];
      if (z[i].tcpa > 0 && z[i].tcpa < 24) sprintf(c, "CPA %.2f in %.0f'", z[i].cpa, z[i].tcpa * 60);
      else strcpy(c, "opening");
      static const char *TY[] = { "PLAT", "RIG", "WELL", "SPM", "WRECK", "BUOY", "LIGHT", "PORT" };
      sprintf(s, "%-12.12s %-5s %03.0f" SYM_DEG " %5.2f nm  %s", z[i].o->name, TY[z[i].o->type < 8 ? z[i].o->type : 0], z[i].brg, z[i].nm, c);
      draw_text(&TINY, 4, 122 + i * 9, s, z[i].warn);
    }
  }
  /* the XTE bar */
  if (R && p->active && !p->done) {
    int yb = 178;
    sprintf(s, "XTE %.3f nm %s", fabs(p->xte), p->xte >= 0 ? "R" : "L");
    if (P->alarm & A_XTE) nt_inv(6, yb - 26, s); else nt(6, yb - 26, s);
    double steer = p->xte;
    if (fabs(steer) > 0.01) { sprintf(s, steer > 0 ? "<< STEER PORT" : "STEER STBD >>"); nt_r(XR, yb - 26, s); }
    sprintf(s, "limit %.2f", p->xte_lim); tiny(8, yb - 10, s);
    xte_bar(6, yb, LCD_X - 12, p->xte, p->xte_lim);
  }
  if (R) {
    tiny(2, BOT - LHT + 1, "4 6 leg  5 start  0 chart  ENTER ok  R/S MOB");
    sprintf(s, "ADV %s  ARR %.2f", p->auto_adv ? "AUTO" : "MAN", p->arr_r);
    tiny_r(LCD_X - 2, BOT - LHT + 1, s);
  } else tiny(2, BOT - LHT + 1, "0 chart  ENTER ok  R/S MOB");
  alarm_bar();
}

/* ------------------------------------------------------------------ BOW: over the bow from the helideck */
#define HOR   58                                   /* the horizon's row */
#define KPER  22.0                                 /* row = HOR + KPER / forward (nm) */
#define FOC   210.0                                /* column = 200 + FOC * right / forward */

/* a world point to the screen: 0 behind or too near */
static int bow_proj(double lat, double lon, double hdg, float *x, float *y) {
  double dl = fmod(lon - P->f.lon + 540.0, 360.0) - 180.0;
  double e = dl * cos(P->f.lat * D2R) * 60, n = (lat - P->f.lat) * 60;
  double fw = e * sin(hdg * D2R) + n * cos(hdg * D2R), rt = e * cos(hdg * D2R) - n * sin(hdg * D2R);
  if (fw < 0.06) return 0;
  *x = (float)(200 + FOC * rt / fw);
  *y = (float)(HOR + KPER / fw);
  return 1;
}

/* the point s metres along the plan, and the course there */
static void plan_at(const route_t *R, double s, double *lat, double *lon, double *crs) {
  int i = 1;
  while (i < R->n - 1 && R->w[i].cum_m < s) i++;
  const wpt_t *a = &R->w[i - 1], *b = &R->w[i];
  double ds = s - a->cum_m;
  if (ds < 0) ds = 0;
  if (ds > b->cum_m - a->cum_m) ds = b->cum_m - a->cum_m;
  if (b->kind == SEG_GEOD) {
    struct geod_geodesicline l;
    geod_inverseline(&l, nav_ell(), a->lat, a->lon, b->lat, b->lon, 0);
    geod_position(&l, ds, lat, lon, crs);
  } else {
    *crs = a->rl_crs;
    if (rhumb_direct(&R->rh, a->lat, a->lon, a->rl_crs, ds, lat, lon) != 0) { *lat = b->lat; *lon = b->lon; }
  }
}

/* your vessel's pictures: /NAV47/BOW.BMP (BOW), /NAV47/SHIP.BMP (the CHART); none: the drawn ones */
static Pic bowpic, shippic;
static int bow_rc, ship_rc;

int gps_pics_load(char *msg, int n) {
  pic_free(&bowpic); pic_free(&shippic);
  bow_rc = pic_load(&bowpic, BOW_FILE, LCD_X, 150);
  ship_rc = pic_load(&shippic, SHIP_FILE, 64, 160);
  if (msg) snprintf(msg, (size_t)n, "BOW.BMP %s, SHIP.BMP %s", pic_err(bow_rc), pic_err(ship_rc));
  return (bow_rc == 1) + (ship_rc == 1);
}

static void bow_ship(void) {
  if (bowpic.h) {                                      /* the picture, at the bottom in the middle */
    int x0 = (LCD_X - bowpic.w) / 2, y0 = BOT + 1 - bowpic.h;
    for (int y = 0; y < bowpic.h; y++)
      for (int x = 0; x < bowpic.w; x++)
        if (pic_get(&bowpic, bowpic.mask, x, y)) gfx_pixel(x0 + x, y0 + y, pic_get(&bowpic, bowpic.ink, x, y));
    return;
  }
  /* the Asiana's foredeck from the helideck: the raked bow, the bulwarks, the windlass, the bow-thruster hatch, the
     bridge front's windows at the bottom (until the photo) */
  float hx[5] = { 112, 172, 200, 228, 288 }, hy[5] = { 200, 144, 122, 144, 200 };
  fill_poly(hx, hy, 5, 0);
  poly_line(hx, hy, 5, 0);
  gfx_line(200, 122, 200, 200, 1);
  for (int k = 0; k < 5; k++) {                         /* the deck's seams */
    int y = 146 + k * 12, w = (int)((y - 122) * 1.04f);
    gfx_line(200 - w, y, 200 + w, y, 1);
  }
  gfx_line(120, 196, 174, 142, 1); gfx_line(174, 142, 200, 124, 1);   /* the bulwark's inside */
  gfx_line(280, 196, 226, 142, 1); gfx_line(226, 142, 200, 124, 1);
  gfx_circle(200, 134, 4, 1);                           /* the windlass */
  gfx_frame(188, 176, 25, 12);                          /* the bow tunnel's hatch */
  gfx_rect(60, 196, 280, 12, 1);                        /* the bridge front */
  for (int x = 70; x < 330; x += 22) gfx_rect(x, 199, 16, 6, 0);
}

static void draw_bow(uint32_t frame) {
  char s[64], t[16];
  Passage *p = &P->p;
  NavFix *f = &P->f;
  double pk;
  const route_t *R = nav_route(&pk);
  double hdg = !isnan(f->hdg) ? f->hdg : !isnan(f->cog) ? f->cog : 0;
  if (R && p->active && !p->done) {
    fmt_eta(t, p->ttg_user);
    sprintf(s, "BOW WP%d %s", p->user_no, t);
  } else strcpy(s, "BOW");
  nav_header(s);
  gfx_rect(0, HOR, LCD_X, 1, 1);
  /* the Sun and the Moon at their bearings, near the horizon */
  VBody bd[8];
  int nb = nav_bodies(bd);
  for (int i = 0; i < nb; i++) {
    if (bd[i].id > 0 || bd[i].id < -1 || bd[i].hc < -1 || bd[i].hc > 40) continue;
    double a = fmod(bd[i].zn - hdg + 540.0, 360.0) - 180.0;
    if (fabs(a) > 62) continue;
    int x = 200 + (int)lround(FOC * tan(a * D2R) * 0.9), y = HOR - 6 - (int)lround(bd[i].hc * 1.0);
    if (y < TOP + 8) y = TOP + 8;
    if (bd[i].id == 0) { gfx_circle(x, y, 6, 1); for (int k = 0; k < 8; k++) gfx_pixel(x + (int)lround(9 * cos(k * M_PI / 4)), y + (int)lround(9 * sin(k * M_PI / 4)), 1); }
    else { gfx_disc(x, y, 5, 1); gfx_disc(x + 3, y - 1, 4, 0); }
  }
  /* the sea: dashes coming in, faster with the speed */
  double kn = isnan(f->sog) ? 0 : f->sog;
  uint32_t ph = frame * (uint32_t)(4 + kn * 1.2);
  for (int r = 0; r < 16; r++) {
    double z = fmod(r / 16.0 + (ph % 1000) / 1000.0, 1.0);
    int y = HOR + 3 + (int)(z * z * 140);
    if (y > BOT) continue;
    int step = 20 + (int)(z * 60), off = (r * 37) % step, w = 2 + (int)(z * 10);
    for (int x = -off; x < LCD_X; x += step) gfx_rect(x, y, w, 1, 1);
  }
  /* the lane: the plan ahead +- the XTE limit, 8 nm */
  if (R && p->active && !p->done) {
    double s0 = R->w[p->leg - 1].cum_m + p->atd * 1852, end = R->w[R->n - 1].cum_m;
    float lx[2][64], ly[2][64], cx[64], cy[64];
    int n = 0, ok[64];
    for (int k = 0; k < 64 && n < 64; k++) {
      double d = 0.05 * pow(1.075, k) * 1852;          /* denser near */
      double sm = s0 + d;
      if (sm > end) sm = end;
      double la, lo, crs, l2, o2, z;
      plan_at(R, sm, &la, &lo, &crs);
      ok[n] = bow_proj(la, lo, hdg, &cx[n], &cy[n]);
      for (int side = 0; side < 2; side++) {
        geod_direct(nav_ell(), la, lo, crs + (side ? 90 : -90), p->xte_lim * 1852, &l2, &o2, &z);
        if (!bow_proj(l2, o2, hdg, &lx[side][n], &ly[side][n])) ok[n] = 0;
      }
      n++;
      if (sm >= end || d > 8 * 1852) break;
    }
    for (int i = 0; i + 1 < n; i++) {
      if (!ok[i] || !ok[i + 1]) continue;
      for (int side = 0; side < 2; side++)
        if ((i + frame / 2) % 3 != 0 || ly[side][i] < 120) {
          gfx_line((int)lx[side][i], (int)ly[side][i], (int)lx[side][i + 1], (int)ly[side][i + 1], 1);
          if (ly[side][i] > 110) gfx_line((int)lx[side][i] + 1, (int)ly[side][i], (int)lx[side][i + 1] + 1, (int)ly[side][i + 1], 1);
        }
      if (i % 2 == 0) gfx_pixel((int)cx[i], (int)cy[i], 1);
    }
    /* the waypoints' flags ahead */
    for (int i = p->leg; i < R->n; i++) {
      if (R->w[i].cum_m - s0 > 9 * 1852) break;
      if (!(R->w[i].flags & WPT_USER) && i != R->n - 1) continue;
      float x, y;
      if (!bow_proj(R->w[i].lat, R->w[i].lon, hdg, &x, &y) || x < 0 || x >= LCD_X) continue;
      int X = (int)x, Y = (int)y, h = 10 + (int)((y - HOR) * 0.5f);
      gfx_line(X, Y, X, Y - h, 1);
      float fx[3] = { (float)X, X + 12.f, (float)X }, fy[3] = { (float)(Y - h), Y - h + 4.f, Y - h + 8.f };
      fill_poly(fx, fy, 3, 1);
      int no = 0;
      for (int k = 0; k <= i; k++) if (R->w[k].flags & WPT_USER) no++;
      sprintf(s, "WP%d", no);
      tiny(X + 14, Y - h - 1, s);
    }
  }
  /* the platforms and their zones ahead */
  obj_bow(P->f.lat, P->f.lon, hdg, HOR, KPER, FOC);
  bow_ship();
  /* the numbers, in boxes beside the bow (the horizon stays clear) */
  const int BY = 150;
  gfx_rect(2, BY, 104, 34, 0); gfx_frame(2, BY, 104, 34);
  gfx_rect(LCD_X - 106, BY, 104, 34, 0); gfx_frame(LCD_X - 106, BY, 104, 34);
  if (R && p->active && !p->done) {
    sprintf(s, "XTE %.2f%s", fabs(p->xte), p->xte >= 0 ? "R" : "L"); nt(6, BY + 1, s);
    fmt_deg(t, p->cts); sprintf(s, "CTS %s", t); nt(6, BY + 17, s);
  } else if (R && !p->done) { nt(6, BY + 1, "the route:"); nt(6, BY + 17, "NAV 5 start"); }
  else { nt(6, BY + 1, R ? "ARRIVED" : "no route"); fmt_kn(t, f->sog); fmt_deg(s, f->cog); sprintf(s + 20, "COG %s", s); nt(6, BY + 17, s + 20); }
  fmt_kn(t, f->sog); sprintf(s, "SOG %s", t); nt(LCD_X - 102, BY + 1, s);
  fmt_deg(t, hdg); sprintf(s, "%s %s", !isnan(f->hdg) ? "HDG" : "COG", t); nt(LCD_X - 102, BY + 17, s);
  if (R && p->active && !p->done && !isnan(p->next_crs) && p->dtg < 3 * p->arr_r + 0.5) {
    fmt_deg(t, p->next_crs);
    sprintf(s, "TURN AHEAD %s", t);
    gfx_rect(2, BY - 20, nt_w(s) + 8, 16, 0); gfx_frame(2, BY - 20, nt_w(s) + 8, 16);
    nt(6, BY - 20, s);
  }
  alarm_bar();
}

/* ------------------------------------------------------------------ GPS SET */
static const char *SNAME[S_N] = { "NMEA INPUT", "POSITION", "SET FOLLOWS", "UT FROM GPS", "DR AFTER", "DR COURSE", "DR SPEED",
                                  "XTE LIMIT", "ARRIVAL", "NEXT LEG", "ANCHOR R", "ALARMS", "MOB" };
static const char *SHELP[S_N] = { "USB ch 2, the bridge", "ENTER: AUTO DR OFF", "SET's position, a minute",
                                  "the clock stays", "s without a fix", "degrees, - gyro / COG", "kn, 0 log / SOG",
                                  "nm", "nm, the circle", "ENTER: AUTO MAN", "m", "the beeps", "ENTER clears" };

static void set_text(int f, char *s) {
  switch (f) {
  case S_INPUT: strcpy(s, P->input ? "ON" : "OFF"); break;
  case S_MODE: strcpy(s, P->fc.mode == POS_AUTO ? "AUTO (GPS, DR)" : P->fc.mode == POS_DR ? "DR ONLY" : "OFF (SET's)"); break;
  case S_FOLLOW: strcpy(s, P->follow ? "ON" : "OFF"); break;
  case S_UT: strcpy(s, P->use_ut ? "ON" : "OFF"); break;
  case S_TIMEOUT: sprintf(s, "%d s", P->fc.timeout); break;
  case S_DRCRS: if (P->fc.dr_crs < 0) strcpy(s, "GYRO / COG"); else sprintf(s, "%03.0f" NT_DEG, P->fc.dr_crs); break;
  case S_DRKN: if (P->fc.dr_kn <= 0) strcpy(s, "LOG / SOG"); else sprintf(s, "%.1f kn", P->fc.dr_kn); break;
  case S_XTE: sprintf(s, "%.2f nm", P->p.xte_lim); break;
  case S_ARR: sprintf(s, "%.2f nm", P->p.arr_r); break;
  case S_ADV: strcpy(s, P->p.auto_adv ? "AUTO" : "BY HAND (6)"); break;
  case S_ANCR: sprintf(s, "%.0f m", P->anc_r); break;
  case S_ALARMS: strcpy(s, P->alarms ? "ON" : "OFF"); break;
  case S_MOB: if (P->mob) strcpy(s, "SET"); else strcpy(s, "-"); break;
  }
}

static void draw_set(void) {
  char v[40];
  nav_header("GPS SET");
  /* 11 rows of 16 (as SET), scrolled to the field; its help on the last line */
  const int VIS = 11;
  static int top;
  if (P->sel < top) top = P->sel;
  if (P->sel >= top + VIS) top = P->sel - VIS + 1;
  int y = TOP + 1;
  for (int f = top; f < S_N && f < top + VIS; f++, y += 16) {
    set_text(f, v);
    if (P->edit == f) sprintf(v, "%s_", P->ebuf);
    if (f == P->sel) nt_inv(6, y, SNAME[f]); else nt(6, y, SNAME[f]);
    nt(150, y, v);
  }
  if (top > 0) draw_text(&TINY, LCD_X - 10, TOP + 2, SYM_UP, 0);
  if (top + VIS < S_N) draw_text(&TINY, LCD_X - 10, TOP + 1 + 16 * VIS - 10, SYM_DOWN, 0);
  char h[64];
  snprintf(h, sizeof h, "%s: %s", SNAME[P->sel], SHELP[P->sel]);
  tiny(2, BOT - LHT + 1, h);
  tiny_r(LCD_X - 2, BOT - LHT + 1, "8 2 field  ENTER switch / edit");
}

static void set_commit(int f) {
  double v = atof(P->ebuf);
  int empty = P->elen == 0;
  switch (f) {
  case S_TIMEOUT: if (!empty && v >= 2 && v <= 600) P->fc.timeout = (int)v; break;
  case S_DRCRS: if (empty || v < 0) P->fc.dr_crs = -1; else if (v <= 360) P->fc.dr_crs = fmod(v, 360); break;
  case S_DRKN: if (empty) P->fc.dr_kn = 0; else if (v >= 0 && v < 60) P->fc.dr_kn = v; break;
  case S_XTE: if (!empty && v > 0 && v <= 10) P->p.xte_lim = v; break;
  case S_ARR: if (!empty && v > 0 && v <= 5) P->p.arr_r = v; break;
  case S_ANCR: if (!empty && v >= 10 && v <= 2000) P->anc_r = v; break;
  }
}

static int edit_key(int key, int allow_neg) {
  static const int dk[10] = { KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9 };
  for (int i = 0; i < 10; i++)
    if (key == dk[i] && P->elen < 8) { P->ebuf[P->elen++] = (char)('0' + i); P->ebuf[P->elen] = 0; return 1; }
  if (key == KEY_DOT && P->elen < 8 && !strchr(P->ebuf, '.')) { P->ebuf[P->elen++] = '.'; P->ebuf[P->elen] = 0; return 1; }
  if (key == KEY_CHS && allow_neg && P->elen == 0) { P->ebuf[P->elen++] = '-'; P->ebuf[P->elen] = 0; return 1; }
  if (key == KEY_BSP) { if (P->elen) P->ebuf[--P->elen] = 0; return 1; }
  return 0;
}

static void set_key(int key) {
  if (P->edit >= 0) {
    if (key == KEY_ENTER) { set_commit(P->edit); P->edit = -1; }
    else if (key == KEY_EXIT || key == KEY_F6) P->edit = -1;
    else edit_key(key, P->edit == S_DRCRS);
    return;
  }
  if (key == KEY_8 || key == KEY_UP) P->sel = (P->sel + S_N - 1) % S_N;
  else if (key == KEY_2 || key == KEY_DOWN) P->sel = (P->sel + 1) % S_N;
  else if (key == KEY_ENTER) {
    switch (P->sel) {
    case S_INPUT:
      if (P->input) { gpsin_off(); P->input = 0; }
      else P->input = gpsin_on();
      break;
    case S_MODE: P->fc.mode = (P->fc.mode + 1) % 3; break;
    case S_FOLLOW: P->follow ^= 1; P->follow_ms = 0; break;
    case S_UT: P->use_ut ^= 1; break;
    case S_ADV: P->p.auto_adv ^= 1; break;
    case S_ALARMS: P->alarms ^= 1; break;
    case S_MOB: P->mob = 0; break;
    default: P->edit = P->sel; P->elen = 0; P->ebuf[0] = 0;
    }
  }
}

/* ------------------------------------------------------------------ the live CHART */
static void ship_sprite(int x, int y, float ang, uint32_t frame, double rot, int moving, float L) {
  /* the Asiana from above, 95 x 22 m: L px long (her true size when the chart is that close, 40 px at least);
     ang: her heading on the screen (0 up, clockwise) */
  const float B = L * 22 / 95 < 7 ? 7 : L * 22 / 95, k = L / 44;
  float sa = sinf(ang), ca = cosf(ang);
#define RX(f, r) (x + (f) * sa + (r) * ca)
#define RY(f, r) (y - (f) * ca + (r) * sa)
  if (moving) {                                          /* the wake: the Kelvin wedge's arms, moving, bent in a turn */
    float bend = (float)(rot * 0.06);
    for (int side = -1; side <= 1; side += 2)
      for (int j = 0; j < 6; j++) {
        float s = (float)((j * 9 + frame * 3) % 54) + 3;
        s *= k > 1 ? k : 1;
        float f0 = -L / 2 - s, r0 = side * (B / 2 + s * 0.354f) - bend * s * s / 20;
        gfx_line((int)RX(f0, r0), (int)RY(f0, r0), (int)RX(f0 - 3, r0 + side * 1.2f), (int)RY(f0 - 3, r0 + side * 1.2f), 1);
      }
    for (int j = 0; j < 30; j += 4) {                    /* the propellers' wash */
      float s = (float)(j + (frame * 2) % 4) * (k > 1 ? k : 1), r0 = -bend * s * s / 20;
      gfx_pixel((int)RX(-L / 2 - s, r0), (int)RY(-L / 2 - s, r0), 1);
    }
    for (int side = -1; side <= 1; side += 2)            /* the bow wave */
      gfx_line((int)RX(L / 2 + 1, 0), (int)RY(L / 2 + 1, 0), (int)RX(L / 2 - 6, side * (B / 2 + 3)), (int)RY(L / 2 - 6, side * (B / 2 + 3)), 1);
  }
  if (shippic.h) {                                      /* SHIP.BMP: bow up, turned and scaled to L px long */
    float sc = shippic.h / L, R = L * 0.75f;
    int r = (int)R + 1;
    for (int dy = -r; dy <= r; dy++)
      for (int dx = -r; dx <= r; dx++) {
        float f = dx * sa - dy * ca, q = dx * ca + dy * sa;      /* forward (px), to starboard (px) */
        int sx = (int)floorf(q * sc + shippic.w / 2.0f), sy = (int)floorf(-f * sc + shippic.h / 2.0f);
        if (pic_get(&shippic, shippic.mask, sx, sy)) gfx_pixel(x + dx, y + dy, pic_get(&shippic, shippic.ink, sx, sy));
      }
    for (int j = (int)(L / 2) + 4; j < (int)(L / 2) + 30; j += 2) gfx_pixel((int)RX(j, 0), (int)RY(j, 0), 1);
    return;
  }
  float hf[5] = { L / 2, L / 2 - 9, -L / 2, -L / 2, L / 2 - 9 }, hr[5] = { 0, -B / 2, -B / 2, B / 2, B / 2 };
  float px[5], py[5];
  for (int i = 0; i < 5; i++) { px[i] = RX(hf[i], hr[i]); py[i] = RY(hf[i], hr[i]); }
  fill_poly(px, py, 5, 0);
  poly_line(px, py, 5, 1);
  float bf[4] = { 12 * k, 6 * k, 6 * k, 12 * k }, br[4] = { -B * 0.36f, -B * 0.36f, B * 0.36f, B * 0.36f };   /* the bridge, forward */
  for (int i = 0; i < 4; i++) { px[i] = RX(bf[i], br[i]); py[i] = RY(bf[i], br[i]); }
  fill_poly(px, py, 4, 1);
  int hx = (int)RX(-14 * k, 0), hy = (int)RY(-14 * k, 0);        /* the helideck, aft */
  gfx_circle(hx, hy, (int)(B * 0.36f), 1);
  gfx_pixel(hx, hy, 1);
  for (int j = (int)(L / 2) + 4; j < (int)(L / 2) + 30; j += 2) gfx_pixel((int)RX(j, 0), (int)RY(j, 0), 1);   /* the heading line */
#undef RX
#undef RY
}

void gps_chart_layer(const map_t *m, uint32_t frame, int follow) {
  (void)follow;
  if (!P) return;
  obj_chart(m);
  int live = P->f.src == SRC_GPS || P->f.src == SRC_DR;
  double pk;
  const route_t *R = nav_route(&pk);
  /* the XTE limits either side of the active leg */
  if (R && P->p.active && !P->p.done) {
    const wpt_t *a = &R->w[P->p.leg - 1], *b = &R->w[P->p.leg];
    for (int side = -1; side <= 1; side += 2) {
      double pl = 0, po = 0;
      for (int k = 0; k <= 16; k++) {
        double la, lo, crs, l2, o2, z;
        plan_at(R, a->cum_m + (b->cum_m - a->cum_m) * k / 16, &la, &lo, &crs);
        geod_direct(nav_ell(), la, lo, crs + side * 90, P->p.xte_lim * 1852, &l2, &o2, &z);
        if (k) map_seg(m, pl, po, l2, o2, MAP_DOT);
        pl = l2; po = o2;
      }
    }
    map_mark(m, b->lat, b->lon, 2);
  }
  /* the track */
  for (int i = 0; i < P->tn; i++) {
    int k = (P->th - 1 - i + TRAIL) % TRAIL;
    float x, y;
    map_xy(m, P->tla[k], P->tlo[k], &x, &y);
    if (x >= m->x0 && x < m->x0 + m->w && y >= m->y0 && y < m->y0 + m->h) { gfx_rect((int)x, (int)y, 2, 2, 1); }
  }
  if (P->mob) {
    float x, y;
    map_xy(m, P->mob_lat, P->mob_lon, &x, &y);
    if (y < m->y0 + 8 || y > m->y0 + m->h - 8 || x < m->x0 || x > m->x0 + m->w - 30) goto no_mob;
    gfx_circle((int)x, (int)y, 6, 1); gfx_line((int)x - 4, (int)y - 4, (int)x + 4, (int)y + 4, 1); gfx_line((int)x - 4, (int)y + 4, (int)x + 4, (int)y - 4, 1);
    tiny((int)x + 8, (int)y - 4, "MOB");
  }
no_mob:
  if (P->anc) {
    float x, y, x2, y2;
    map_xy(m, P->anc_lat, P->anc_lon, &x, &y);
    map_xy(m, P->anc_lat + P->anc_r / 1852 / 60, P->anc_lon, &x2, &y2);
    int r = (int)fabsf(y2 - y);
    if (r > 2) gfx_circle((int)x, (int)y, r, 1);
    tiny((int)x + 3, (int)y - 4, "ANC");
  }
  if (!live) return;
  float x, y, ax, ay;
  map_xy(m, P->f.lat, P->f.lon, &x, &y);
  if (x < m->x0 - 30 || x > m->x0 + m->w + 30 || y < m->y0 - 30 || y > m->y0 + m->h + 30) return;
  double h = !isnan(P->f.hdg) ? P->f.hdg : !isnan(P->f.cog) ? P->f.cog : 0;
  double c = cos(P->f.lat * D2R);
  map_xy(m, P->f.lat + 0.01 * cos(h * D2R), P->f.lon + 0.01 * sin(h * D2R) / (c > 0.01 ? c : 0.01), &ax, &ay);
  float ang = atan2f(ax - x, -(ay - y));
  /* the COG vector: 6 minutes ahead */
  if (!isnan(P->f.cog) && !isnan(P->f.sog) && P->f.sog > 0.3) {
    double d = P->f.sog / 10 / 60;
    float vx, vy;
    map_xy(m, P->f.lat + d * cos(P->f.cog * D2R), P->f.lon + d * sin(P->f.cog * D2R) / (c > 0.01 ? c : 0.01), &vx, &vy);
    if (hypotf(vx - x, vy - y) > 30) for (int k = 0; k < 40; k += 3) {
      float t = k / 40.f;
      gfx_pixel((int)(x + (vx - x) * t), (int)(y + (vy - y) * t), 1);
    }
  }
  float nx, ny;                                           /* her true length on this chart */
  map_xy(m, P->f.lat + 95.0 / 110574, P->f.lon, &nx, &ny);
  float L = hypotf(nx - x, ny - y);
  ship_sprite((int)lroundf(x), (int)lroundf(y), ang, frame, P->rot, !isnan(P->f.sog) && P->f.sog > 0.5, L < 40 ? 40 : L > 160 ? 160 : L);
  /* the box: to the next waypoint */
  char s[40], t[16];
  int bx = 2, by = m->y0 + 2;
  gfx_rect(bx, by, 140, 54, 0); gfx_frame(bx, by, 140, 54);
  sprintf(s, "%s  %s kn", navfix_src_name(&P->f), (fmt_kn(t, P->f.sog), t)); tiny(bx + 3, by + 2, s);
  if (R && P->p.active && !P->p.done) {
    fmt_deg(t, P->p.brg); sprintf(s, "WP%d BRG %s", P->p.user_no, t); nt(bx + 3, by + 11, s);
    fmt_eta(t, P->p.ttg_user); sprintf(s, "%.2f nm %s", P->p.dtg_user, t); nt(bx + 3, by + 26, s);
    sprintf(s, "XTE %.2f%s  FINAL %.0f nm", fabs(P->p.xte), P->p.xte >= 0 ? "R" : "L", P->p.dtg_final); tiny(bx + 3, by + 43, s);
  } else {
    fmt_deg(t, P->f.cog); sprintf(s, "COG %s", t); nt(bx + 3, by + 11, s);
    fmt_deg(t, P->f.hdg); sprintf(s, "HDG %s", t); nt(bx + 3, by + 26, s);
  }
  const char *a = gps_alarm_text();
  if (a) { int w = nt_w(a); lcd_fill_rect((uint32_t)(LCD_X / 2 - w / 2 - 4), (uint32_t)(m->y0 + m->h - 18), (uint32_t)(w + 8), 16, 1); nt_inv(LCD_X / 2 - w / 2, m->y0 + m->h - 18, a); }
}

/* ------------------------------------------------------------------ the pages */
int gps_anim(int page) {
  if (!P) return 0;
  if (page == GP_BOW) return 250;
  return 0;
}

int gps_editing(void) { return P && (P->edit >= 0 || P->gyro_edit); }

void gps_draw(int page, uint32_t frame) {
  if (!P) { nav_header("GPS"); nt(6, 40, "GPS: not enough memory"); return; }
  switch (page) {
  case GP_FIX: draw_fix(); break;
  case GP_SATS: draw_sats(); break;
  case GP_NAV: draw_nav(); break;
  case GP_BOW: draw_bow(frame); break;
  case GP_SET: draw_set(); break;
  }
}

int gps_key(int page, int key) {
  if (!P) return 0;
  if (page == GP_SET) { if (key == KEY_EXIT && P->edit < 0) return 0; set_key(key); return 1; }
  if (P->gyro_edit) {
    if (key == KEY_ENTER) {
      double v = atof(P->ebuf);
      if (P->elen && v >= 0 && v <= 360) { P->gyro_obs = v; P->gyro_have = 1; }
      P->gyro_edit = 0;
    } else if (key == KEY_EXIT || key == KEY_F6) P->gyro_edit = 0;
    else edit_key(key, 0);
    return 1;
  }
  if (key == KEY_RUN) { gps_mob(); return 1; }
  if (key == KEY_ENTER && (P->alarm & ~P->ack)) { P->ack |= P->alarm; P->event = 0; return 1; }
  double pk;
  const route_t *R;
  switch (page) {
  case GP_FIX:
    if (key == KEY_7) {
      if (P->anc) P->anc = 0;
      else if (P->f.src == SRC_GPS || P->f.src == SRC_DR) { P->anc = 1; P->anc_lat = P->f.lat; P->anc_lon = P->f.lon; }
      return 1;
    }
    if (key == KEY_ENTER) { P->gyro_edit = 1; P->elen = 0; P->ebuf[0] = 0; return 1; }
    break;
  case GP_SATS:
    if (key == KEY_0) { P->stars ^= 1; return 1; }
    if (key == KEY_4) { P->sat_sys = (P->sat_sys + GNSS_N) % (GNSS_N + 1); return 1; }
    if (key == KEY_6) { P->sat_sys = (P->sat_sys + 1) % (GNSS_N + 1); return 1; }
    break;
  case GP_NAV:
  case GP_BOW:
    R = nav_route(&pk);
    if ((key == KEY_4 || key == KEY_6) && R) {
      psg_step(&P->p, R, key == KEY_6 ? 1 : -1);
      P->had_route = 1;
      int adv = P->p.auto_adv;
      P->p.auto_adv = 0;                                 /* by hand: this leg, even inside its arrival circle */
      psg_update(&P->p, R, nav_ell(), P->f.lat, P->f.lon, P->f.sog, pk);
      P->p.auto_adv = adv;
      return 1;
    }
    if (key == KEY_5 && R) {
      psg_start(&P->p, R, nav_ell(), P->f.lat, P->f.lon);
      P->had_route = 1;
      psg_update(&P->p, R, nav_ell(), P->f.lat, P->f.lon, P->f.sog, pk);
      return 1;
    }
    if (key == KEY_0) { nav_chart_live(page); return 1; }
    break;
  }
  return 0;
}
