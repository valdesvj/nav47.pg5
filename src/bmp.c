/* bmp.c - NAV47: a picture from a BMP file (any paint program: 1, 4, 8, 24 or 32 bits, not compressed) as ink and a
 * mask, for the vessel's pictures (/NAV47/BOW.BMP, /NAV47/SHIP.BMP). Dark is ink, light is paper; light pixels joined
 * to the picture's edge (the sky, the sea round the vessel), and pure magenta (255, 0, 255), are see-through.
 * Read a row at a time (the DM42n's RAM): no whole file in memory.
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#include <main.h>
#include <dmcp.h>
#include <stdlib.h>
#include <string.h>
#include "bmp.h"

static unsigned u16(const uint8_t *p) { return p[0] | p[1] << 8; }
static long s32(const uint8_t *p) { return (long)(int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24); }

void pic_free(Pic *p) { free(p->ink); free(p->mask); memset(p, 0, sizeof *p); }

int pic_get(const Pic *p, const uint8_t *a, int x, int y) {
  return x >= 0 && y >= 0 && x < p->w && y < p->h && (a[y * p->bpr + x / 8] & (0x80 >> (x % 8)));
}

/* 0 = see-through, 1 = ink, 2 = paper (state of a pixel while it is read) */
int pic_load(Pic *p, const char *path, int maxw, int maxh) {
  memset(p, 0, sizeof *p);
  if (!sys_disk_ok() || !file_exists(path)) return 0;
  FIL f;
  if (f_open(&f, path, FA_READ) != FR_OK) return 0;
  uint8_t h[54], pal[256 * 4];
  UINT br = 0;
  int rc = -1;
  uint8_t *st = NULL, *row = NULL;
  if (f_read(&f, h, 54, &br) != FR_OK || br < 54 || h[0] != 'B' || h[1] != 'M') goto out;
  long off = s32(h + 10), dib = s32(h + 14), w = s32(h + 18), hh = s32(h + 22);
  unsigned bpp = u16(h + 28);
  long comp = s32(h + 30), ncol = s32(h + 46);
  int topdown = hh < 0;
  if (topdown) hh = -hh;
  if (w <= 0 || hh <= 0 || w > maxw || hh > maxh) { rc = -2; goto out; }       /* too big */
  if (!(comp == 0 || (comp == 3 && (bpp == 32 || bpp == 16)))) { rc = -3; goto out; }   /* compressed */
  if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32) { rc = -3; goto out; }
  if (bpp <= 8) {
    int n = ncol > 0 && ncol <= 256 ? (int)ncol : 1 << bpp;
    if (f_lseek(&f, (FSIZE_t)(14 + dib)) != FR_OK || f_read(&f, pal, (UINT)(n * 4), &br) != FR_OK) goto out;
  }
  long stride = ((w * (long)bpp + 31) / 32) * 4;
  st = (uint8_t *)malloc((size_t)(w * hh));
  row = (uint8_t *)malloc((size_t)stride);
  if (!st || !row) { rc = -4; goto out; }
  for (long y = 0; y < hh; y++) {
    long fy = topdown ? y : hh - 1 - y;                   /* the file's rows are bottom-up */
    if (f_lseek(&f, (FSIZE_t)(off + fy * stride)) != FR_OK || f_read(&f, row, (UINT)stride, &br) != FR_OK || (long)br < stride) goto out;
    for (long x = 0; x < w; x++) {
      int r, g, b;
      if (bpp >= 24) { const uint8_t *q = row + x * (bpp / 8); b = q[0]; g = q[1]; r = q[2]; }
      else {
        int i = bpp == 8 ? row[x] : bpp == 4 ? (row[x / 2] >> (x % 2 ? 0 : 4)) & 15 : (row[x / 8] >> (7 - x % 8)) & 1;
        b = pal[4 * i]; g = pal[4 * i + 1]; r = pal[4 * i + 2];
      }
      uint8_t v;
      if (r == 255 && g == 0 && b == 255) v = 0;          /* magenta: see-through */
      else v = (r * 30 + g * 59 + b * 11) / 100 < 128 ? 1 : 2;
      st[y * w + x] = v;
    }
  }
  /* the paper joined to the edge: see-through (passes both ways until nothing changes) */
  for (long y = 0; y < hh; y++) for (long x = 0; x < w; x++)
    if ((x == 0 || y == 0 || x == w - 1 || y == hh - 1) && st[y * w + x] == 2) st[y * w + x] = 0;
  for (int pass = 0, ch = 1; ch && pass < 60; pass++) {
    ch = 0;
    for (long y = 0; y < hh; y++) for (long x = 0; x < w; x++) {
      uint8_t *c = &st[y * w + x];
      if (*c != 2) continue;
      if ((x && c[-1] == 0) || (y && c[-w] == 0)) { *c = 0; ch = 1; }
    }
    for (long y = hh - 1; y >= 0; y--) for (long x = w - 1; x >= 0; x--) {
      uint8_t *c = &st[y * w + x];
      if (*c != 2) continue;
      if ((x < w - 1 && c[1] == 0) || (y < hh - 1 && c[w] == 0)) { *c = 0; ch = 1; }
    }
  }
  p->w = (int)w; p->h = (int)hh; p->bpr = (int)((w + 7) / 8);
  p->ink = (uint8_t *)calloc((size_t)(p->bpr * hh), 1);
  p->mask = (uint8_t *)calloc((size_t)(p->bpr * hh), 1);
  if (!p->ink || !p->mask) { pic_free(p); rc = -4; goto out; }
  for (long y = 0; y < hh; y++) for (long x = 0; x < w; x++) {
    uint8_t v = st[y * w + x], b = (uint8_t)(0x80 >> (x % 8));
    if (v) p->mask[y * p->bpr + x / 8] |= b;
    if (v == 1) p->ink[y * p->bpr + x / 8] |= b;
  }
  rc = 1;
out:
  free(st); free(row);
  f_close(&f);
  return rc;
}

const char *pic_err(int rc) {
  switch (rc) {
  case 1: return "ok";
  case 0: return "no file";
  case -2: return "too big";
  case -3: return "not a plain BMP";
  case -4: return "no memory";
  }
  return "not a BMP";
}
