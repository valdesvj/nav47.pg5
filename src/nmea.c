/* nmea.c - NAV47 GPS: the NMEA 0183 parser. See nmea.h.
 * Sentences: RMC GGA GNS GSA GSV VTG ZDA GST (any talker: GP GL GA GB BD GQ GI GN), HDT THS HDG (gyro, HE / HC /
 * any), VHW (log). NMEA 4.10's GSA system ID and GSV signal ID are read when present.
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#include "nmea.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAXF 24

void nmea_init(GpsState *g) {
  memset(g, 0, sizeof *g);
  g->alt = g->sep = g->pdop = g->hdop = g->vdop = g->dgps_age = NAN;
  g->sig_lat = g->sig_lon = g->sig_alt = g->sog = g->cog = g->magvar = g->hdg = g->stw = g->utc = NAN;
  g->dgps_id = -1;
}

static int hexv(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

int nmea_checksum_ok(const char *s) {
  if (*s == '$' || *s == '!') s++;
  unsigned x = 0;
  while (*s && *s != '*') x ^= (unsigned char)*s++;
  if (*s != '*') return -1;
  int a = hexv(s[1]), b = hexv(s[2]);
  if (a < 0 || b < 0) return 0;
  return (unsigned)(a * 16 + b) == x;
}

static double num(const char *f) { return *f ? atof(f) : NAN; }
static int inum(const char *f, int dflt) { return *f ? atoi(f) : dflt; }

/* ddmm.mmmm + hemisphere */
static double coord(const char *v, const char *h) {
  if (!*v || !*h) return NAN;
  double x = atof(v);
  double deg = floor(x / 100);
  double r = deg + (x - deg * 100) / 60;
  return (*h == 'S' || *h == 'W') ? -r : r;
}

static double hms(const char *f) {
  if (strlen(f) < 6) return NAN;
  int h = (f[0] - '0') * 10 + f[1] - '0', m = (f[2] - '0') * 10 + f[3] - '0';
  return h * 3600 + m * 60 + atof(f + 4);
}

static int talker_sys(const char *t) {
  if (t[0] == 'G' && t[1] == 'L') return GNSS_GLO;
  if (t[0] == 'G' && t[1] == 'A') return GNSS_GAL;
  if ((t[0] == 'G' && t[1] == 'B') || (t[0] == 'B' && t[1] == 'D')) return GNSS_BDS;
  if (t[0] == 'G' && t[1] == 'Q') return GNSS_QZS;
  if (t[0] == 'G' && t[1] == 'I') return GNSS_NAVIC;
  if (t[0] == 'G' && t[1] == 'P') return GNSS_GPS;
  return -1;                                    /* GN: by the satellite number */
}

/* GP / GN numbering (NMEA 2.x-4.x and the usual receivers) */
static int prn_sys(int prn, int talker) {
  if (talker == GNSS_GPS || talker < 0) {
    if (prn >= 1 && prn <= 32) return GNSS_GPS;
    if (prn >= 33 && prn <= 64) return GNSS_SBAS;
    if (prn >= 65 && prn <= 96) return GNSS_GLO;
    if (prn >= 120 && prn <= 158) return GNSS_SBAS;
    if (prn >= 193 && prn <= 202) return GNSS_QZS;
    if (prn >= 201 && prn <= 263) return GNSS_BDS;
    if (prn >= 301 && prn <= 336) return GNSS_GAL;
    if (prn >= 401 && prn <= 463) return GNSS_BDS;
    return talker < 0 ? GNSS_GPS : talker;
  }
  if (talker == GNSS_QZS && prn >= 1 && prn <= 10) return GNSS_QZS;
  return talker;
}

/* NMEA 4.10 GSA system ID */
static int gsa_sys(int id) {
  static const int m[7] = { -1, GNSS_GPS, GNSS_GLO, GNSS_GAL, GNSS_BDS, GNSS_QZS, GNSS_NAVIC };
  return id >= 1 && id <= 6 ? m[id] : -1;
}

