/* ntext.c - NAV47: text in the DMCP5 system font. A private copy of t20's display state (the system's own is not
 * touched): no background or line fill, so the text sits on the charts; the rows are placed by the baseline.
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#include <main.h>
#include <dmcp.h>
#include <string.h>
#include "ntext.h"

static disp_stat_t D;
static int CW, BASE;           /* the cell width; the baseline from the top of a row */

void nt_init(void) {
  D = *t20;
  D.lnfill = 0; D.bgfill = 0; D.newln = 0; D.inv = 0; D.y_top_grd = 0; D.xoffs = 0;
  CW = lcd_charWidth(&D, '0');
  BASE = 13;
}
int nt_cw(void) { return CW; }

static int narrow(int c) { return c == '.' || c == ':' || c == 1; }

static void ring(int x, int y, int v) {          /* the degree sign: a 4 x 4 ring at the cap height */
  y += 2;
  lcd_fill_rect((uint32_t)x + 1, (uint32_t)y, 2, 1, v);
  lcd_fill_rect((uint32_t)x + 1, (uint32_t)y + 3, 2, 1, v);
  lcd_fill_rect((uint32_t)x, (uint32_t)y + 1, 1, 2, v);
  lcd_fill_rect((uint32_t)x + 3, (uint32_t)y + 1, 1, 2, v);
}

int nt_w(const char *s) {
  int w = 0;
  for (; *s; s++) w += narrow((unsigned char)*s) ? 5 : CW;
  return w;
}

static int draw(int x, int y, const char *s, int inv) {
  int top = y + BASE - (lcd_lineHeight(&D) - lcd_baseHeight(&D));     /* the line box, by the baseline */
  D.inv = (uint8_t)inv;
  char run[64];
  while (*s) {
    int n = 0;
    while (s[n] && !narrow((unsigned char)s[n]) && n < 63) { run[n] = s[n]; n++; }
    if (n) {
      run[n] = 0;
      lcd_setXY(&D, x, top);
      lcd_writeText(&D, run);
      x += n * CW;
      s += n;
      continue;
    }
    int c = (unsigned char)*s++;
    if (c == 1) ring(x, y, !inv);
    else {
      char t[2] = { (char)c, 0 };
      lcd_setXY(&D, x + 2 - CW / 2, top);          /* the dot of a cell is in its middle */
      lcd_writeText(&D, t);
    }
    x += 5;
  }
  D.inv = 0;
  return x;
}

int nt(int x, int y, const char *s) { return draw(x, y, s, 0); }
int nt_inv(int x, int y, const char *s) {
  lcd_fill_rect((uint32_t)(x - 1), (uint32_t)y, (uint32_t)(nt_w(s) + 2), NT_ROW, 1);
  return draw(x, y, s, 1);
}
int nt_r(int xr, int y, const char *s) { return draw(xr - nt_w(s), y, s, 0); }
int nt_c(int xc, int y, const char *s) { return draw(xc - nt_w(s) / 2, y, s, 0); }
