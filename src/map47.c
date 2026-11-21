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
  m->globe = 0; m->lat0 = 0; m->sl0 = 0; m->cl0 = 1;
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

/* the point on the LCD; 0 when it is on the far side of the globe */
static int proj(const map_t *m, double lat, double lon, float *x, float *y) {
  float cx = m->x0 + m->w * 0.5f, cy = m->y0 + m->h * 0.5f;
  if (!m->globe) {
    *x = cx + m->k * (float)((lon - m->lon0) * D2R);
    *y = cy - m->k * (map_psi(m, lat) - m->psi0);
    return 1;
  }
  float p = (float)(lat * D2R), d = (float)((lon - m->lon0) * D2R);
  float sp = sinf(p), cp = cosf(p), sd = sinf(d), cd = cosf(d);
  *x = cx + m->k * cp * sd;
  *y = cy - m->k * (m->cl0 * sp - m->sl0 * cp * cd);
  return m->sl0 * sp + m->cl0 * cp * cd >= 0;
}

void map_xy(const map_t *m, double lat, double lon, float *x, float *y) { proj(m, lat, lon, x, y); }

void map_center(map_t *m, double lat, double lon) {
  if (lat > 90) lat = 90;
  if (lat < -90) lat = -90;
  m->lat0 = (float)lat; m->lon0 = (float)lon;
  m->sl0 = sinf((float)(lat * D2R)); m->cl0 = cosf((float)(lat * D2R));
  m->psi0 = map_psi(m, lat);
}

float map_kmin(const map_t *m) {
  return m->globe ? (m->h < m->w ? m->h : m->w) * 0.5f - 4 : (float)(m->w / (2 * M_PI));
}
float map_kmax(const map_t *m) { return (float)(m->w / (0.2 * D2R)); }   /* 0.2 deg (12 NM) across */

void map_set_globe(map_t *m, int on) {
  if (on == m->globe) return;
  if (on) {                                                   /* the same scale at the centre */
    double lat = map_lat(m, m->psi0);
    float c = cosf((float)(lat * D2R));
    m->globe = 1;
    map_center(m, lat, m->lon0);
    m->k /= c > 0.05f ? c : 0.05f;
  } else {
    float lat = m->lat0 > 84 ? 84 : m->lat0 < -84 ? -84 : m->lat0;
    m->globe = 0;
    m->k *= cosf(lat * (float)D2R);
    map_center(m, lat, m->lon0);
  }
  map_zoom(m, 1);
}

float map_globe_fitpt(const map_t *m, double lat, double lon) {
  float p = (float)(lat * D2R), d = (float)((lon - m->lon0) * D2R);
  float sp = sinf(p), cp = cosf(p), sd = sinf(d), cd = cosf(d);
  if (m->sl0 * sp + m->cl0 * cp * cd < 0.17f) return 0;       /* behind or at the rim (over 80 deg away) */
  float px = fabsf(cp * sd), py = fabsf(m->cl0 * sp - m->sl0 * cp * cd), k = 1e9f;
  if (px > 1e-6f) k = (m->w * 0.5f - 8) / px;
  if (py > 1e-6f && (m->h * 0.5f - 8) / py < k) k = (m->h * 0.5f - 8) / py;
  return k;
}

static void clamp_view(map_t *m) {
  float kmin = map_kmin(m), kmax = map_kmax(m);
  float pmax = map_psi(m, LAT_MAX);
  if (m->k < kmin) m->k = kmin;
  if (m->k > kmax) m->k = kmax;
  if (m->psi0 > pmax) m->psi0 = pmax;
  if (m->psi0 < -pmax) m->psi0 = -pmax;
  m->lon0 = (float)near_lon(m->lon0, 0);
  if (m->globe) map_center(m, m->lat0, m->lon0);
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
  if (m->globe) {                                             /* turn the globe, tilt it: as an airliner's map */
    float a = map_span_lon(m);
    if (a > 60) a = 60;                                       /* the whole Earth: 15 deg a key */
    float lat = m->lat0 + fy * a;
    if (lat > 90) lat = 90;
    if (lat < -90) lat = -90;
    float c = cosf(m->lat0 * (float)D2R);
    map_center(m, lat, m->lon0 + fx * a / (c > 0.25f ? c : 0.25f));
    clamp_view(m);
    return;
  }
  m->lon0 += (float)(fx * m->w / m->k * R2D);
  m->psi0 += fy * m->h / m->k;
  clamp_view(m);
}

