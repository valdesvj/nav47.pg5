/* nmea.h - NAV47 GPS: the NMEA 0183 parser (GPS / DGPS, multi-GNSS, gyro, log). No DMCP: the PC tests and the web
 * use it as it is. The caller owns the GpsState (on the heap: 8 KB of static RAM) and gives the time in ms.
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#ifndef NAV47_NMEA_H
#define NAV47_NMEA_H
#include <stdint.h>

#define NMEA_MAXSAT  64
#define NMEA_SAT_TTL 15000u      /* ms: a satellite not in a GSV for this long is dropped */
#define NMEA_USE_TTL 3000u       /* ms: a satellite is "used" while a GSA named it this recently */

enum { GNSS_GPS, GNSS_GLO, GNSS_GAL, GNSS_BDS, GNSS_QZS, GNSS_SBAS, GNSS_NAVIC, GNSS_N };

/* GGA quality (0-8), and the summary nmea_qual() gives (RMC's mode D also counts as DGPS) */
enum { Q_NONE = 0, Q_GPS = 1, Q_DGPS = 2, Q_PPS = 3, Q_RTK = 4, Q_FLOAT = 5, Q_EST = 6, Q_MAN = 7, Q_SIM = 8 };

/* what nmea_feed() read */
enum { NM_BAD = -1, NM_UNKNOWN = 0, NM_RMC, NM_GGA, NM_GNS, NM_GSA, NM_GSV, NM_VTG, NM_ZDA, NM_GST,
       NM_HDT, NM_THS, NM_HDG, NM_VHW };

typedef struct {
  uint16_t prn;                  /* as the receiver numbers it */
  uint8_t  sys;                  /* GNSS_* */
  int8_t   el;                   /* degrees, -1 unknown */
  int16_t  az;                   /* degrees, -1 unknown */
  int8_t   snr;                  /* dB-Hz, -1 not tracked */
  uint32_t ms, used_ms;          /* last in a GSV, last in a GSA (0 never) */
} NmeaSat;

typedef struct {
  /* position: the last valid fix (RMC A, GGA / GNS quality > 0) */
  double   lat, lon;             /* degrees N+ E+ */
  double   alt, sep;             /* m above the geoid, geoid separation (NAN unknown) */
  uint32_t fix_ms;               /* when (0: never) */
  int      have_fix;             /* the last position sentence was valid */
  int      q;                    /* GGA quality Q_*, of the last GGA / GNS */
  char     mode;                 /* RMC / VTG mode: A D E N F R P M S, 0 unknown */
  int      fix_dim;              /* GSA: 1 none, 2 2D, 3 3D, 0 unknown */
  int      nused;                /* GGA: satellites in use */
  double   pdop, hdop, vdop;     /* NAN unknown */
  double   dgps_age;             /* s, NAN: no differential */
  int      dgps_id;              /* reference station, -1 none */
  double   sig_lat, sig_lon, sig_alt;   /* GST 1-sigma errors, m (NAN unknown) */
  uint32_t gst_ms;
  /* motion */
  double   sog, cog;             /* knots, degrees true (NAN unknown) */
  uint32_t vel_ms;
  double   magvar;               /* degrees, E+ (RMC), NAN unknown */
  /* gyro and log */
  double   hdg;                  /* true heading, degrees (HDT, THS, HDG with variation, VHW) */
  uint32_t hdg_ms;
  double   stw;                  /* speed through the water, knots (VHW) */
  uint32_t stw_ms;
  /* time */
  double   utc;                  /* seconds of the UT day, NAN unknown */
  int      y, mo, d;             /* the UT date, y = 0 unknown */
  uint32_t utc_ms;
  /* satellites */
  NmeaSat  sat[NMEA_MAXSAT];
  int      nsat;
  /* counters */
  uint32_t n_ok, n_bad, n_unknown, n_ovf;
  uint32_t rx_ms;                /* last good sentence */
} GpsState;

void nmea_init(GpsState *g);
int  nmea_feed(GpsState *g, const char *line, uint32_t now_ms);   /* one sentence (CR LF optional): NM_* */
void nmea_prune(GpsState *g, uint32_t now_ms);                    /* drops the satellites not seen lately */
int  nmea_qual(const GpsState *g);                                /* Q_*: GGA's, or DGPS from RMC's mode D */
const char *nmea_qual_name(int q);                                /* "NO FIX" "GPS" "DGPS" "RTK" ... */
const char *gnss_name(int sys);                                   /* "GPS" "GLO" "GAL" "BDS" "QZS" "SBAS" "NAVIC" */
int  nmea_sat_used(const GpsState *g, const NmeaSat *s, uint32_t now_ms);
int  nmea_count(const GpsState *g, int sys, int *used, uint32_t now_ms);   /* satellites seen of a system */
int  nmea_jd(const GpsState *g, double *jd);                      /* UT as a Julian date: 1 = known */
int  nmea_checksum_ok(const char *line);                          /* 1 ok, 0 bad, -1 none */

#endif
