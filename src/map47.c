/* map47.c - NAV47 CHART: Mercator on the ellipsoid, the coastline streamed from the disk, the route over it.
 *
 * COAST_x.BIN (tools/prep_coast.py): header 16 B, a directory of 16 B a polygon (its box in 0.01 deg, the offset of
 * its points, their number), then the points (delta coded). The 50 m directory alone is 22 KB, so nothing is loaded:
 * the directory and the points are read through two 512 B windows of one file, a polygon outside the view is skipped
 * on its box. Longitudes in the file may run past 180 (unrolled); a polygon is tried at -360, 0 and +360.
 * GPL-3.0-or-later. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dmcp.h>
#include "gfx.h"
#include "map47.h"

#define D2R 0.017453292519943295
#define R2D 57.29577951308232
#define LAT_MAX 85.0

/* ---------------------------------------------------------------- projection and view */

void map_init(map_t *m, int x0, int y0, int w, int h, const struct geod_geodesic *g) {
  m->x0 = x0; m->y0 = y0; m->w = w; m->h = h;
  m->e = (float)sqrt(g->f * (2 - g->f));
  m->lon0 = 0; m->psi0 = 0; m->k = (float)(w / (2 * M_PI));
}

float map_psi(const map_t *m, double lat) {
  if (lat > LAT_MAX) lat = LAT_MAX;
  if (lat < -LAT_MAX) lat = -LAT_MAX;
  float s = sinf((float)(lat * D2R));
  return atanhf(s) - m->e * atanhf(m->e * s);
}

double map_lat(const map_t *m, float psi) {
  float phi = 2 * atanf(expf(psi)) - (float)M_PI_2;          /* the sphere, then three corrections */
  for (int i = 0; i < 3; i++) phi = asinf(tanhf(psi + m->e * atanhf(m->e * sinf(phi))));
  return phi * R2D;
}

static double near_lon(double lon, double ref) {              /* lon + n 360 nearest to ref */
  while (lon - ref > 180) lon -= 360;
  while (lon - ref < -180) lon += 360;
  return lon;
}

void map_xy(const map_t *m, double lat, double lon, float *x, float *y) {
  *x = m->x0 + m->w * 0.5f + m->k * (float)((lon - m->lon0) * D2R);
  *y = m->y0 + m->h * 0.5f - m->k * (map_psi(m, lat) - m->psi0);
}

static void clamp_view(map_t *m) {
  float kmin = (float)(m->w / (2 * M_PI)), kmax = (float)(m->w / (0.2 * D2R));   /* 360 deg .. 0.2 deg wide */
  float pmax = map_psi(m, LAT_MAX);
  if (m->k < kmin) m->k = kmin;
  if (m->k > kmax) m->k = kmax;
  if (m->psi0 > pmax) m->psi0 = pmax;
  if (m->psi0 < -pmax) m->psi0 = -pmax;
  m->lon0 = (float)near_lon(m->lon0, 0);
}

void map_fit(map_t *m, double lat_s, double lat_n, double lon_w, double lon_e) {
  float ps = map_psi(m, lat_s), pn = map_psi(m, lat_n);
  double dl = lon_e - lon_w;
  if (dl < 0.5) dl = 0.5;
  if (pn - ps < 0.005f) pn = ps + 0.005f;
  m->lon0 = (float)((lon_w + lon_e) / 2);
  m->psi0 = (ps + pn) / 2;
  float kx = (float)(m->w / (dl * D2R)), ky = m->h / (pn - ps);
  m->k = kx < ky ? kx : ky;
  clamp_view(m);
}

void map_zoom(map_t *m, float f) { m->k *= f; clamp_view(m); }

void map_pan(map_t *m, float fx, float fy) {
  m->lon0 += (float)(fx * m->w / m->k * R2D);
  m->psi0 += fy * m->h / m->k;
  clamp_view(m);
}