float map_span_lon(const map_t *m) {
  if (!m->globe) return (float)(m->w / m->k * R2D);
  float r = m->w * 0.5f / m->k;
  return r >= 1 ? 180.0f : 2 * asinf(r) * (float)R2D;
}

/* ---------------------------------------------------------------- lines, clipped to the box */

static unsigned pat;                                          /* the dash pattern runs on from line to line */

static void plot(const map_t *m, int x, int y, int style) {
  pat++;
  if (style == MAP_DASH && pat % 9 >= 5) return;
  if (style == MAP_DOT && pat % 3) return;
  if (style == MAP_FAINT && pat % 6) return;
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
  if (!isfinite(x1) || !isfinite(y1) || !isfinite(x2) || !isfinite(y2)) return;   /* a bad point: never a runaway line */
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

/* straight on the LCD between two points (both on the near side of the globe) */
static void seg_ll(const map_t *m, double la1, double lo1, double la2, double lo2, int style) {
  float x1, y1, x2, y2;
  lo1 = near_lon(lo1, m->lon0);
  lo2 = near_lon(lo2, lo1);
  if (proj(m, la1, lo1, &x1, &y1) & proj(m, la2, lo2, &x2, &y2)) line_f(m, x1, y1, x2, y2, style);
}

void map_seg(const map_t *m, double la1, double lo1, double la2, double lo2, int style) {
  if (!m->globe) { seg_ll(m, la1, lo1, la2, lo2, style); return; }
  /* on the globe a rhumb line bends: pieces, linear in longitude and isometric latitude (Mercator's straight line) */
  lo2 = near_lon(lo2, lo1);
  float p1 = map_psi(m, la1), p2 = map_psi(m, la2);
  double dl = fabs(lo2 - lo1), dp = fabs(la2 - la1);
  int n = (int)((dl > dp ? dl : dp) / 2) + 1;
  if (n > 64) n = 64;
  double pla = la1, plo = lo1;
  for (int i = 1; i <= n; i++) {
    double t = (double)i / n, lo = lo1 + (lo2 - lo1) * t;
    double la = i == n ? la2 : (la1 == la2 ? la1 : map_lat(m, p1 + (p2 - p1) * (float)t));
    seg_ll(m, pla, plo, la, lo, style);
    pla = la; plo = lo;
  }
}

void map_geod(const map_t *m, const struct geod_geodesic *g, double la1, double lo1, double la2, double lo2, int style) {
  struct geod_geodesicline l;
  geod_inverseline(&l, g, la1, lo1, la2, lo2, 0);
  float x1, y1, x2, y2;
  int n;
  if (m->globe) n = (int)(l.a13 / 2) + 4;                     /* a piece every 2 deg of arc */
  else {
    map_xy(m, la1, near_lon(lo1, m->lon0), &x1, &y1);
    map_xy(m, la2, near_lon(lo2, m->lon0), &x2, &y2);
    n = (int)(hypotf(x2 - x1, y2 - y1) / 6) + 4;              /* a piece every ~6 px, more where it bends */
  }
  if (n > 64) n = 64;
  double pla = la1, plo = lo1;
  for (int i = 1; i <= n; i++) {
    double la, lo;
    if (i == n) { la = la2; lo = lo2; }
    else geod_position(&l, l.s13 * i / n, &la, &lo, NULL);
    seg_ll(m, pla, plo, la, lo, style);
    pla = la; plo = lo;
  }
}

void map_mark(const map_t *m, double lat, double lon, int kind) {
  float fx, fy;
  if (!proj(m, lat, near_lon(lon, m->lon0), &fx, &fy)) return;
  int x = (int)lroundf(fx), y = (int)lroundf(fy);
  if (outcode(m, fx, fy)) return;
  if (kind == 3) {                                            /* you: a cross with a gap, clear around */
    gfx_rect(x - 6, y - 1, 13, 3, 0); gfx_rect(x - 1, y - 6, 3, 13, 0);
    gfx_rect(x - 6, y, 5, 1, 1); gfx_rect(x + 2, y, 5, 1, 1);
    gfx_rect(x, y - 6, 1, 5, 1); gfx_rect(x, y + 2, 1, 5, 1);
    gfx_pixel(x, y, 1);
    return;
  }
  if (kind == 0) gfx_rect(x - 1, y - 1, 3, 3, 1);
  else if (kind == 1) { gfx_rect(x - 3, y - 3, 7, 7, 0); gfx_frame(x - 3, y - 3, 7, 7); gfx_pixel(x, y, 1); }
  else { gfx_circle(x, y, 7, 1); gfx_circle(x, y, 8, 1); }
}

/* 13 x 13, the top row first, bit 12 the left */
/* the ship: the Mermaid Asiana from the starboard side, the bow to the right, after a photograph: the square stern
   and the low working deck aft, the main crane's pedestal aft with its boom parked forward, the rescue boat, the
   accommodation and the bridge, the lattice mast, the helideck over the bow on its struts, the high raked bow;
   37 x 16, bit 36 the left */
#define SHIP_W 37
#define SHIP_H 16
static const uint64_t SHIP[SHIP_H] = { 0x0000004000ULL, 0x000000E000ULL, 0x0000004000ULL, 0x000000E000ULL, 0x0000004000ULL, 0x000001F000ULL, 0x000003FFFFULL, 0x000002AB02ULL, 0x00FFE3FE84ULL, 0x007FDFFF48ULL, 0x0060BFFFB0ULL, 0x00607FFFFCULL, 0x00F1FFFFFCULL, 0x1FFFFFFFFCULL, 0x1FFFFFFFF8ULL, 0x1FFFFFFFF0ULL };
static const uint16_t PLANE[13] = { 0x0040, 0x00E0, 0x00E0, 0x00E0, 0x01F0, 0x0FFE, 0x1FFF, 0x00E0, 0x00E0, 0x00E0,
                                    0x01F0, 0x03F8, 0x0000 };

void map_icon(const map_t *m, double lat, double lon, double crs, int plane) {
  float fx, fy, ax, ay;
  lon = near_lon(lon, m->lon0);
  if (!proj(m, lat, lon, &fx, &fy) || outcode(m, fx, fy)) return;
  double c = cos(lat * D2R);                                  /* the heading: a point a little ahead, on the LCD */
  proj(m, lat + 0.05 * cos(crs * D2R), lon + 0.05 * sin(crs * D2R) / (c > 0.01 ? c : 0.01), &ax, &ay);
  float dx = ax - fx, dy = ay - fy, l = hypotf(dx, dy);
  int x = (int)lroundf(fx), y = (int)lroundf(fy);
  if (l > 1e-4f) {
    dx /= l; dy /= l;
    int i0 = plane ? 8 : 16, i1 = plane ? 20 : 28;
    for (int i = i0; i < i1; i++) {                           /* the heading line, white edged */
      int px = x + (int)lroundf(dx * i), py = y + (int)lroundf(dy * i);
      if (outcode(m, (float)px, (float)py)) break;
      gfx_rect(px - 1, py - 1, 3, 3, 0);
    }
    for (int i = i0; i < i1; i++) {
      int px = x + (int)lroundf(dx * i), py = y + (int)lroundf(dy * i);
      if (outcode(m, (float)px, (float)py)) break;
      gfx_pixel(px, py, 1);
    }
  }
  if (plane && l > 1e-4f) {                                   /* the plane: along its course on the LCD */
    gfx_disc(x, y, 9, 0);
    for (int v = -9; v <= 9; v++)
      for (int u = -9; u <= 9; u++) {
        /* the pixel in the plane's frame: f forward (dx, dy), r to the right (-dy, dx) */
        float f = u * dx + v * dy, r = fabsf(-u * dy + v * dx);
        int in = (r <= 1.1f && f >= -6.5f && f <= 7.5f - r)                      /* the fuselage, a pointed nose */
              || (r <= 7.5f && f <= 1.8f - 0.5f * r && f >= -0.8f - 0.5f * r)     /* the wings, swept */
              || (r <= 3.6f && f <= -4.2f - 0.3f * r && f >= -6.5f);              /* the tailplane */
        if (in) gfx_pixel(x + u, y + v, 1);
      }
    return;
  }
  if (plane) {                                                /* (no course yet: upright) */
    gfx_rect(x - 7, y - 7, 15, 15, 0);
    for (int r = 0; r < 13; r++)
      for (int k = 0; k < 13; k++)
        if (PLANE[r] & (1u << (12 - k))) gfx_pixel(x - 6 + k, y - 6 + r, 1);
    return;
  }
  if (l > 1e-4f) {                                            /* the vessel from above, along its course */
    /* in its own frame: f forward (dx, dy), r to starboard (-dy, dx); 28 px long, 9 wide: the hull (a square
       stern, a pointed bow), the helideck forward (white, a ring, an H along the vessel) */
    gfx_disc(x, y, 16, 0);
    for (int v = -16; v <= 16; v++)
      for (int u = -16; u <= 16; u++) {
        float f = u * dx + v * dy, rr = -u * dy + v * dx, r = fabsf(rr);
        float half = f <= 6 ? 4.6f : 4.6f * (14 - f) / 8;              /* the bow's taper from f = 6 to the stem at 14 */
        if (f < -13.5f || f > 14 || r > half + 0.4f) continue;
        float hf = f - 6.5f, d = sqrtf(hf * hf + rr * rr);                /* the helideck: centre f = 6.5, radius 4.4 */
        int on = 1;
        if (d <= 4.4f) {
          on = d >= 3.5f;                                                  /* its ring */
          if (fabsf(hf) <= 2.2f && fabsf(r - 1.6f) <= 0.55f) on = 1;       /* the H: two legs along the vessel ... */
          if (fabsf(hf) <= 0.5f && r <= 1.6f) on = 1;                      /* ... and the bar across */
        }
        gfx_pixel(x + u, y + v, on);
      }
    return;
  }
  int flip = 0;                                               /* no course: the side view */
  gfx_rect(x - SHIP_W / 2 - 1, y - SHIP_H / 2 - 1, SHIP_W + 2, SHIP_H + 2, 0);
  for (int r = 0; r < SHIP_H; r++)
    for (int k = 0; k < SHIP_W; k++)
      if (SHIP[r] & (1ULL << (SHIP_W - 1 - k))) gfx_pixel(x - SHIP_W / 2 + (flip ? SHIP_W - 1 - k : k), y - SHIP_H / 2 + r, 1);
}

/* ---------------------------------------------------------------- graticule */

static void lon_label(char *s, int lon) {
  lon = (int)near_lon(lon, 0);
  if (lon == 0 || lon == 180 || lon == -180) sprintf(s, "%d", abs(lon));
  else sprintf(s, "%d%c", abs(lon), lon < 0 ? 'W' : 'E');
}

static void grid_globe(const map_t *m, int step) {
  float rho = map_span_lon(m) / 2 * (float)M_SQRT2;           /* the corners' distance from the centre, deg */
  if (rho > 90) rho = 90;
  float la_s = m->lat0 - rho - step, la_n = m->lat0 + rho + step;
  if (la_s < -90) la_s = -90;
  if (la_n > 90) la_n = 90;
  float cmin = cosf((fabsf(m->lat0) + rho) * (float)D2R);
  float wl = rho >= 89 || fabsf(m->lat0) + rho >= 85 ? 180 : rho / (cmin > 0.05f ? cmin : 0.05f) + step;
  if (wl > 180) wl = 180;
  float lo_w = m->lon0 - wl, lo_e = m->lon0 + wl;
  for (int lon = (int)ceilf(lo_w / step) * step; lon <= lo_e && lon < lo_w + 360; lon += step)
    for (int i = 0; i < 48; i++) {
      double a = la_s + (la_n - la_s) * i / 48.0, b = la_s + (la_n - la_s) * (i + 1) / 48.0;
      seg_ll(m, a, lon, b, lon, MAP_FAINT);
    }
  for (int lat = (int)ceilf(la_s / step) * step; lat <= la_n; lat += step) {
    if (abs(lat) >= 90) continue;
    for (int i = 0; i < 48; i++) {
      double a = lo_w + (lo_e - lo_w) * i / 48.0, b = lo_w + (lo_e - lo_w) * (i + 1) / 48.0;
      seg_ll(m, lat, a, lat, b, MAP_FAINT);
    }
  }
}

/* globe: the rim, black space outside it, a few stars */
void map_space(const map_t *m) {
  if (!m->globe) return;
  float cx = m->x0 + m->w * 0.5f, cy = m->y0 + m->h * 0.5f, r = m->k;
  if (r > 2 * (m->w + m->h)) return;                          /* the rim is far off the LCD */
  for (int y = m->y0; y < m->y0 + m->h; y++) {
    float dy = y + 0.5f - cy, q = r * r - dy * dy;
    if (q <= 0) { gfx_rect(m->x0, y, m->w, 1, 1); continue; }
    float hw = sqrtf(q);
    int xa = (int)lroundf(cx - hw), xb = (int)lroundf(cx + hw);
    if (xa > m->x0) gfx_rect(m->x0, y, xa - m->x0, 1, 1);
    if (xb < m->x0 + m->w) gfx_rect(xb, y, m->x0 + m->w - xb, 1, 1);
    if (xa >= m->x0) gfx_pixel(xa, y, 1);                     /* the rim, twice as thick */
    if (xb - 1 < m->x0 + m->w) gfx_pixel(xb - 1, y, 1);
  }
  uint32_t z = 47;                                            /* the same stars every time */
  for (int i = 0; i < 60; i++) {
    z = z * 1103515245u + 12345u; int x = m->x0 + (int)((z >> 8) % (uint32_t)m->w);
    z = z * 1103515245u + 12345u; int y = m->y0 + (int)((z >> 8) % (uint32_t)m->h);
    float dx = x - cx, dy = y - cy;
    if (dx * dx + dy * dy > (r + 3) * (r + 3)) gfx_pixel(x, y, 0);
  }
}

int map_grid(const map_t *m) {
  static const int STEP[] = { 1, 2, 5, 10, 15, 30, 45 };
  float span = map_span_lon(m), lw = m->lon0 - span / 2, le = m->lon0 + span / 2;
  int step = 45;
  for (unsigned i = 0; i < sizeof STEP / sizeof *STEP; i++) if (span / STEP[i] <= 7) { step = STEP[i]; break; }
  if (m->globe) { if (step > 30) step = 30; grid_globe(m, step); return step; }
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
  int v1 = proj(m, la / scale, lo / scale + dlon, &x1, &y1);
  int lx = (int)lroundf(x1), ly = (int)lroundf(y1);
  for (unsigned j = 1; j < npts; j++) {
    if (!delta) { if (!(p = rd(c, 1, off, 4))) return; la = le16(p); lo = le16(p + 2); off += 4; }
    else {
      if (!(p = rd(c, 1, off, 2))) return;
      if (p[0] == 0x80) { if (!(p = rd(c, 1, off, 6))) return; la = le16(p + 2); lo = le16(p + 4); off += 6; }
      else { la += (int8_t)p[0]; lo += (int8_t)p[1]; off += 2; }
    }
    float x2, y2;
    int v2 = proj(m, la / scale, lo / scale + dlon, &x2, &y2);
    int ix = (int)lroundf(x2), iy = (int)lroundf(y2);
    if (ix == lx && iy == ly && v2 == v1 && j + 1 < npts) continue;   /* still the same pixel */
    if (v1 && v2) line_f(m, x1, y1, x2, y2, MAP_SOLID);
    x1 = x2; y1 = y2; lx = ix; ly = iy; v1 = v2;
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
  float rho = map_span_lon(m) / 2 * (float)M_SQRT2 + 2;       /* globe: the view's radius, deg */
  int drawn = 0;
  for (unsigned i = 0; i < npoly; i++) {
    const uint8_t *d = rd(c, 0, 16 + 16 * i, 16);
    if (!d) break;
    int a = le16(d), b = le16(d + 2), lo1 = le16(d + 4), lo2 = le16(d + 6);
    uint32_t off = u32(d + 8);
    unsigned npts = u16(d + 12);
    if (m->globe) {                                           /* every polygon once: the sphere wraps by itself */
      float la0 = m->lat0 * scale, near = la0 < a ? a : la0 > b ? b : la0;
      if (fabsf(near - la0) > rho * scale) continue;          /* too far north or south of the view */
      if ((lo2 - lo1) * px < 1.0f && (b - a) * px < 1.0f) continue;
      coast_poly(m, c, off, npts, delta, scale, 0);
      drawn++;
      continue;
    }
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

int map_coast_nearest(const char *path, double lat, double lon, double *deg) {
  coast_io *c = (coast_io *)malloc(sizeof(coast_io));
  if (!c) return -1;
  c->len[0] = c->len[1] = 0; c->base[0] = c->base[1] = 0;
  if (f_open(&c->f, path, FA_READ) != FR_OK) { free(c); return -1; }
  const uint8_t *h = rd(c, 0, 0, 16);
  if (!h || memcmp(h, "N47C", 4) || u16(h + 4) != 1) { f_close(&c->f); free(c); return -1; }
  unsigned npoly = u16(h + 6), sc = u16(h + 8);
  int delta = u16(h + 10) & 1;
  float scale = sc ? (float)sc : 100.0f;
  float p0 = (float)(lat * D2R), s0 = sinf(p0), c0 = cosf(p0);
  float best = 4.0f;                                          /* 1 - cos(distance): 4 is more than any */
  float bdeg = 180;
  for (unsigned i = 0; i < npoly; i++) {
    const uint8_t *d = rd(c, 0, 16 + 16 * i, 16);
    if (!d) break;
    float a = le16(d) / scale, b = le16(d + 2) / scale;
    float nl = (float)lat < a ? a : (float)lat > b ? b : (float)lat;
    if (fabsf(nl - (float)lat) > bdeg) continue;              /* farther in latitude alone than the best */
    uint32_t off = u32(d + 8);
    unsigned npts = u16(d + 12);
    const uint8_t *p = rd(c, 1, off, 4);
    if (!p) continue;
    int la = le16(p), lo = le16(p + 2);
    off += 4;
    for (unsigned j = 0; j < npts; j++) {
      if (j) {
        if (!delta) { if (!(p = rd(c, 1, off, 4))) break; la = le16(p); lo = le16(p + 2); off += 4; }
        else {
          if (!(p = rd(c, 1, off, 2))) break;
          if (p[0] == 0x80) { if (!(p = rd(c, 1, off, 6))) break; la = le16(p + 2); lo = le16(p + 4); off += 6; }
          else { la += (int8_t)p[0]; lo += (int8_t)p[1]; off += 2; }
        }
      }
      float p1 = la / scale * (float)D2R, dl = (float)((lo / scale - lon) * D2R);
      float v = 1 - (s0 * sinf(p1) + c0 * cosf(p1) * cosf(dl));
      if (v < best) { best = v; bdeg = acosf(1 - v) * (float)R2D; }
    }
  }
  f_close(&c->f);
  free(c);
  if (best > 3.9f) return -1;
  *deg = bdeg;
  return 0;
}
