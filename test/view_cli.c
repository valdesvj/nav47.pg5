/* view_cli.c - PC driver for test/test_views.py: "view jd lat lon frame" -> the lit pixels (x row, row 0 top). */
#include <stdio.h>
#include <string.h>
#include "../src/views.h"
#include "../src/c47scr.h"

int main(void) {
  char v[16];
  double j, lat, lon;
  int frame;
  static Al al;
  c47_init();
  while (scanf("%15s %lf %lf %lf %d", v, &j, &lat, &lon, &frame) == 5) {
    al_compute(&al, j, lat, lon);
    int nf = 1;
    if (!strcmp(v, "ALMANAC")) view_almanac(&al);
    else if (!strcmp(v, "CHART")) view_chart(&al);
    else if (!strcmp(v, "SKY")) nf = view_sky(&al, frame);
    else if (!strcmp(v, "SPLIT")) view_split(&al);
    else if (!strcmp(v, "ANIM")) view_anim(&al, 24, 0.5, frame);
    else if (!strcmp(v, "ALLSKY")) view_allsky(&al);
    printf("F %d", nf);
    for (int y = 0; y < SH; y++)
      for (int x = 0; x < SW; x++)
        if (c47_get(x, y)) printf(" %d,%d", x, SH - 1 - y);
    printf("\n");
    fflush(stdout);
  }
  return 0;
}