float map_span_lon(const map_t *m) { return (float)(m->w / m->k * R2D); }

/* ---------------------------------------------------------------- lines, clipped to the box */

static unsigned pat;                                          /* the dash pattern runs on from line to line */

static void plot(const map_t *m, int x, int y, int style) {
  pat++;
  if (style == MAP_DASH && pat % 9 >= 5) return;
  if (style == MAP_DOT && pat % 3) return;
  gfx_pixel(x, y, 1);
  if (style == MAP_THICK) {
    if (y + 1 < m->y0 + m->h) gfx_pixel(x, y + 1, 1);
    if (x + 1 < m->x0 + m->w) gfx_pixel(x + 1, y, 1);
  }
}

static int outcode(const map_t *m, float x, float y) {
  int c = 0;
  if (x < m->x0) c |= 1; else if (x > m->x0 + m->w - 1) c |= 2;
  if (y < m->y0) c |= 4; else if (y > m->y0 + m->h - 1) c |= 8;
  return c;
}

static void line_f(const map_t *m, float x1, float y1, float x2, float y2, int style) {
  int c1 = outcode(m, x1, y1), c2 = outcode(m, x2, y2);
  float xa = m->x0, xb = m->x0 + m->w - 1, ya = m->y0, yb = m->y0 + m->h - 1;
  while (c1 | c2) {                                           /* Cohen-Sutherland */
    if (c1 & c2) return;
    int c = c1 ? c1 : c2;
    float x, y;
    if (c & 8)      { x = x1 + (x2 - x1) * (yb - y1) / (y2 - y1); y = yb; }
    else if (c & 4) { x = x1 + (x2 - x1) * (ya - y1) / (y2 - y1); y = ya; }
    else if (c & 2) { y = y1 + (y2 - y1) * (xb - x1) / (x2 - x1); x = xb; }
    else            { y = y1 + (y2 - y1) * (xa - x1) / (x2 - x1); x = xa; }
    if (c == c1) { x1 = x; y1 = y; c1 = outcode(m, x1, y1); }
    else         { x2 = x; y2 = y; c2 = outcode(m, x2, y2); }
  }
  int ix = (int)lroundf(x1), iy = (int)lroundf(y1), jx = (int)lroundf(x2), jy = (int)lroundf(y2);
  int dx = abs(jx - ix), sx = ix < jx ? 1 : -1, dy = -abs(jy - iy), sy = iy < jy ? 1 : -1, err = dx + dy;
  for (;;) {                                                  /* Bresenham */
    plot(m, ix, iy, style);
    if (ix == jx && iy == jy) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; ix += sx; }
    if (e2 <= dx) { err += dx; iy += sy; }
  }
}

void map_seg(const map_t *m, double la1, double lo1, double la2, double lo2, int style) {
  float x1, y1, x2, y2;
  lo1 = near_lon(lo1, m->lon0);
  lo2 = near_lon(lo2, lo1);
  map_xy(m, la1, lo1, &x1, &y1);
  map_xy(m, la2, lo2, &x2, &y2);
  line_f(m, x1, y1, x2, y2, style);
}

void map_geod(const map_t *m, const struct geod_geodesic *g, double la1, double lo1, double la2, double lo2, int style) {
  struct geod_geodesicline l;
  geod_inverseline(&l, g, la1, lo1, la2, lo2, 0);
  float x1, y1, x2, y2;
  map_xy(m, la1, near_lon(lo1, m->lon0), &x1, &y1);
  map_xy(m, la2, near_lon(lo2, m->lon0), &x2, &y2);
  int n = (int)(hypotf(x2 - x1, y2 - y1) / 6) + 4;            /* a piece every ~6 px, more where it bends */
  if (n > 64) n = 64;
  double pla = la1, plo = lo1;
  for (int i = 1; i <= n; i++) {
    double la, lo;
    if (i == n) { la = la2; lo = lo2; }
    else geod_position(&l, l.s13 * i / n, &la, &lo, NULL);
    map_seg(m, pla, plo, la, lo, style);
    pla = la; plo = lo;
  }
}