static NmeaSat *sat_get(GpsState *g, int sys, int prn, int make) {
  for (int i = 0; i < g->nsat; i++) if (g->sat[i].sys == sys && g->sat[i].prn == prn) return &g->sat[i];
  if (!make || g->nsat >= NMEA_MAXSAT) return NULL;
  NmeaSat *s = &g->sat[g->nsat++];
  memset(s, 0, sizeof *s);
  s->sys = (uint8_t)sys; s->prn = (uint16_t)prn; s->el = -1; s->az = -1; s->snr = -1;
  return s;
}

void nmea_prune(GpsState *g, uint32_t now) {
  int j = 0;
  for (int i = 0; i < g->nsat; i++)
    if (now - g->sat[i].ms < NMEA_SAT_TTL || now - g->sat[i].used_ms < NMEA_USE_TTL) g->sat[j++] = g->sat[i];
  g->nsat = j;
}

int nmea_sat_used(const GpsState *g, const NmeaSat *s, uint32_t now) {
  (void)g;
  return s->used_ms && now - s->used_ms < NMEA_USE_TTL;
}

int nmea_count(const GpsState *g, int sys, int *used, uint32_t now) {
  int n = 0, u = 0;
  for (int i = 0; i < g->nsat; i++) {
    if (g->sat[i].sys != sys) continue;
    if (g->sat[i].ms && now - g->sat[i].ms < NMEA_SAT_TTL) n++;
    if (nmea_sat_used(g, &g->sat[i], now)) u++;
  }
  if (used) *used = u;
  return n;
}

static void set_pos(GpsState *g, double lat, double lon, uint32_t now) {
  if (isnan(lat) || isnan(lon) || fabs(lat) > 90 || fabs(lon) > 180) { g->have_fix = 0; return; }
  g->lat = lat; g->lon = lon; g->fix_ms = now ? now : 1; g->have_fix = 1;
}

static void set_time(GpsState *g, const char *f, uint32_t now) {
  double t = hms(f);
  if (!isnan(t)) { g->utc = t; g->utc_ms = now; }
}

static void set_hdg(GpsState *g, double h, uint32_t now) {
  if (isnan(h)) return;
  h = fmod(h, 360); if (h < 0) h += 360;
  g->hdg = h; g->hdg_ms = now ? now : 1;
}

