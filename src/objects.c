/* objects.c - NAV47: your objects and their zones. See objects.h. The parser follows web/app.js (parseObjects,
 * parsePos, typeOf), so one objects.csv serves the web page and the calculator.
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "objects.h"
#ifndef OBJ_NO_DMCP
#include <main.h>
#include <dmcp.h>
#include "gfx.h"
#endif

#define D2R (M_PI / 180)

static Obj *O;
static int NO;

static const float ZONE[OB_N] = { 500, 500, 500, 500, 100, 50, 50, 0 };

static int type_of(const char *s0) {
  char s[24];
  int n = 0;
  for (; *s0 && n < 23; s0++) s[n++] = (char)tolower((unsigned char)*s0);
  s[n] = 0;
  static const char *K[OB_N] = { "platform", "rig", "wellhead", "spm", "wreck", "buoy", "light", "port" };
  for (int i = 0; i < OB_N; i++) if (!strncmp(s, K[i], strlen(K[i]))) return i;
  if (strstr(s, "plat") || strstr(s, "jacket") || !strncmp(s, "tp", 2) || strstr(s, "gosp")) return OB_PLATFORM;
  if (strstr(s, "buoy") || strstr(s, "bouy")) return OB_BUOY;
  if (strstr(s, "well")) return OB_WELL;
  if (strstr(s, "rig") || strstr(s, "jack")) return OB_RIG;
  if (strstr(s, "light") || strstr(s, "beacon")) return OB_LIGHT;
  if (strstr(s, "wreck")) return OB_WRECK;
  if (strstr(s, "port") || strstr(s, "berth") || strstr(s, "pier")) return OB_PORT;
  return -1;
}

/* "27 12.0N 049 42.0E", "27.2 49.7", "-33.92 18.42": as web/app.js parsePos. 1 = ok */
typedef struct { char t[16]; } Tok;
static int pos_read(const Tok *tok, int nt, int mins, double *v, char *h) {
  int k = 0;
  for (int i = 0; i < nt && k < 2; i++) {
    if (isalpha((unsigned char)tok[i].t[0])) continue;
    double x = atof(tok[i].t);
    if (mins && !strchr(tok[i].t, '.') && i + 1 < nt && !isalpha((unsigned char)tok[i + 1].t[0]) && fabs(atof(tok[i + 1].t)) < 60) {
      x = (x < 0 || tok[i].t[0] == '-' ? -1 : 1) * (fabs(x) + atof(tok[i + 1].t) / 60);
      i++;
    }
    char hh = 0;
    if (i + 1 < nt && isalpha((unsigned char)tok[i + 1].t[0])) { hh = tok[i + 1].t[0]; i++; }
    if (hh == 'S' || hh == 'W') x = -fabs(x);
    v[k] = x; h[k] = hh; k++;
  }
  return k == 2;
}

int geo_parse_pos(const char *s, double *lat, double *lon) {
  Tok tok[12];
  int nt = 0;
  while (*s && nt < 12) {
    unsigned char c = (unsigned char)toupper((unsigned char)*s);
    if (c == 'N' || c == 'S' || c == 'E' || c == 'W') { tok[nt].t[0] = (char)c; tok[nt].t[1] = 0; nt++; s++; continue; }
    if (isdigit(c) || ((c == '-' || c == '.') && isdigit((unsigned char)s[1]))) {
      int n = 0;
      while ((isdigit((unsigned char)*s) || *s == '.' || (n == 0 && *s == '-')) && n < 15) tok[nt].t[n++] = *s++;
      tok[nt].t[n] = 0;
      nt++;
      continue;
    }
    if (isalpha(c)) return 0;                              /* a word: not a position */
    s++;
  }
  double v[2];
  char h[2];
  if (!pos_read(tok, nt, 1, v, h) && !pos_read(tok, nt, 0, v, h)) return 0;
  if (h[0] == 'E' || h[0] == 'W') { double t = v[0]; v[0] = v[1]; v[1] = t; }
  if (fabs(v[0]) > 90 || fabs(v[1]) > 180) return 0;
  *lat = v[0]; *lon = v[1];
  return 1;
}

