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

static int parse_pos(const char *s, double *lat, double *lon) {
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
        if (parse_pos(ps, &la, &lon)) add(g[0] ? type_of(g[0]) : -1, g[1], la, lon, g[2], g[3], g[4], g[5]);
        continue;
      }
    }
    /* type, name, lat, lon, ... or name, lat, lon, ... */
    char ps[96];
    double la, lon;
    int ty = type_of(f[0]);
    if (ty >= 0 && nf >= 4) { snprintf(ps, sizeof ps, "%s %s", f[0], f[1]); if (parse_pos(ps, &la, &lon)) ty = -1; }
    else ty = -1;
    char **r = ty >= 0 ? f + 1 : f;
    int nr = ty >= 0 ? nf - 1 : nf;
    if (nr < 2) continue;
    snprintf(ps, sizeof ps, "%s %s", nr > 1 ? r[1] : "", nr > 2 ? r[2] : "");
    int extra = 3;
    if (!parse_pos(ps, &la, &lon)) {
      ps[0] = 0;
      for (int i = 1; i < nr; i++) { strncat(ps, r[i], sizeof ps - strlen(ps) - 2); strcat(ps, " "); }
      if (!parse_pos(ps, &la, &lon)) continue;
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
    if (!on_map(m, x, y)) continue;
    if (!drawn) symbol((int)x, (int)y, o->type);
    if (o->zone > 0) {                                  /* the name, when the zone has room */
      double la, lo;
      float x2, y2;
      off(o->lat, o->lon, 0, o->zone, &la, &lo);
      map_xy(m, la, lo, &x2, &y2);
      if (fabsf(y2 - y) >= 7) draw_text(&TINY, (int)x + 7, (int)y - 4, o->name, 0);
    }
  }
}

void obj_bow(obj_proj_fn proj, double hdg, double lat, double lon) {
  for (int i = 0; i < NO; i++) {
    const Obj *o = &O[i];
    double dl = fmod(o->lon - lon + 540.0, 360.0) - 180.0;
    double d = 60 * hypot(dl * cos(lat * D2R), o->lat - lat);
    if (d > 8) continue;
    float x, y;
    if (!proj(o->lat, o->lon, hdg, &x, &y) || x < -40 || x > LCD_X + 40) continue;
    /* the zone: dots on the sea */
    if (o->zone > 0)
      for (int k = 0; k < 48; k++) {
        double la, lo;
        float zx, zy;
        off(o->lat, o->lon, k * 7.5, o->zone, &la, &lo);
        if (proj(la, lo, hdg, &zx, &zy) && zx >= 0 && zx < LCD_X && zy < 207) gfx_pixel((int)zx, (int)zy, 1);
      }
    /* the structure: its size with the distance (a platform 30 m high) */
    float s = (float)(0.6 / (d > 0.08 ? d : 0.08));
    if (s > 6) s = 6;
    int X = (int)x, Y = (int)y;
    int w = (int)(6 * s) + 2, h = (int)(8 * s) + 3;
    if (o->type == OB_PLATFORM || o->type == OB_RIG) {
      gfx_rect(X - w / 2, Y - h, w, (int)(2 * s) + 1, 1);                 /* the deck */
      for (int k = 0; k < 2; k++) gfx_line(X - w / 2 + 1 + k * (w - 3), Y - h, X - w / 2 + 1 + k * (w - 3) + (k ? 1 : -1), Y, 1);   /* the legs */
      if (o->type == OB_RIG) gfx_line(X, Y - h, X, Y - h - (int)(5 * s) - 2, 1);    /* the derrick */
      else gfx_rect(X - w / 4, Y - h - (int)(2 * s) - 1, w / 2, (int)(2 * s) + 1, 1);
    } else if (o->type == OB_BUOY || o->type == OB_SPM || o->type == OB_LIGHT) {
      gfx_line(X, Y, X, Y - h, 1); gfx_disc(X, Y - h, 1 + (int)s / 2, 1);
    } else symbol(X, Y, o->type);
    draw_text(&TINY, X + w / 2 + 2, Y - h - 2, o->name, 0);
  }
}
#endif