int nmea_feed(GpsState *g, const char *line, uint32_t now) {
  char b[128];
  size_t n = strlen(line);
  while (n && (line[n - 1] == '\r' || line[n - 1] == '\n')) n--;
  if (n < 7 || n >= sizeof b || (line[0] != '$' && line[0] != '!')) { g->n_bad++; return NM_BAD; }
  memcpy(b, line, n); b[n] = 0;
  if (nmea_checksum_ok(b) == 0) { g->n_bad++; return NM_BAD; }
  char *star = strchr(b, '*');
  if (star) *star = 0;
  char *f[MAXF];
  int nf = 0;
  for (char *p = b + 1; nf < MAXF;) {
    f[nf++] = p;
    char *c = strchr(p, ',');
    if (!c) break;
    *c = 0; p = c + 1;
  }
  for (int i = nf; i < MAXF; i++) f[i] = "";
  const char *id = f[0];
  if (strlen(id) != 5) { g->n_unknown++; return NM_UNKNOWN; }
  const char *t = id + 2;
  int tsys = talker_sys(id);
  int r = NM_UNKNOWN;

  if (!strcmp(t, "RMC")) {
    /* time, A/V, lat, N, lon, E, sog, cog, ddmmyy, var, E/W, mode */
    set_time(g, f[1], now);
    if (strlen(f[9]) == 6) {
      g->d = (f[9][0] - '0') * 10 + f[9][1] - '0';
      g->mo = (f[9][2] - '0') * 10 + f[9][3] - '0';
      g->y = 2000 + (f[9][4] - '0') * 10 + f[9][5] - '0';
      if (g->y >= 2080) g->y -= 100;
    }
    if (*f[12]) g->mode = f[12][0];
    if (f[2][0] == 'A' && g->mode != 'N') {
      set_pos(g, coord(f[3], f[4]), coord(f[5], f[6]), now);
      g->sog = num(f[7]); g->cog = num(f[8]); g->vel_ms = now ? now : 1;
    } else g->have_fix = 0;
    double v = num(f[10]);
    if (!isnan(v)) g->magvar = f[11][0] == 'W' ? -v : v;
    r = NM_RMC;
  } else if (!strcmp(t, "GGA") || !strcmp(t, "GNS")) {
    /* GGA: time, lat, N, lon, E, q, nsat, hdop, alt, M, sep, M, age, station
       GNS: time, lat, N, lon, E, modes, nsat, hdop, alt, sep, age, station */
    int gns = t[1] == 'N';
    set_time(g, f[1], now);
    int q;
    if (gns) {
      q = Q_NONE;
      for (const char *m = f[6]; *m; m++) {     /* the best of the systems' modes */
        int k = *m == 'A' ? Q_GPS : *m == 'D' ? Q_DGPS : *m == 'P' ? Q_PPS : *m == 'R' ? Q_RTK :
                *m == 'F' ? Q_FLOAT : *m == 'E' ? Q_EST : *m == 'M' ? Q_MAN : *m == 'S' ? Q_SIM : Q_NONE;
        if (k == Q_EST && q == Q_NONE) q = Q_EST;
        else if (k != Q_EST && k > q) q = k;
      }
    } else q = inum(f[6], 0);
    g->q = q;
    g->nused = inum(f[7], g->nused);
    double h = num(f[8]); if (!isnan(h)) g->hdop = h;
    g->alt = num(f[9]);
    g->sep = num(f[gns ? 10 : 11]);
    g->dgps_age = num(f[gns ? 11 : 13]);
    g->dgps_id = inum(f[gns ? 12 : 14], -1);
    if (q > 0) set_pos(g, coord(f[2], f[3]), coord(f[4], f[5]), now);
    else g->have_fix = 0;
    r = gns ? NM_GNS : NM_GGA;
  } else if (!strcmp(t, "GSA")) {
    /* M/A, 1/2/3, 12 PRNs, PDOP, HDOP, VDOP, [system ID] */
    g->fix_dim = inum(f[2], 0);
    int sid = gsa_sys(inum(f[18], 0));
    for (int i = 3; i <= 14; i++) {
      if (!*f[i]) continue;
      int prn = atoi(f[i]);
      int sys = sid >= 0 ? sid : prn_sys(prn, tsys);
      NmeaSat *s = sat_get(g, sys, prn, 1);
      if (s) s->used_ms = now ? now : 1;
    }
    double v;
    if (!isnan(v = num(f[15]))) g->pdop = v;
    if (!isnan(v = num(f[16]))) g->hdop = v;
    if (!isnan(v = num(f[17]))) g->vdop = v;
    r = NM_GSA;
  } else if (!strcmp(t, "GSV")) {
    /* total, number, in view, then 4 x (prn, el, az, snr), [signal ID] */
    int k = (nf - 4) / 4;
    if (k > 4) k = 4;
    for (int i = 0; i < k; i++) {
      const char **q = (const char **)&f[4 + i * 4];
      if (!*q[0]) continue;
      int prn = atoi(q[0]);
      int sys = prn_sys(prn, tsys);
      NmeaSat *s = sat_get(g, sys, prn, 1);
      if (!s) continue;
      int snr = inum(q[3], -1);
      if (s->ms && now - s->ms < 500 && s->snr > snr) snr = s->snr;   /* another signal (L1 / L5) of the same one */
      s->el = (int8_t)inum(q[1], -1);
      s->az = (int16_t)inum(q[2], -1);
      s->snr = (int8_t)(snr > 99 ? 99 : snr);
      s->ms = now ? now : 1;
    }
    nmea_prune(g, now);
    r = NM_GSV;
  } else if (!strcmp(t, "VTG")) {
    /* cog T, T, cog M, M, sog N, N, sog K, K, mode */
    if (f[9][0] != 'N') {
      double c = num(f[1]), s = num(f[5]);
      if (isnan(s)) { s = num(f[7]); if (!isnan(s)) s /= 1.852; }
      if (!isnan(s)) { g->sog = s; g->cog = c; g->vel_ms = now ? now : 1; }
    }
    if (*f[9]) g->mode = f[9][0];
    r = NM_VTG;
  } else if (!strcmp(t, "ZDA")) {
    set_time(g, f[1], now);
    int d = inum(f[2], 0), mo = inum(f[3], 0), y = inum(f[4], 0);
    if (y > 1999 && mo >= 1 && mo <= 12 && d >= 1 && d <= 31) { g->y = y; g->mo = mo; g->d = d; }
    r = NM_ZDA;
  } else if (!strcmp(t, "GST")) {
    /* time, rms, smaj, smin, orient, sig lat, sig lon, sig alt */
    g->sig_lat = num(f[6]); g->sig_lon = num(f[7]); g->sig_alt = num(f[8]); g->gst_ms = now ? now : 1;
    r = NM_GST;
  } else if (!strcmp(t, "HDT")) {
    set_hdg(g, num(f[1]), now);
    r = NM_HDT;
  } else if (!strcmp(t, "THS")) {
    if (*f[2] && f[2][0] != 'V') set_hdg(g, num(f[1]), now);
    r = NM_THS;
  } else if (!strcmp(t, "HDG")) {
    /* magnetic heading, deviation, E/W, variation, E/W: true only when the variation is given */
    double h = num(f[1]), dv = num(f[2]), va = num(f[4]);
    if (!isnan(h) && !isnan(va)) {
      if (!isnan(dv)) h += f[3][0] == 'W' ? -dv : dv;
      h += f[5][0] == 'W' ? -va : va;
      set_hdg(g, h, now);
    }
    r = NM_HDG;
  } else if (!strcmp(t, "VHW")) {
    /* hdg T, T, hdg M, M, stw N, N, stw K, K */
    double s = num(f[5]);
    if (isnan(s)) { s = num(f[7]); if (!isnan(s)) s /= 1.852; }
    if (!isnan(s)) { g->stw = s; g->stw_ms = now ? now : 1; }
    double h = num(f[1]);
    if (!isnan(h) && (!g->hdg_ms || now - g->hdg_ms > 2000)) set_hdg(g, h, now);   /* a gyro sentence wins */
    r = NM_VHW;
  }
  if (r == NM_UNKNOWN) { g->n_unknown++; return r; }
  g->n_ok++;
  g->rx_ms = now ? now : 1;
  return r;
}

