/* passage_test.c - NAV47 GPS: src/passage.c on the PC.
 *   1. legs (geodesic and rhumb) and points off them: printed as X lines, checked by test/passage_verify.py against
 *      geographiclib (the nearest point of the leg found by search);
 *   2. a passage sailed: 3 user waypoints, the vessel 0.1 nm right of the track at 10 kn, a fix a minute: the leg
 *      advances at the arrival circle, ETA, the XTE alarm when it strays.
 * make test-gps. GPL-3.0-or-later. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "geodesic.h"
#include "geo_route.h"
#include "passage.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define NEAR(a, b, e) do { checks++; if (!(fabs((a) - (b)) <= (e))) { fails++; fprintf(stderr, "FAIL %s:%d: %s = %.6f, not %.6f\n", __FILE__, __LINE__, #a, (double)(a), (double)(b)); } } while (0)

static route_t R;
static struct geod_geodesic G;

static void legs(void) {
  static const double L[][4] = {
    { 26.30, 50.30, 26.80, 51.10 },     /* Gulf, short */
    { 40.64, -73.78, 51.47, -0.46 },    /* the Atlantic */
    { -33.92, 18.42, -37.82, 144.96 },  /* the Southern Ocean */
    { 60.0, 179.0, 61.0, -175.0 },      /* across 180 */
  };
  static const double OFF[][2] = { { 0.3, 0.2 }, { 0.5, -5 }, { 0.7, 20 }, { 0.05, 1 }, { 0.95, -0.4 }, { 1.2, 3 } };
  for (int sail = 0; sail < 2; sail++)
    for (unsigned i = 0; i < sizeof L / sizeof *L; i++) {
      route_init(&R, &G);
      route_add_leg(&R, L[i][0], L[i][1], L[i][2], L[i][3], sail ? SAIL_RHUMB : SAIL_GEOD, 0, SPLIT_NONE, 0);
      route_finish(&R);
      double s12, a1, a2;
      geod_inverse(&G, L[i][0], L[i][1], L[i][2], L[i][3], &s12, &a1, &a2);
      for (unsigned k = 0; k < sizeof OFF / sizeof *OFF; k++) {
        /* a point: along the geodesic at the fraction, then off it at right angles by the nm given (+ right) */
        struct geod_geodesicline l;
        double la, lo, az, pla, plo, pz;
        geod_inverseline(&l, &G, L[i][0], L[i][1], L[i][2], L[i][3], 0);
        geod_position(&l, s12 * OFF[k][0], &la, &lo, &az);
        geod_direct(&G, la, lo, az + (OFF[k][1] >= 0 ? 90 : -90), fabs(OFF[k][1]) * PSG_NM, &pla, &plo, &pz);
        Passage p;
        psg_init(&p);
        p.active = 1; p.leg = 1; p.auto_adv = 0;
        psg_update(&p, &R, &G, pla, plo, 10, 10);
        printf("X,%c,%.10f,%.10f,%.10f,%.10f,%.10f,%.10f,%.6f,%.6f,%.6f,%.6f,%.6f\n", sail ? 'R' : 'G',
               L[i][0], L[i][1], L[i][2], L[i][3], pla, plo, p.xte, p.atd, p.leg_nm, p.dtg, p.brg);
      }
    }
}

