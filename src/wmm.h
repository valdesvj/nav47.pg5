/* wmm.h - NAV47: the magnetic variation from the World Magnetic Model (WMM2025, NOAA NCEI / NGA, public domain),
 * degree 12 (src/wmmdata.c, made by tools/gen_wmm.py from WMM.COF). GPL-3.0-or-later (the model's numbers: public
 * domain). Accuracy at sea about 0.3-0.5 deg; local anomalies (some coasts, wrecks) larger: the chart's notes apply. */
#ifndef NAV47_WMM_H
#define NAV47_WMM_H

extern const double WMM_EPOCH;          /* 2025.0: valid WMM_EPOCH .. + 5 */
extern const char WMM_NAME[];
extern const float WMM_G[13 * 13], WMM_H[13 * 13], WMM_GD[13 * 13], WMM_HD[13 * 13];

typedef struct { double decl, incl, h, x, y, z, f, ddecl; } WmmField;   /* deg (E+, down+), nT; ddecl deg/year */

/* lat, lon degrees (geodetic, WGS84), alt km above the ellipsoid, year decimal (2026.9); the field there */
void   wmm_field(double lat, double lon, double alt_km, double year, WmmField *w);
/* the variation (declination), degrees, east +, at sea level */
double wmm_var(double lat, double lon, double year);
/* the decimal year of a JD (UT) */
double wmm_year(double jd);
/* 1 inside the model's 5 years */
int    wmm_valid(double year);
#endif