void map_mark(const map_t *m, double lat, double lon, int kind) {
  float fx, fy;
  map_xy(m, lat, near_lon(lon, m->lon0), &fx, &fy);
  int x = (int)lroundf(fx), y = (int)lroundf(fy);
  if (outcode(m, fx, fy)) return;
  if (kind == 0) gfx_rect(x - 1, y - 1, 3, 3, 1);
  else if (kind == 1) { gfx_rect(x - 3, y - 3, 7, 7, 0); gfx_frame(x - 3, y - 3, 7, 7); gfx_pixel(x, y, 1); }
  else { gfx_circle(x, y, 7, 1); gfx_circle(x, y, 8, 1); }
}

/* ---------------------------------------------------------------- graticule */

static void lon_label(char *s, int lon) {
  lon = (int)near_lon(lon, 0);
  if (lon == 0 || lon == 180 || lon == -180) sprintf(s, "%d", abs(lon));
  else sprintf(s, "%d%c", abs(lon), lon < 0 ? 'W' : 'E');
}

int map_grid(const map_t *m) {
  static const int STEP[] = { 1, 2, 5, 10, 15, 30, 45 };
  float span = map_span_lon(m), lw = m->lon0 - span / 2, le = m->lon0 + span / 2;
  int step = 45;
  for (unsigned i = 0; i < sizeof STEP / sizeof *STEP; i++) if (span / STEP[i] <= 7) { step = STEP[i]; break; }
  char s[8];
  for (int lon = (int)ceilf(lw / step) * step; lon <= le; lon += step) {
    float x, y;
    map_xy(m, 0, lon, &x, &y);
    int ix = (int)lroundf(x);
    for (int yy = m->y0; yy < m->y0 + m->h; yy += 4) gfx_pixel(ix, yy, 1);
    lon_label(s, lon);
    if (ix + 2 + text_width(&TINY, s) < m->x0 + m->w) draw_text(&TINY, ix + 2, m->y0 + 1, s, 0);
  }
  double ls = map_lat(m, m->psi0 - m->h / 2.0f / m->k), ln = map_lat(m, m->psi0 + m->h / 2.0f / m->k);
  for (int lat = (int)ceil(ls / step) * step; lat <= ln; lat += step) {
    if (abs(lat) > LAT_MAX) continue;
    float x, y;
    map_xy(m, lat, m->lon0, &x, &y);
    int iy = (int)lroundf(y);
    if (iy < m->y0 || iy >= m->y0 + m->h) continue;
    gfx_hline_dotted(m->x0, m->x0 + m->w - 1, iy, 4);
    if (lat == 0) strcpy(s, "EQ"); else sprintf(s, "%d%c", abs(lat), lat < 0 ? 'S' : 'N');
    if (iy - 9 > m->y0 + 9) draw_text(&TINY, m->x0 + 2, iy - 9, s, 0);
  }
  return step;
}

/* ---------------------------------------------------------------- the coastline file */

typedef struct {
  FIL f;
  uint32_t base[2], len[2];
  uint8_t buf[2][512];                                        /* 0 the directory, 1 the points */
} coast_io;

/* n bytes at offset off of the file, through window w (n <= 16) */
static const uint8_t *rd(coast_io *c, int w, uint32_t off, uint32_t n) {
  if (off >= c->base[w] && off + n <= c->base[w] + c->len[w]) return c->buf[w] + (off - c->base[w]);
  UINT br = 0;
  if (f_lseek(&c->f, off) != FR_OK || f_read(&c->f, c->buf[w], sizeof c->buf[w], &br) != FR_OK) br = 0;
  c->base[w] = off; c->len[w] = br;
  return br >= n ? c->buf[w] : NULL;
}