static int split(char *l, char **f, int max) {
  int n = 0;
  char *p = l;
  while (n < max) {
    while (*p == ' ') p++;
    f[n++] = p;
    char *c = strpbrk(p, ",;\t");
    char *e = c ? c : p + strlen(p);
    char *q = e;
    while (q > p && (q[-1] == ' ' || q[-1] == '\r')) q--;
    *q = 0;
    if (f[n - 1][0] == '"') { f[n - 1]++; size_t L = strlen(f[n - 1]); if (L && f[n - 1][L - 1] == '"') f[n - 1][L - 1] = 0; }
    if (!c) break;
    p = c + 1;
  }
  return n;
}

static void add(int type, const char *name, double lat, double lon, const char *zone, const char *orient, const char *a, const char *b) {
  if (NO >= OBJ_MAX) return;
  Obj *o = &O[NO++];
  memset(o, 0, sizeof *o);
  o->type = (unsigned char)(type < 0 ? OB_PLATFORM : type);
  snprintf(o->name, sizeof o->name, "%s", name && *name ? name : "?");
  o->lat = lat; o->lon = lon;
  o->zone = zone && *zone && (isdigit((unsigned char)*zone) || *zone == '.') ? (float)atof(zone) : ZONE[o->type];
  o->orient = -1;
  if (orient && *orient && isdigit((unsigned char)*orient)) {
    o->orient = (float)fmod(atof(orient), 360);
    o->a = a && *a ? (float)atof(a) : 0;
    o->b = b && *b ? (float)atof(b) : 0;
    if (o->a <= 0 && o->b <= 0) o->a = o->b = 50;
    else if (o->a <= 0) o->a = o->b;
    else if (o->b <= 0) o->b = o->a;
  }
}

int obj_parse(const char *text) {
  if (!O) O = (Obj *)malloc(OBJ_MAX * sizeof *O);
  if (!O) return 0;
  NO = 0;
  char line[200], *f[12], cols[12][12];
  int ncol = 0;
  const char *p = text;
  while (*p) {
    int n = 0;
    while (*p && *p != '\n' && n < (int)sizeof line - 1) line[n++] = *p++;
    while (*p && *p != '\n') p++;
    if (*p) p++;
    line[n] = 0;
    char *l = line;
    while (*l == ' ' || *l == '\t') l++;
    if (!*l || *l == '#' || *l == '\r') continue;
    int nf = split(l, f, 12);
    char lo[12];
    snprintf(lo, sizeof lo, "%s", f[0]);
    for (char *c = lo; *c; c++) *c = (char)tolower((unsigned char)*c);
    if (!strcmp(lo, "type") || !strcmp(lo, "name")) {       /* a header */
      ncol = nf;
      for (int i = 0; i < nf; i++) {
        int k = 0;
        for (const char *c = f[i]; *c && k < 11; c++) if (isalpha((unsigned char)*c) || *c == '_') cols[i][k++] = (char)tolower((unsigned char)*c);
        cols[i][k] = 0;
      }
      continue;
    }
    if (ncol) {
      int ilat = -1, ilon = -1;
      for (int i = 0; i < ncol; i++) { if (!strcmp(cols[i], "lat")) ilat = i; if (!strcmp(cols[i], "lon")) ilon = i; }
      if (ilat >= 0 && ilon >= 0) {
        const char *g[8] = { 0 };
        static const char *K[8][3] = { { "type", "kind", 0 }, { "name", "title", 0 }, { "zone_m", "zone", "radius" }, { "orient", "orientation", "face" },
                                       { "face_m", "length", "a" }, { "side_m", "width", "b" } };
        for (int k = 0; k < 6; k++)
          for (int i = 0; i < ncol && i < nf && !g[k]; i++)
            for (int j = 0; j < 3 && K[k][j]; j++) if (!strcmp(cols[i], K[k][j])) g[k] = f[i];
        char ps[64];
        double la, lon;
        snprintf(ps, sizeof ps, "%s %s", ilat < nf ? f[ilat] : "", ilon < nf ? f[ilon] : "");
        if (geo_parse_pos(ps, &la, &lon)) add(g[0] ? type_of(g[0]) : -1, g[1], la, lon, g[2], g[3], g[4], g[5]);
        continue;
      }
    }
    /* type, name, lat, lon, ... or name, lat, lon, ... */
    char ps[96];
    double la, lon;
    int ty = type_of(f[0]);
    if (ty >= 0 && nf >= 4) { snprintf(ps, sizeof ps, "%s %s", f[0], f[1]); if (geo_parse_pos(ps, &la, &lon)) ty = -1; }
    else ty = -1;
    char **r = ty >= 0 ? f + 1 : f;
    int nr = ty >= 0 ? nf - 1 : nf;
    if (nr < 2) continue;
    snprintf(ps, sizeof ps, "%s %s", nr > 1 ? r[1] : "", nr > 2 ? r[2] : "");
    int extra = 3;
    if (!geo_parse_pos(ps, &la, &lon)) {
      ps[0] = 0;
      for (int i = 1; i < nr && strlen(ps) < sizeof ps - 2; i++) { strncat(ps, r[i], sizeof ps - strlen(ps) - 2); strcat(ps, " "); }
      if (!geo_parse_pos(ps, &la, &lon)) continue;
      extra = nr;
    }
    const char *z = extra < nr ? r[extra] : NULL;
    add(ty, r[0], la, lon, z, extra + 1 < nr ? r[extra + 1] : NULL, extra + 2 < nr ? r[extra + 2] : NULL, extra + 3 < nr ? r[extra + 3] : NULL);
  }
  return NO;
}