static void sail(void) {
  /* WP1 -> WP2 -> WP3 rhumb lines, split every 5 nm (the split points are not user waypoints) */
  static const double W[3][2] = { { 26.30, 50.30 }, { 26.55, 50.60 }, { 26.50, 51.00 } };
  route_init(&R, &G);
  for (int i = 1; i < 3; i++) route_add_leg(&R, W[0][0], W[0][1], W[i][0], W[i][1], SAIL_RHUMB, 0, SPLIT_DIST, 5 * PSG_NM);
  route_finish(&R);
  CHECK(R.n > 3);
  Passage p;
  psg_init(&p);
  p.xte_lim = 0.25; p.arr_r = 0.2;
  double tot = R.w[R.n - 1].cum_m / PSG_NM;
  psg_start(&p, &R, &G, W[0][0], W[0][1]);
  CHECK(p.active && p.leg == 1);
  int arrivals = 0, finals = 0, users_seen = 0, last_user = 0, wp2 = 1;
  while (!(R.w[wp2].flags & WPT_USER)) wp2++;
  double turn = R.w[wp2].cum_m / PSG_NM;      /* the corner at WP2: the offset point there is off both legs */
  /* sail: along the plan, 0.1 nm right, a minute at a time at 10 kn */
  for (double run = 0.05; run < tot + 0.3; run += 10.0 / 60) {
    double s = run * PSG_NM, la, lo, crs;
    int i = 1;
    while (i < R.n - 1 && R.w[i].cum_m < s) i++;
    const wpt_t *a = &R.w[i - 1];
    double ds = s - a->cum_m;
    if (ds > R.w[i].cum_m - a->cum_m) ds = R.w[i].cum_m - a->cum_m;
    rhumb_direct(&R.rh, a->lat, a->lon, a->rl_crs, ds, &la, &lo);
    crs = a->rl_crs;
    double pla, plo, pz;
    geod_direct(&G, la, lo, crs + 90, 0.1 * PSG_NM, &pla, &plo, &pz);
    int leg0 = p.leg;
    psg_update(&p, &R, &G, pla, plo, 10, 12);
    if (p.event == PSG_A_ARRIVE) { arrivals++; p.event = 0; }
    if (p.event == PSG_A_FINAL) { finals++; p.event = 0; }
    int corner = fabs(run - turn) < 0.5;
    if ((p.leg != leg0 || run < 0.1) && !corner) {
      NEAR(p.xte, 0.1, 0.01);                    /* on every leg: 0.1 nm right */
    }
    if (p.user_no != last_user) { users_seen++; last_user = p.user_no; }
    if (!p.done && !corner) {
      CHECK(!(p.alarm & PSG_A_XTE));
      NEAR(p.dtg_final, tot - run, 0.05);
      NEAR(p.ttg_final, p.dtg_final / 10, 1e-9);
      CHECK(p.dtg_user <= p.dtg_final + 1e-9);
    }
  }
  CHECK(arrivals == 1);                          /* WP2 only: the split points advance silently */
  CHECK(finals == 1 && p.done);
  CHECK(users_seen == 2);                        /* to WP2, then to WP3 */
  /* stray: 0.4 nm left of the first leg: the XTE alarm */
  psg_start(&p, &R, &G, W[0][0], W[0][1]);
  double la, lo, z;
  geod_direct(&G, W[0][0], W[0][1], R.w[0].rl_crs, 2 * PSG_NM, &la, &lo, &z);
  geod_direct(&G, la, lo, R.w[0].rl_crs - 90, 0.4 * PSG_NM, &la, &lo, &z);
  psg_update(&p, &R, &G, la, lo, 0, 12);
  CHECK(p.alarm & PSG_A_XTE);
  NEAR(p.xte, -0.4, 0.01);
  NEAR(p.kn, 12, 1e-9);                          /* stopped: the plan's speed for the ETA */
  /* start in the middle of the second user leg: that leg */
  int mid = R.n - 2;
  psg_start(&p, &R, &G, R.w[mid].lat + 0.001, R.w[mid].lon + 0.003);
  CHECK(p.leg >= mid);
  /* NEXT / PREV */
  psg_step(&p, &R, -100); CHECK(p.leg == 1);
  psg_step(&p, &R, 100); CHECK(p.leg == R.n - 1);
}

int main(void) {
  geod_init(&G, 6378137, 1 / 298.257223563);
  legs();
  sail();
  fprintf(stderr, "passage: %d checks, %d failed\n", checks, fails);
  return fails != 0;
}
