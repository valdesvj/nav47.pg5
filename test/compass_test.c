/* compass_test.c - NAV47: src/compass.c checked: the errors on a worked example, a card made from known
 * coefficients fitted back, the interpolation of a short card, the advice over 5 degrees. make test-compass */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "compass.h"

static int fails;
static void near(const char *what, double got, double want, double tol) {
  int ok = fabs(got - want) <= tol;
  if (!ok) fails++;
  printf("%-34s %9.3f  want %9.3f  %s\n", what, got, want, ok ? "ok" : "FAIL");
}

int main(void) {
  CmpOut o;
  /* Zn 139.3, the gyro reads 140.8: 1.5 HIGH; gyro heading 045 -> true 043.5; compass 041 -> 2.5 E; VAR 2.9 E -> 0.4 W */
  cmp_errors(139.3, 140.8, 45.0, 41.0, 2.9, &o);
  near("gyro error (- HIGH)", o.gyro_err, -1.5, 1e-9);
  near("true heading", o.true_hdg, 43.5, 1e-9);
  near("compass error (+ E)", o.comp_err, 2.5, 1e-9);
  near("deviation (+ E)", o.dev, -0.4, 1e-9);
  cmp_errors(2.0, 358.5, 359.0, 357.0, -1.0, &o);      /* across north */
  near("across 000: gyro error", o.gyro_err, 3.5, 1e-9);
  near("across 000: true heading", o.true_hdg, 2.5, 1e-9);
  near("across 000: compass error", o.comp_err, 5.5, 1e-9);
  near("across 000: deviation", o.dev, 6.5, 1e-9);
  cmp_errors(100, 101, NAN, 90, 1, &o);
  printf("%-34s %s\n", "no gyro heading: no deviation", isnan(o.dev) && isnan(o.true_hdg) ? "ok" : "FAIL"); if (!isnan(o.dev)) fails++;

  /* a card from A 0.5, B 4.0, C -2.0, D 1.5, E -0.5 on 8 headings: the coefficients back */
  const double K[5] = { 0.5, 4.0, -2.0, 1.5, -0.5 };
  double h[36], d[36];
  for (int i = 0; i < 8; i++) {
    h[i] = i * 45.0; double r = h[i] * 3.14159265358979323846 / 180;
    d[i] = K[0] + K[1] * sin(r) + K[2] * cos(r) + K[3] * sin(2 * r) + K[4] * cos(2 * r);
  }
  DevFit f;
  dev_fit(8, h, d, &f);
  for (int k = 0; k < 5; k++) { char w[32]; snprintf(w, sizeof w, "8 headings: coefficient %c", "ABCDE"[k]); near(w, f.c[k], K[k], 0.01); }
  near("8 headings: rms", f.rms, 0, 1e-6);
  near("dev_at 100 (the curve)", dev_at(8, h, d, &f, 100), 0.5 + 4 * sin(100 * M_PI / 180) - 2 * cos(100 * M_PI / 180) + 1.5 * sin(200 * M_PI / 180) - 0.5 * cos(200 * M_PI / 180), 1e-6);
  char adv[600];
  dev_advice(&f, adv, sizeof adv);
  printf("advice (max %.1f at %03.0f):\n%s\n", f.max, f.at, adv);
  if (!strstr(adv, "fore-and-aft magnets") || fabs(f.max) <= 5) { fails++; printf("advice FAIL\n"); }

  /* a small card: within 5: no advice; 3 headings: interpolated */
  double h3[3] = { 0, 120, 240 }, d3[3] = { 1.0, -1.0, 2.0 };
  dev_fit(3, h3, d3, &f);
  near("3 headings: interpolated at 060", dev_at(3, h3, d3, &f, 60), 0.0, 1e-9);
  near("3 headings: interpolated at 300", dev_at(3, h3, d3, &f, 300), 1.5, 1e-9);
  dev_advice(&f, adv, sizeof adv);
  printf("%-34s %s\n", "within 5: no advice", adv[0] ? "FAIL" : "ok"); if (adv[0]) fails++;

  printf("compass: %s\n", fails ? "FAILED" : "all ok");
  return fails != 0;
}