int obj_count(void) { return NO; }
const Obj *obj_get(int i) { return i >= 0 && i < NO ? &O[i] : NULL; }

const Obj *obj_inside(double lat, double lon) {
  for (int i = 0; i < NO; i++) {
    if (O[i].zone <= 0) continue;
    double dl = fmod(lon - O[i].lon + 540.0, 360.0) - 180.0;
    double e = dl * cos(lat * D2R) * 111320, n = (lat - O[i].lat) * 110574;
    if (hypot(e, n) < O[i].zone) return &O[i];
  }
  return NULL;
}

int obj_near(double lat, double lon, double cog, double sog, double max_nm, ObjNear *out, int max) {
  int n = 0;
  double ve = 0, vn = 0;
  if (!isnan(cog) && !isnan(sog) && sog > 0.3) { ve = sog * sin(cog * D2R); vn = sog * cos(cog * D2R); }
  for (int i = 0; i < NO; i++) {
    double dl = fmod(O[i].lon - lon + 540.0, 360.0) - 180.0;
    double e = dl * cos(lat * D2R) * 60, nn = (O[i].lat - lat) * 60, d = hypot(e, nn);
    if (d > max_nm) continue;
    ObjNear z = { &O[i], d, fmod(atan2(e, nn) / D2R + 360.0, 360.0), d, -1, 0 };
    double v2 = ve * ve + vn * vn;
    if (v2 > 0) {
      double t = (e * ve + nn * vn) / v2;                /* hours to the closest point */
      z.tcpa = t;
      if (t > 0) z.cpa = hypot(e - ve * t, nn - vn * t);
    }
    z.warn = O[i].zone > 0 && (d * 1852 < O[i].zone || (z.tcpa > 0 && z.tcpa < 0.5 && z.cpa * 1852 < O[i].zone));
    int k;                                               /* insert, by distance: the nearest max kept */
    if (n < max) k = n++;
    else if (d < out[max - 1].nm) k = max - 1;
    else continue;
    while (k > 0 && out[k - 1].nm > d) { out[k] = out[k - 1]; k--; }
    out[k] = z;
  }
  return n;
}

