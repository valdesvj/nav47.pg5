/* wmm.c - NAV47: the World Magnetic Model (NOAA NCEI / NGA, public domain; the WMM2025 technical report's equations).
 * Degree 12, the coefficients in wmmdata.c. On the stack only (a few KB, freed at once): nothing static (8 KB RAM).
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#include <math.h>
#include "wmm.h"

#define WMM_N   12
#define WMM_A   6371.2                      /* the model's reference radius, km */
#define WGS_A   6378.137                    /* WGS84, km */
#define WGS_F   (1 / 298.257223563)
#define D2R     (3.14159265358979323846 / 180)

void wmm_field(double lat, double lon, double alt_km, double year, WmmField *w) {
  /* the geodetic position (WGS84) to the geocentric (spherical): its latitude and radius */
  const double e2 = WGS_F * (2 - WGS_F), sl = sin(lat * D2R), cl = cos(lat * D2R);
  const double rc = WGS_A / sqrt(1 - e2 * sl * sl);
  const double p = (rc + alt_km) * cl, z = (rc * (1 - e2) + alt_km) * sl, r = sqrt(p * p + z * z);
  const double latc = asin(z / r);
  const double ct = sin(latc), st = cos(latc);           /* cos, sin of the colatitude */
  const double dt = year - WMM_EPOCH;
  /* Schmidt semi-normalised P(n, m) of cos(colatitude) and their derivatives d/d(colatitude) */
  double P[WMM_N + 1][WMM_N + 1], dP[WMM_N + 1][WMM_N + 1];
  P[0][0] = 1; dP[0][0] = 0;
  for (int n = 1; n <= WMM_N; n++) {
    if (n == 1) { P[1][1] = st; dP[1][1] = ct; }
    else {
      double k = sqrt((2.0 * n - 1) / (2.0 * n));
      P[n][n] = k * st * P[n - 1][n - 1];
      dP[n][n] = k * (st * dP[n - 1][n - 1] + ct * P[n - 1][n - 1]);
    }
    for (int m = 0; m < n; m++) {
      double a = sqrt((double)n * n - (double)m * m), b = n >= 2 ? sqrt((double)(n - 1) * (n - 1) - (double)m * m) : 0;
      double p2 = n >= 2 && m <= n - 2 ? P[n - 2][m] : 0, d2 = n >= 2 && m <= n - 2 ? dP[n - 2][m] : 0;
      P[n][m] = ((2.0 * n - 1) * ct * P[n - 1][m] - b * p2) / a;
      dP[n][m] = ((2.0 * n - 1) * (ct * dP[n - 1][m] - st * P[n - 1][m]) - b * d2) / a;
    }
  }
  /* the field in spherical components: north X', east Y', down Z' */
  double X = 0, Y = 0, Z = 0, ar = WMM_A / r, arn = ar * ar;
  double cm[WMM_N + 1], sm[WMM_N + 1];
  for (int m = 0; m <= WMM_N; m++) { cm[m] = cos(m * lon * D2R); sm[m] = sin(m * lon * D2R); }
  for (int n = 1; n <= WMM_N; n++) {
    arn *= ar;                                           /* (a / r)^(n + 2) */
    for (int m = 0; m <= n; m++) {
      int i = n * 13 + m;
      double g = WMM_G[i] + dt * WMM_GD[i], h = WMM_H[i] + dt * WMM_HD[i];
      double gc = g * cm[m] + h * sm[m], gs = g * sm[m] - h * cm[m];
      X += arn * gc * dP[n][m];
      Y += arn * m * gs * P[n][m];
      Z -= arn * (n + 1) * gc * P[n][m];
    }
  }
  Y = st > 1e-10 ? Y / st : Y;                           /* (at a pole: the limit, as the report's own code) */
  /* back to the geodetic north and down */
  double psi = latc - lat * D2R;
  w->x = X * cos(psi) - Z * sin(psi);
  w->y = Y;
  w->z = X * sin(psi) + Z * cos(psi);
  w->h = hypot(w->x, w->y);
  w->f = hypot(w->h, w->z);
  w->decl = atan2(w->y, w->x) / D2R;
  w->incl = atan2(w->z, w->h) / D2R;
  w->ddecl = 0;
}

double wmm_var(double lat, double lon, double year) {
  WmmField w;
  wmm_field(lat, lon, 0, year, &w);
  return w.decl;
}

/* JD (0 h) of January 1 of year y, Gregorian */
static double jan1(int y) { int k = y - 1; return 1721425.5 + 365.0 * k + floor(k / 4.0) - floor(k / 100.0) + floor(k / 400.0); }

double wmm_year(double jd) {
  int y = (int)floor((jd - 2451544.5) / 365.2425) + 2000;
  if (jd < jan1(y)) y--; else if (jd >= jan1(y + 1)) y++;
  return y + (jd - jan1(y)) / (jan1(y + 1) - jan1(y));
}

int wmm_valid(double year) { return year >= WMM_EPOCH - 0.01 && year < WMM_EPOCH + 5.0; }
