/* gfx.c - NAV47 drawing on the DMCP LCD buffer (lcd_fill_rect only: 1 = black). GPL-3.0-or-later. */
#include <stdlib.h>
#include <dmcp.h>
#include "gfx.h"

void gfx_rect(int x, int y, int w, int h, int v) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > LCD_X) w = LCD_X - x;
  if (y + h > LCD_Y) h = LCD_Y - y;
  if (w <= 0 || h <= 0) return;
  lcd_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, v);
}

void gfx_pixel(int x, int y, int v) { gfx_rect(x, y, 1, 1, v); }

void gfx_frame(int x, int y, int w, int h) {
  gfx_rect(x, y, w, 1, 1);
  gfx_rect(x, y + h - 1, w, 1, 1);
  gfx_rect(x, y, 1, h, 1);
  gfx_rect(x + w - 1, y, 1, h, 1);
}

void gfx_line(int x0, int y0, int x1, int y1, int v) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
  for (;;) {
    gfx_pixel(x0, y0, v);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

void gfx_hline_dotted(int x0, int x1, int y, int step) {
  for (int x = x0; x <= x1; x += step) gfx_pixel(x, y, 1);
}

void gfx_circle(int cx, int cy, int r, int v) {
  int x = r, y = 0, err = 1 - r;
  while (x >= y) {
    gfx_pixel(cx + x, cy + y, v); gfx_pixel(cx + y, cy + x, v); gfx_pixel(cx - y, cy + x, v); gfx_pixel(cx - x, cy + y, v);
    gfx_pixel(cx - x, cy - y, v); gfx_pixel(cx - y, cy - x, v); gfx_pixel(cx + y, cy - x, v); gfx_pixel(cx + x, cy - y, v);
    y++;
    if (err < 0) err += 2 * y + 1;
    else { x--; err += 2 * (y - x) + 1; }
  }
}

void gfx_disc(int cx, int cy, int r, int v) {
  for (int dy = -r; dy <= r; dy++) {
    int w = 0;
    while ((w + 1) * (w + 1) + dy * dy <= r * r + r) w++;
    gfx_rect(cx - w, cy + dy, 2 * w + 1, 1, v);
  }
}

static int adv(const Font *f, unsigned char c) {
  const Glyph *g = &f->g[c & 0x7f];
  return g->has ? g->before + g->cols + g->after : (f->box == 20 ? 8 : 6);
}

int text_width(const Font *f, const char *s) {
  int w = 0;
  for (; *s; s++) w += adv(f, (unsigned char)*s);
  return w;
}

int draw_text(const Font *f, int x, int y, const char *s, int inv) {
  int fg = inv ? 0 : 1;
  if (inv) gfx_rect(x - 1, y, text_width(f, s) + 1, f->box, 1);
  for (; *s; s++) {
    const Glyph *g = &f->g[(unsigned char)*s & 0x7f];
    if (g->has) {
      int gx = x + g->before;
      for (int r = 0; r < g->rows; r++) {
        unsigned bits = f->rows[g->ofs + r];
        int yy = y + g->above + r;
        int c = 0;
        while (c < g->cols) {
          if (bits & (1u << (g->cols - 1 - c))) {
            int c0 = c;
            while (c < g->cols && (bits & (1u << (g->cols - 1 - c)))) c++;
            gfx_rect(gx + c0, yy, c - c0, 1, fg);
          } else c++;
        }
      }
    }
    x += adv(f, (unsigned char)*s);
  }
  return x;
}

int draw_text_r(const Font *f, int xr, int y, const char *s, int inv) {
  return draw_text(f, xr - text_width(f, s), y, s, inv);
}
