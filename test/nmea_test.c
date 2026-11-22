/* nmea_test.c - NAV47 GPS: src/nmea.c on the PC. Known sentences, then a log made by test/nmea/make_log.py
 * (a blank line ends each second). make test-gps. GPL-3.0-or-later. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "nmea.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define NEAR(a, b, e) CHECK(fabs((a) - (b)) <= (e))

static GpsState G;

static void known(void) {
  nmea_init(&G);
  CHECK(nmea_checksum_ok("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47") == 1);
  CHECK(nmea_checksum_ok("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*48") == 0);
  CHECK(nmea_feed(&G, "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n", 1000) == NM_GGA);
  NEAR(G.lat, 48 + 7.038 / 60, 1e-9);
  NEAR(G.lon, 11 + 31.0 / 60, 1e-9);
  CHECK(G.q == Q_GPS && G.nused == 8 && G.have_fix);
  NEAR(G.hdop, 0.9, 1e-9); NEAR(G.alt, 545.4, 1e-9);
  CHECK(isnan(G.dgps_age) && G.dgps_id == -1);
  NEAR(G.utc, 12 * 3600 + 35 * 60 + 19, 1e-9);
  CHECK(nmea_feed(&G, "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A", 2000) == NM_RMC);
  NEAR(G.sog, 22.4, 1e-9); NEAR(G.cog, 84.4, 1e-9); NEAR(G.magvar, -3.1, 1e-9);
  CHECK(G.y == 1994 && G.mo == 3 && G.d == 23);
  CHECK(nmea_feed(&G, "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6B", 3000) == NM_BAD);
  CHECK(G.n_bad == 1 && G.n_ok == 2);
  CHECK(nmea_feed(&G, "garbage", 3000) == NM_BAD);
  /* no checksum: accepted */
  CHECK(nmea_feed(&G, "$HEHDT,271.5,T", 4000) == NM_HDT);
  NEAR(G.hdg, 271.5, 1e-9);
  /* THS invalid: ignored */
  CHECK(nmea_feed(&G, "$HETHS,10.0,V", 5000) == NM_THS);
  NEAR(G.hdg, 271.5, 1e-9);
  /* HDG: 100 magnetic, deviation 2 W, variation 5 E: 103 true */
  CHECK(nmea_feed(&G, "$HCHDG,100.0,2.0,W,5.0,E", 6000) == NM_HDG);
  NEAR(G.hdg, 103, 1e-9);
  /* VHW in km/h only */
  CHECK(nmea_feed(&G, "$VWVHW,,T,,M,,N,18.52,K", 7000) == NM_VHW);
  NEAR(G.stw, 10, 1e-9);
  /* GNS: GPS differential, GLONASS autonomous: DGPS */
  CHECK(nmea_feed(&G, "$GNGNS,014035.00,4332.69262,S,17235.48549,E,DA,13,0.9,25.0,-1.5,5.0,0023", 8000) == NM_GNS);
  CHECK(G.q == Q_DGPS && G.dgps_id == 23 && G.lat < -43.5 && G.lon > 172.5);
  NEAR(G.dgps_age, 5, 1e-9);
  /* GGA without a fix: the position kept, have_fix 0 */
  double la = G.lat;
  CHECK(nmea_feed(&G, "$GPGGA,014036.00,,,,,0,00,,,M,,M,,", 9000) == NM_GGA);
  CHECK(!G.have_fix && G.lat == la && G.fix_ms == 8000 && nmea_qual(&G) == Q_NONE);
  /* GN numbering: 70 is GLONASS, 40 SBAS, 302 Galileo */
  nmea_feed(&G, "$GNGSA,A,3,70,40,302,,,,,,,,,,1.2,0.7,1.0", 10000);
  int u;
  nmea_count(&G, GNSS_GLO, &u, 10000); CHECK(u == 1);
  nmea_count(&G, GNSS_SBAS, &u, 10000); CHECK(u == 1);
  nmea_count(&G, GNSS_GAL, &u, 10000); CHECK(u == 1);
  nmea_count(&G, GNSS_GAL, &u, 14000); CHECK(u == 0);     /* used for NMEA_USE_TTL only */
  CHECK(!strcmp(nmea_qual_name(Q_RTK), "RTK"));
}

static void logfile(const char *path) {
  FILE *f = fopen(path, "r");
  if (!f) { printf("FAIL: no %s\n", path); fails++; return; }
  nmea_init(&G);
  char l[256];
  int t = 0;
  uint32_t lost_fix_ms = 0;
  while (fgets(l, sizeof l, f)) {
    if (l[0] == '\n' || l[0] == '\r') {             /* the end of second t */
      uint32_t now = (uint32_t)t * 1000;
      if (t == 10) {
        CHECK(G.have_fix && nmea_qual(&G) == Q_DGPS && G.dgps_id == 412);
        NEAR(G.dgps_age, 4, 1e-9);
        int used, n;
        n = nmea_count(&G, GNSS_GPS, &used, now); CHECK(n == 9 && used == 8);
        n = nmea_count(&G, GNSS_SBAS, &used, now); CHECK(n == 2 && used == 0);
        n = nmea_count(&G, GNSS_GLO, &used, now); CHECK(n == 6 && used == 4);
        n = nmea_count(&G, GNSS_GAL, &used, now); CHECK(n == 7 && used == 6);
        n = nmea_count(&G, GNSS_BDS, &used, now); CHECK(n == 5 && used == 4);
        CHECK(G.fix_dim == 3);
        NEAR(G.sog, 10.07, 0.05); NEAR(G.cog, 51.8, 0.05);
        NEAR(G.hdg, 45, 1e-9); NEAR(G.stw, 10, 1e-9);
        NEAR(G.sig_lat, 0.6, 1e-9);
        double jd;
        CHECK(nmea_jd(&G, &jd)); NEAR(jd, 2461366.75 + 10 / 86400.0, 1e-7);
        NEAR(G.lat, 26.30, 0.01); NEAR(G.lon, 50.30, 0.01);
      }
      if (t == 299) lost_fix_ms = G.fix_ms;
      if (t == 320) {                              /* the fix lost at 300 s */
        CHECK(!G.have_fix && G.fix_ms == lost_fix_ms && nmea_qual(&G) == Q_NONE);
        int used; nmea_count(&G, GNSS_GPS, &used, now); CHECK(used == 0);
        NEAR(G.hdg, 45, 1e-9);                      /* the gyro still comes (the turn is at 360 s) */
      }
      if (t == 450) CHECK(G.have_fix && nmea_qual(&G) == Q_GPS && G.dgps_id == -1 && isnan(G.dgps_age));
      t++;
      continue;
    }
    nmea_feed(&G, l, (uint32_t)t * 1000);
  }
  fclose(f);
  CHECK(t == 720);
  CHECK(G.n_bad == 3 && G.n_unknown == 0);
}

int main(int argc, char **argv) {
  known();
  if (argc > 1) logfile(argv[1]);
  printf("nmea: %d checks, %d failed\n", checks, fails);
  return fails != 0;
}
