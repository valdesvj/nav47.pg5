/* astro_cli.c - PC driver for tests/test_astro.py: reads "jd lat lon" lines, prints every result of the engine. */
#include <stdio.h>
#include "../src/astro.h"

int main(void) {
  double j, lat, lon;
  while (scanf("%lf %lf %lf", &j, &lat, &lon) == 3) {
    Sun s;
    sun_at(&s, j);
    printf("SUN %.17g %.17g %.17g %.17g %.17g %.17g\n", s.gha, s.dec, s.aries, s.sd, s.dpsi, s.deps);
    double g, d, h, sd, sha;
    moon_pos(&s, &g, &d, &h, &sd);
    printf("MOON %.17g %.17g %.17g %.17g\n", g, d, h, sd);
    for (int p = 1; p <= 4; p++) {
      planet_pos(&s, p, 0, &g, &d, &sha, &h);
      printf("PL%d %.17g %.17g %.17g %.17g\n", p, g, d, sha, h);
      double dist;
      planet_quick(&s, p, &g, &d, &dist);
      printf("PQ%d %.17g %.17g %.17g\n", p, g, d, dist);
    }
    moon_quick(&s, &g, &d);
    printf("MQ %.17g %.17g\n", g, d);
    for (int n = 1; n <= 58; n++) {
      star_pos(&s, n, &g, &d, &sha);
      double hc, zn;
      hcz(lat, lon, d, g, &hc, &zn);
      printf("ST%d %.17g %.17g %.17g %.17g %.17g\n", n, g, d, sha, hc, zn);
    }
    double j0 = day0(j);
    printf("EV");
    for (int k = 0; k < EV_COUNT; k++) printf(" %.17g", sun_event(j0, lat, lon, k));
    printf("\n");
    double il, age;
    moon_phase(&s, &il, &age);
    printf("PH %.17g %.17g\n", il, age);
  }
  return 0;
}
