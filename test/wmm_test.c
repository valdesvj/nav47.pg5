/* wmm_test.c - NAV47's WMM against NOAA's WMM2025 test values (WMM2025_TestValues.txt): declination, inclination,
 * H X Y Z F. Run: make test-wmm (the test values' file: test/wmm/WMM2025_TestValues.txt). */
#include <math.h>
#include <stdio.h>
#include "wmm.h"
int main(int argc, char **argv) {
  FILE *f = fopen(argc > 1 ? argv[1] : "test/wmm/WMM2025_TestValues.txt", "r");
  if (!f) { puts("no test values"); return 1; }
  char l[512]; int n = 0, bad = 0; double md = 0, mi = 0, mf = 0;
  while (fgets(l, sizeof l, f)) {
    double yr, alt, la, lo, D, I, H, X, Y, Z, F;
    if (l[0] == '#' || sscanf(l, "%lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf", &yr, &alt, &la, &lo, &D, &I, &H, &X, &Y, &Z, &F) != 11) continue;
    WmmField w; wmm_field(la, lo, alt, yr, &w);
    double dd = fabs(w.decl - D), di = fabs(w.incl - I), df = fmax(fmax(fabs(w.x - X), fabs(w.y - Y)), fmax(fabs(w.z - Z), fabs(w.f - F)));
    if (dd > md) md = dd;
    if (di > mi) mi = di;
    if (df > mf) mf = df;
    n++;
    if (dd > 0.01 || di > 0.01 || df > 1) { bad++; printf("FAIL %.1f %.0f km %.0f %.0f: D %.2f (%.2f) I %.2f (%.2f) dF %.1f nT\n", yr, alt, la, lo, w.decl, D, w.incl, I, df); }
  }
  printf("wmm: %d test values, %d failed; worst: declination %.4f deg, inclination %.4f deg, X Y Z F %.2f nT\n", n, bad, md, mi, mf);
  printf("wmm_year: 2026-11-23 12 h = %.4f, 2025-01-01 0 h = %.4f\n", wmm_year(2461368.0), wmm_year(2460676.5));
  return bad != 0;
}
