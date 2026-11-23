/* ports.c - NAV47: the world's ports on the CHART. See ports.h. Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#include <main.h>
#include <dmcp.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "gfx.h"
#include "ports.h"

#define PMAX 150
typedef struct { float lat, lon; char name[14]; } Port;
static Port *PT;
static int NP = -1, many;
static float key[5] = { -999 };

static int visible(const map_t *m, double lat, double lon, float *x, float *y) {
  if (m->globe) {                                         /* the near side only */
    double c = m->sl0 * sin(lat * M_PI / 180) + m->cl0 * cos(lat * M_PI / 180) * cos((lon - m->lon0) * M_PI / 180);
    if (c <= 0) return 0;
  }
  map_xy(m, lat, lon, x, y);
  return *x >= m->x0 && *x < m->x0 + m->w && *y >= m->y0 && *y < m->y0 + m->h;
}

/* one line "name,lat,lon,..." into the list when it shows */
static void take(const map_t *m, char *l) {
  char *c1 = strchr(l, ','); if (!c1) return;
  char *c2 = strchr(c1 + 1, ','); if (!c2) return;
  *c1 = 0;
  double lat = atof(c1 + 1), lon = atof(c2 + 1);
  if (fabs(lat) > 90 || fabs(lon) > 180 || (lat == 0 && lon == 0)) return;
  float x, y;
  if (!visible(m, lat, lon, &x, &y)) return;
  if (NP >= PMAX) { many = 1; return; }
  Port *p = &PT[NP++];
  p->lat = (float)lat; p->lon = (float)lon;
  strncpy(p->name, l, sizeof p->name - 1); p->name[sizeof p->name - 1] = 0;
}

static void scan(const map_t *m) {
  NP = -1; many = 0;
  if (!sys_disk_ok() || !file_exists(PORTS_FILE)) return;
  if (!PT && !(PT = (Port *)malloc(PMAX * sizeof *PT))) return;
  FIL f;
  if (f_open(&f, PORTS_FILE, FA_READ) != FR_OK) return;
  NP = 0;
  char buf[256], line[96];
  int n = 0, skip = 1;                                    /* skip: the header line */
  UINT br;
  while (f_read(&f, buf, sizeof buf, &br) == FR_OK && br > 0) {
    for (UINT i = 0; i < br; i++) {
      char c = buf[i];
      if (c == '\n' || c == '\r') {
        if (n) { line[n] = 0; if (!skip) take(m, line); skip = 0; n = 0; }
      } else if (n < (int)sizeof line - 1) line[n++] = c;
    }
  }
  if (n && !skip) { line[n] = 0; take(m, line); }
  f_close(&f);
}

void ports_chart(const map_t *m) {
  float k[5] = { m->lon0, m->psi0, m->k, (float)m->globe, m->lat0 };
  if (memcmp(k, key, sizeof k)) { memcpy(key, k, sizeof k); scan(m); }   /* the view changed: read them again */
  if (NP <= 0) return;
  int names = NP <= 40 && !many, nr = 0;
  short R[40][4];                                         /* the names drawn: a name over another one is left out */
  for (int i = 0; i < NP; i++) {
    float x, y;
    if (!visible(m, PT[i].lat, PT[i].lon, &x, &y)) continue;
    int xi = (int)lroundf(x), yi = (int)lroundf(y);
    gfx_rect(xi - 2, yi - 2, 5, 5, 1); gfx_rect(xi - 1, yi - 1, 3, 3, 0);   /* a small hollow square */
    if (!names) continue;
    int w = text_width(&TINY, PT[i].name), lx = xi + 5, ly = yi - 9;
    if (lx + w >= m->x0 + m->w) lx = xi - 4 - w;                         /* at the right edge: on the left */
    if (lx < m->x0 || ly < m->y0) continue;
    int free = 1;
    for (int r = 0; r < nr && free; r++) if (lx < R[r][0] + R[r][2] && lx + w > R[r][0] && ly < R[r][1] + 9 && ly + 9 > R[r][1]) free = 0;
    if (!free || nr >= 40) continue;
    R[nr][0] = (short)lx; R[nr][1] = (short)ly; R[nr][2] = (short)w; nr++;
    draw_text(&TINY, lx, ly, PT[i].name, 0);
  }
}

int ports_shown(void) { return NP; }
