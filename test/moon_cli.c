#include <stdio.h>
#include "../src/moon47.h"
int main(void) { double j; int s;
  while (scanf("%lf %d", &j, &s) == 2) { Moon47 p; moon47_page(j, s, &p);
    printf("%.17g %.17g %.17g %.17g %d", p.lit, p.age, p.hp, p.sd, p.index);
    for (int k = 0; k < 4; k++) printf(" %.17g %d", p.next_t[k], p.next_k[k]);
    int R[2][2]; long h = 0; for (int dx = -40; dx <= 40; dx++) { int n = moon47_disc_runs(40, p.age, s, dx, R); for (int i = 0; i < n; i++) { h = (h * 31 + R[i][0] * 97 + R[i][1] + dx) % 1000000007; if (h < 0) h += 1000000007; } }
    printf(" %ld\n", h); }
  return 0; }
