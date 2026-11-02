/* zones_cli.c - PC driver for test/test_zones.py: "zone city jd_ut" -> offset and DST flag */
#include <stdio.h>
#include "../src/zones.h"
int main(void) {
  int z, c; double j;
  while (scanf("%d %d %lf", &z, &c, &j) == 3) {
    int on; double o = city_offset(&ZONES[z], &ZONES[z].c[c], j, &on);
    printf("%.17g %d\n", o, on);
  }
  return 0;
}