#ifndef OBJ_NO_DMCP
int obj_load(void) {
  if (!sys_disk_ok() || !file_exists(OBJ_FILE)) return -1;
  FIL f;
  if (f_open(&f, OBJ_FILE, FA_READ) != FR_OK) return -1;
  UINT n = (UINT)f_size(&f), br = 0;
  if (n > 60000) n = 60000;
  char *b = (char *)malloc(n + 1);
  if (!b) { f_close(&f); return -1; }
  f_read(&f, b, n, &br);
  f_close(&f);
  b[br] = 0;
  int k = obj_parse(b);
  free(b);
  return k;
}

/* the point d metres from (lat, lon) towards brg */
static void off(double lat, double lon, double brg, double d, double *la, double *lo) {
  *la = lat + d * cos(brg * D2R) / 110574;
  *lo = lon + d * sin(brg * D2R) / (111320 * cos(lat * D2R));
}

static void symbol(int x, int y, int t) {
  switch (t) {
  case OB_PLATFORM: gfx_rect(x - 4, y - 4, 9, 9, 0); gfx_frame(x - 4, y - 4, 9, 9); gfx_line(x - 4, y - 4, x + 4, y + 4, 1); gfx_line(x - 4, y + 4, x + 4, y - 4, 1); break;
  case OB_RIG: gfx_line(x, y - 5, x - 5, y + 4, 1); gfx_line(x, y - 5, x + 5, y + 4, 1); gfx_line(x - 5, y + 4, x + 5, y + 4, 1); gfx_line(x, y - 5, x, y + 4, 1); break;
  case OB_WELL: gfx_circle(x, y, 3, 1); gfx_line(x - 6, y, x + 6, y, 1); gfx_line(x, y - 6, x, y + 6, 1); break;
  case OB_SPM: gfx_circle(x, y, 4, 1); gfx_disc(x, y, 1, 1); break;
  case OB_WRECK: gfx_line(x - 5, y, x + 5, y, 1); gfx_line(x - 3, y - 3, x - 3, y + 3, 1); gfx_line(x, y - 3, x, y + 3, 1); gfx_line(x + 3, y - 3, x + 3, y + 3, 1); break;
  case OB_BUOY: gfx_circle(x, y, 3, 1); break;
  case OB_LIGHT: gfx_line(x - 4, y, x + 4, y, 1); gfx_line(x, y - 4, x, y + 4, 1); gfx_line(x - 3, y - 3, x + 3, y + 3, 1); gfx_line(x - 3, y + 3, x + 3, y - 3, 1); break;
  default: gfx_frame(x - 3, y - 3, 7, 7); gfx_pixel(x, y, 1);
  }
}

static int on_map(const map_t *m, float x, float y) { return x >= m->x0 && x < m->x0 + m->w && y >= m->y0 && y < m->y0 + m->h; }

void obj_chart(const map_t *m) {
  for (int i = 0; i < NO; i++) {
    const Obj *o = &O[i];
    float x, y;
    map_xy(m, o->lat, o->lon, &x, &y);
    if (x < m->x0 - 60 || x > m->x0 + m->w + 60 || y < m->y0 - 60 || y > m->y0 + m->h + 60) continue;
    /* the zone: dotted, when it is 4 px or more */
    if (o->zone > 0) {
      double la, lo;
      float x2, y2;
      off(o->lat, o->lon, 0, o->zone, &la, &lo);
      map_xy(m, la, lo, &x2, &y2);
      float r = fabsf(y2 - y);
      if (r >= 4) {
        int n = r > 40 ? 72 : 36;
        for (int k = 0; k < n; k++) {
          off(o->lat, o->lon, k * 360.0 / n, o->zone, &la, &lo);
          map_xy(m, la, lo, &x2, &y2);
          if (on_map(m, x2, y2)) gfx_pixel((int)x2, (int)y2, 1);
        }
      }
    }
    /* the outline to scale, when it shows */
    int drawn = 0;
    if (o->orient >= 0) {
      double c[4][2];
      float px[4], py[4];
      double h0 = o->orient, a = o->a, b = o->b;
      for (int k = 0; k < 4; k++) {
        double fw = (k == 0 || k == 3) ? b / 2 : -b / 2, sd = (k < 2) ? -a / 2 : a / 2;   /* face 0 looks to orient */
        double la, lo, l2, o2;
        off(o->lat, o->lon, h0, fw, &la, &lo);
        off(la, lo, h0 + 90, sd, &l2, &o2);
        c[k][0] = l2; c[k][1] = o2;
        map_xy(m, l2, o2, &px[k], &py[k]);
      }
      if (fabsf(px[0] - px[2]) + fabsf(py[0] - py[2]) >= 8) {
        for (int k = 0; k < 4; k++) gfx_line((int)px[k], (int)py[k], (int)px[(k + 1) % 4], (int)py[(k + 1) % 4], 1);
        gfx_line((int)px[0], (int)py[0], (int)px[1], (int)py[1], 1);
        drawn = 1;
      }
      (void)c;
    }
    if (!on_map(m, x, y) || y < m->y0 + 6 || y > m->y0 + m->h - 6) continue;   /* the box: not over the top bar */
    if (!drawn) symbol((int)x, (int)y, o->type);
    if (o->zone > 0) {                                  /* the name, when the zone has room */
      double la, lo;
      float x2, y2;
      off(o->lat, o->lon, 0, o->zone, &la, &lo);
      map_xy(m, la, lo, &x2, &y2);
      if (fabsf(y2 - y) >= 7 && x + 7 + text_width(&TINY, o->name) < m->x0 + m->w) draw_text(&TINY, (int)x + 7, (int)y - 4, o->name, 0);
    }
  }
}

