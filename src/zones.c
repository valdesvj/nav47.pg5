/* zones.c - NAV47 time zones: the city offset with DST computed on the calculator (as ZONE47).
 * A rule: DST from (month, n-th weekday, hour of standard time) start to end; n 5 = the last; when the start month
 * is after the end month (southern hemisphere) the DST period wraps over the new year.
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#include <math.h>
#include "zones.h"

static long jdn_ymd(int y, int m, int d) {
  long a = (14 - m) / 12, yy = y + 4800 - a, mm = m + 12 * a - 3;
  return d + (153 * mm + 2) / 5 + 365 * yy + yy / 4 - yy / 100 + yy / 400 - 32045;
}

static void ymd_jdn(long n, int *y, int *m, int *d) {
  long a = n + 32044, b = (4 * a + 3) / 146097, c = a - 146097 * b / 4;
  long dd = (4 * c + 3) / 1461, e = c - 1461 * dd / 4, mm = (5 * e + 2) / 153;
  *y = (int)(100 * b + dd - 4800 + mm / 10);
  *m = (int)(mm + 3 - 12 * (mm / 10));
  *d = (int)(e - (153 * mm + 2) / 5 + 1);
}

/* the JDN of the n-th (5: last) weekday wd (0 = Sunday) of month m of year y */
static long nth_wday(int y, int m, int n, int wd) {
  if (n >= 5) {
    int ny = m == 12 ? y + 1 : y, nm = m == 12 ? 1 : m + 1;
    long last = jdn_ymd(ny, nm, 1) - 1;
    long dow = (last + 1) % 7;                 /* 0 = Sunday */
    return last - ((dow - wd + 7) % 7);
  }
  long first = jdn_ymd(y, m, 1);
  long dow = (first + 1) % 7;
  return first + (wd - dow + 7) % 7 + 7 * (n - 1);
}

/* the rule's instant as hours of local standard time from JDN 0 */
static double rule_hours(int y, const DstRule *r) {
  return (double)nth_wday(y, r->month, r->n, r->wday) * 24.0 + r->hour;
}

double city_offset(const Zone *z, const City *c, double jd_ut, int *on) {
  *on = 0;
  if (c->kind != DST_RULE) return z->off;
  double std_h = (jd_ut + 0.5) * 24.0 + z->off;           /* local standard time, hours from JDN 0 at 0h */
  long n = (long)floor(std_h / 24.0);
  int y, m, d;
  ymd_jdn(n, &y, &m, &d);
  double s = rule_hours(y, &c->start), e = rule_hours(y, &c->end);
  int dst = (s < e) ? (std_h >= s && std_h < e) : (std_h >= s || std_h < e);
  *on = dst;
  return z->off + (dst ? c->dst : 0.0);
}

int zone_nearest(double tz, double jd_ut) {
  int best = 0;
  double bd = 1e9;
  for (int i = 0; i < NZONES; i++) {
    int on;
    double now = city_offset(&ZONES[i], &ZONES[i].c[0], jd_ut, &on), std = ZONES[i].off;
    double a = fabs(std - tz), b = fabs(now - tz) + (now - std) / 100;
    double dd = a < b ? a : b;
    if (dd < bd) { bd = dd; best = i; }
  }
  return best;
}