int nmea_qual(const GpsState *g) {
  int q = g->q;
  if (q == Q_GPS && g->mode == 'D') q = Q_DGPS;
  if (q == Q_NONE && g->have_fix) q = g->mode == 'D' ? Q_DGPS : g->mode == 'E' ? Q_EST : Q_GPS;   /* RMC only */
  return q;
}

const char *nmea_qual_name(int q) {
  static const char *n[9] = { "NO FIX", "GPS", "DGPS", "PPS", "RTK", "RTK FLT", "EST", "MANUAL", "SIM" };
  return q >= 0 && q <= 8 ? n[q] : "?";
}

const char *gnss_name(int sys) {
  static const char *n[GNSS_N] = { "GPS", "GLO", "GAL", "BDS", "QZS", "SBAS", "NAVIC" };
  return sys >= 0 && sys < GNSS_N ? n[sys] : "?";
}

int nmea_jd(const GpsState *g, double *jd) {
  if (!g->y || isnan(g->utc)) return 0;
  int y = g->y, m = g->mo;
  if (m <= 2) { y--; m += 12; }
  int a = y / 100, b = 2 - a + a / 4;
  *jd = floor(365.25 * (y + 4716)) + floor(30.6001 * (m + 1)) + g->d + b - 1524.5 + g->utc / 86400.0;
  return 1;
}