/* ---------------------------------------------------------------- BOW: the objects in perspective */
#define CAM_M 20.0                                   /* the eye on the helideck, m above the sea */

typedef struct { double hor, kper, foc, lat, lon, ch, sh; } View;

/* a point (metres east, north of the vessel) at height h m: 0 behind / too near */
static int vproj(const View *v, double e, double n, double h, float *x, float *y) {
  double fw = (e * v->sh + n * v->ch) / 1852, rt = (e * v->ch - n * v->sh) / 1852;
  if (fw < 0.012) return 0;
  *x = (float)(200 + v->foc * rt / fw);
  *y = (float)(v->hor + v->kper / fw * (1 - h / CAM_M));
  return 1;
}

static void quad(const float *x, const float *y) {     /* a face: white, outlined (the painter's way) */
  float y0 = 1e9f, y1 = -1e9f;
  for (int i = 0; i < 4; i++) { if (y[i] < y0) y0 = y[i]; if (y[i] > y1) y1 = y[i]; }
  if (y0 < 16) y0 = 16;
  if (y1 > 207) y1 = 207;
  for (int r = (int)ceilf(y0); r <= (int)y1; r++) {
    float xs[4];
    int k = 0;
    float yc = r + 0.5f;
    for (int i = 0, j = 3; i < 4; j = i++)
      if ((y[i] > yc) != (y[j] > yc)) xs[k++] = x[j] + (yc - y[j]) * (x[i] - x[j]) / (y[i] - y[j]);
    if (k < 2) continue;
    if (xs[0] > xs[1]) { float t = xs[0]; xs[0] = xs[1]; xs[1] = t; }
    int a = (int)lroundf(xs[0]), b = (int)lroundf(xs[1]);
    if (a < 0) a = 0;
    if (b > LCD_X - 1) b = LCD_X - 1;
    if (b >= a) gfx_rect(a, r, b - a + 1, 1, 0);
  }
  for (int i = 0; i < 4; i++) gfx_line((int)lroundf(x[i]), (int)lroundf(y[i]), (int)lroundf(x[(i + 1) % 4]), (int)lroundf(y[(i + 1) % 4]), 1);
}

