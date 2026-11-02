/* almdata.h - written by tools/gen_almanac.py: do not edit. */
#ifndef NAV47_ALMDATA_H
#define NAV47_ALMDATA_H

typedef struct { const double (*r)[4]; int n; } Series;

extern const Series VL;
extern const Series VB;
extern const Series VR;
extern const Series EEL;
extern const Series EEB;
extern const Series EER;
extern const Series VNL;
extern const Series VNB;
extern const Series VNR;
extern const Series MAL;
extern const Series MAB;
extern const Series MAR;
extern const Series JUL;
extern const Series JUB;
extern const Series JUR;
extern const Series SAL;
extern const Series SAB;
extern const Series SAR;

#define NU_N 10
extern const double NU[10][9];
#define ST_N 58
extern const double ST[58][4];
#define ML_N 60
extern const double ML[60][6];
#define MB_N 60
extern const double MB[60][6];
#define MCL_N 40
extern const double MCL[40][6];
#define MCB_N 15
extern const double MCB[15][6];
extern const double KEPLER[5][2][6];
extern const unsigned char BRIGHT[58];      /* star numbers by brightness */
extern const char *const STAR_NAME[58];

#endif
