/* compass.h - NAV47: the gyro error and the magnetic compass's deviation from a body's azimuth; the deviation card.
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#ifndef NAV47_COMPASS_H
#define NAV47_COMPASS_H

#define DEV_MAX 36                     /* the card's headings at most */
#define DEV_LIMIT 5.0                  /* over it: the compass wants adjusting */

typedef struct {
  double gyro_err;                     /* Zn - the gyro bearing: + LOW (add), - HIGH (subtract) */
  double true_hdg;                     /* the gyro heading + its error */
  double comp_err;                     /* the true heading - the compass course: + east */
  double dev;                          /* the compass error - the variation: + east */
} CmpOut;

/* the errors; NAN in, NAN out for what needs it (no gyro heading: no true heading, no compass error, no deviation) */
void cmp_errors(double zn, double gyro_brg, double gyro_hdg, double compass_hdg, double var, CmpOut *o);

typedef struct {
  double c[5];                         /* A B C D E: dev = A + B sin h + C cos h + D sin 2h + E cos 2h, degrees */
  int    fitted;                       /* 1: the five coefficients (5 headings or more, spread); 0: interpolated */
  double max, at;                      /* the largest deviation over the circle (fitted or the card's), its heading */
  double rms;                          /* the card's points about the fitted curve */
} DevFit;

/* the card: n headings (compass, degrees) and their deviations (+ east) */
void   dev_fit(int n, const double *hdg, const double *dev, DevFit *f);
double dev_at(int n, const double *hdg, const double *dev, const DevFit *f, double h);   /* NAN: no card */
/* the advice when the deviation is over DEV_LIMIT: lines into out (\n between), "" when within */
void   dev_advice(const DevFit *f, char *out, int len);

#endif