/* a box: the footprint's corners (e, n), from h0 to h1: the faces the eye sees */
static int box(const View *v, const double c[4][2], double h0, double h1) {
  float bx[4], by[4], tx[4], ty[4];
  for (int i = 0; i < 4; i++)
    if (!vproj(v, c[i][0], c[i][1], h0, &bx[i], &by[i]) || !vproj(v, c[i][0], c[i][1], h1, &tx[i], &ty[i])) return 0;
  double ce = (c[0][0] + c[2][0]) / 2, cn = (c[0][1] + c[2][1]) / 2;
  for (int i = 0; i < 4; i++) {
    int j = (i + 1) % 4;
    double me = (c[i][0] + c[j][0]) / 2, mn = (c[i][1] + c[j][1]) / 2;
    if ((me - ce) * (-me) + (mn - cn) * (-mn) <= 0) continue;   /* faces away from the eye */
    float qx[4] = { bx[i], bx[j], tx[j], tx[i] }, qy[4] = { by[i], by[j], ty[j], ty[i] };
    quad(qx, qy);
  }
  return 1;
}

typedef struct { int i; double d; } Far;
typedef struct { short x0, y0, x1, y1; } Box;

void obj_bow(double lat, double lon, double hdg, int hor, double kper, double foc) {
  View v = { hor, kper, foc, lat, lon, cos(hdg * D2R), sin(hdg * D2R) };
  Far f[64];
  int nf = 0;
  for (int i = 0; i < NO && nf < 64; i++) {
    double dl = fmod(O[i].lon - lon + 540.0, 360.0) - 180.0;
    double e = dl * cos(lat * D2R) * 111320, n = (O[i].lat - lat) * 110574;
    double d = hypot(e, n) / 1852;
    if (d > 8 || O[i].type == OB_PORT) continue;
    f[nf].i = i; f[nf].d = d; nf++;
  }
  for (int a = 1; a < nf; a++) for (int b = a; b > 0 && f[b].d > f[b - 1].d; b--) { Far t = f[b]; f[b] = f[b - 1]; f[b - 1] = t; }
  Box lab[64];
  int nl = 0;
  for (int k = 0; k < nf; k++) {                       /* the farthest first: the nearer hide them */
    const Obj *o = &O[f[k].i];
    double dl = fmod(o->lon - lon + 540.0, 360.0) - 180.0;
    double e0 = dl * cos(lat * D2R) * 111320, n0 = (o->lat - lat) * 110574;
    /* the zone: dots on the sea */
    if (o->zone > 0)
      for (int j = 0; j < 72; j++) {
        float zx, zy;
        if (vproj(&v, e0 + o->zone * sin(j * 5 * D2R), n0 + o->zone * cos(j * 5 * D2R), 0, &zx, &zy) && zx >= 0 && zx < LCD_X && zy < 207)
          gfx_pixel((int)zx, (int)zy, 1);
      }
    /* the footprint and the heights by kind */
    double a = 50, b = 50, rot = 0, legs = 22, top = 42, mast = 0;
    switch (o->type) {
    case OB_PLATFORM: if (o->orient >= 0) { a = o->a; b = o->b; rot = o->orient; } break;
    case OB_RIG: a = 70; b = 60; legs = 25; top = 40; mast = 95; if (o->orient >= 0) { a = o->a; b = o->b; rot = o->orient; } break;
    case OB_WELL: a = b = 14; legs = 14; top = 20; break;
    case OB_SPM: a = b = 12; legs = 0; top = 5; break;
    case OB_BUOY: a = b = 3; legs = 0; top = 4; mast = 7; break;
    case OB_LIGHT: a = b = 5; legs = 0; top = 14; mast = 18; break;
    case OB_WRECK: a = b = 30; legs = 0; top = 0; break;
    }
    /* out of the view (+-55 deg of the bow): gone by, or not yet in sight */
    double rel = fmod(atan2(e0, n0) / D2R - hdg + 540.0, 360.0) - 180.0;
    if (fabs(rel) > 55) continue;
    /* far: drawn bigger (to 4 times at 1.6 nm), so a platform stays a box and not a dot */
    double big = f[k].d / 0.4;
    if (big < 1) big = 1;
    if (big > 4) big = 4;
    a *= big; b *= big;
    double c[4][2];
    for (int j = 0; j < 4; j++) {                      /* face 0 looks to rot: a along it, b the other side */
      double fw = (j == 0 || j == 3) ? b / 2 : -b / 2, sd = j < 2 ? -a / 2 : a / 2;
      c[j][0] = e0 + fw * sin(rot * D2R) + sd * cos(rot * D2R);
      c[j][1] = n0 + fw * cos(rot * D2R) - sd * sin(rot * D2R);
    }
    float px, py, qx, qy;
    if (!vproj(&v, e0, n0, 0, &px, &py)) continue;     /* abeam or astern: gone past */
    vproj(&v, e0 + a / 2 * v.ch, n0 - a / 2 * v.sh, 0, &qx, &qy);
    int small = fabsf(qx - px) < 2.5f;
    int X = (int)px, Y = (int)py, ytop = Y;
    if (small) {                                       /* far: a mark */
      int h = top > 10 ? 6 : 3;
      gfx_line(X, Y, X, Y - h, 1);
      if (top > 10) gfx_rect(X - 2, Y - h - 1, 5, 2, 1);
      ytop = Y - h - 2;
    } else if (o->type == OB_WRECK) {
      gfx_line(X - 4, Y, X + 4, Y, 1); gfx_line(X - 2, Y - 3, X - 2, Y, 1); gfx_line(X + 2, Y - 3, X + 2, Y, 1);
    } else {
      if (legs > 0)                                    /* the jacket's legs, then the deck module on them */
        for (int j = 0; j < 4; j++) {
          float lx0, ly0, lx1, ly1;
          double ie = e0 + (c[j][0] - e0) * 0.8, in = n0 + (c[j][1] - n0) * 0.8;
          if (vproj(&v, ie, in, 0, &lx0, &ly0) && vproj(&v, ie, in, legs, &lx1, &ly1)) {
            gfx_line((int)lx0, (int)ly0, (int)lx1, (int)ly1, 1);
            gfx_line((int)lx0 + 1, (int)ly0, (int)lx1 + 1, (int)ly1, 1);
          }
        }
      box(&v, (const double (*)[2])c, legs, top);
      float mx, my;
      if (mast > 0 && vproj(&v, e0, n0, mast, &mx, &my)) {   /* the derrick, the light, the buoy's topmark */
        float bx2, by2;
        vproj(&v, e0, n0, top, &bx2, &by2);
        gfx_line((int)bx2, (int)by2, (int)mx, (int)my, 1);
        if (o->type == OB_RIG) { gfx_line((int)bx2 - 3, (int)by2, (int)mx, (int)my, 1); gfx_line((int)bx2 + 3, (int)by2, (int)mx, (int)my, 1); }
        else gfx_disc((int)mx, (int)my, 2, 1);
        ytop = (int)my;
      }
      float tx2, ty2;
      if (vproj(&v, e0, n0, top, &tx2, &ty2) && ty2 < ytop) ytop = (int)ty2;
      for (int j = 0; j < 4; j++) { float zx, zy; if (vproj(&v, c[j][0], c[j][1], top, &zx, &zy) && zy < ytop) ytop = (int)zy; }
    }
    char s[32];
    snprintf(s, sizeof s, f[k].d < 9.95 ? "%s %.1f" : "%s", o->name, f[k].d);
    int w = text_width(&TINY, s), lx = X - w / 2, ly = ytop - 10;
    if (lx < 1) lx = 1;
    if (lx + w > LCD_X - 1) lx = LCD_X - 1 - w;
    for (int tries = 0; tries < 4; tries++) {          /* a name over another: higher up, else left out */
      int hit = 0;
      for (int j = 0; j < nl; j++)
        if (lx < lab[j].x1 && lx + w > lab[j].x0 && ly < lab[j].y1 && ly + 9 > lab[j].y0) { hit = 1; break; }
      if (!hit) break;
      ly -= 10;
      if (tries == 3) ly = 0;
    }
    if (ly > 17 && nl < 64) {
      gfx_rect(lx - 1, ly, w + 2, 9, 0);
      draw_text(&TINY, lx, ly, s, 0);
      lab[nl++] = (Box){ (short)(lx - 2), (short)ly, (short)(lx + w + 2), (short)(ly + 9) };
    }
  }
}
#endif