static int le16(const uint8_t *p) { return (int16_t)(p[0] | p[1] << 8); }
static unsigned u16(const uint8_t *p) { return (unsigned)(p[0] | p[1] << 8); }
static uint32_t u32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static void coast_poly(const map_t *m, coast_io *c, uint32_t off, unsigned npts, int delta, float scale, float dlon) {
  const uint8_t *p = rd(c, 1, off, 4);
  if (!p) return;
  int la = le16(p), lo = le16(p + 2);
  off += 4;
  float x1, y1;
  map_xy(m, la / scale, lo / scale + dlon, &x1, &y1);
  int lx = (int)lroundf(x1), ly = (int)lroundf(y1);
  for (unsigned j = 1; j < npts; j++) {
    if (!delta) { if (!(p = rd(c, 1, off, 4))) return; la = le16(p); lo = le16(p + 2); off += 4; }
    else {
      if (!(p = rd(c, 1, off, 2))) return;
      if (p[0] == 0x80) { if (!(p = rd(c, 1, off, 6))) return; la = le16(p + 2); lo = le16(p + 4); off += 6; }
      else { la += (int8_t)p[0]; lo += (int8_t)p[1]; off += 2; }
    }
    float x2, y2;
    map_xy(m, la / scale, lo / scale + dlon, &x2, &y2);
    int ix = (int)lroundf(x2), iy = (int)lroundf(y2);
    if (ix == lx && iy == ly && j + 1 < npts) continue;      /* still the same pixel */
    line_f(m, x1, y1, x2, y2, MAP_SOLID);
    x1 = x2; y1 = y2; lx = ix; ly = iy;
  }
}

int map_coast(const map_t *m, const char *path) {
  coast_io *c = (coast_io *)malloc(sizeof(coast_io));
  if (!c) return -1;
  c->len[0] = c->len[1] = 0; c->base[0] = c->base[1] = 0;
  if (f_open(&c->f, path, FA_READ) != FR_OK) { free(c); return -1; }
  const uint8_t *h = rd(c, 0, 0, 16);
  if (!h || memcmp(h, "N47C", 4) || u16(h + 4) != 1) { f_close(&c->f); free(c); return -1; }
  unsigned npoly = u16(h + 6), sc = u16(h + 8);
  int delta = u16(h + 10) & 1;
  float scale = sc ? (float)sc : 100.0f;
  float span = map_span_lon(m);
  /* the view in file units */
  float lw = (m->lon0 - span / 2) * scale, le = (m->lon0 + span / 2) * scale;
  float ls = (float)map_lat(m, m->psi0 - m->h / 2.0f / m->k) * scale;
  float ln = (float)map_lat(m, m->psi0 + m->h / 2.0f / m->k) * scale;
  if (ls <= -LAT_MAX * scale + 1) ls = -90 * scale;
  if (ln >= LAT_MAX * scale - 1) ln = 90 * scale;
  float px = m->k * (float)D2R / scale;                       /* pixels a file unit of longitude */
  int drawn = 0;
  for (unsigned i = 0; i < npoly; i++) {
    const uint8_t *d = rd(c, 0, 16 + 16 * i, 16);
    if (!d) break;
    int a = le16(d), b = le16(d + 2), lo1 = le16(d + 4), lo2 = le16(d + 6);
    uint32_t off = u32(d + 8);
    unsigned npts = u16(d + 12);
    if (b < ls || a > ln) continue;
    if ((lo2 - lo1) * px < 1.5f && (b - a) * px < 1.5f) continue;   /* smaller than a pixel or two */
    for (int k = -1; k <= 1; k++) {
      float o = k * 360 * scale;
      if (lo2 + o < lw || lo1 + o > le) continue;
      coast_poly(m, c, off, npts, delta, scale, k * 360.0f);
      drawn++;
    }
  }
  f_close(&c->f);
  free(c);
  return drawn;
}
