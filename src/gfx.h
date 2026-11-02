/* gfx.h - NAV47 drawing: C47 fonts and simple shapes on the DMCP LCD buffer. GPL-3.0-or-later. */
#ifndef NAV47_GFX_H
#define NAV47_GFX_H

typedef struct { unsigned char before, cols, after, above, rows; unsigned short ofs; unsigned char has; } Glyph;
typedef struct { int box; const Glyph *g; const unsigned short *rows; } Font;

extern const Font STD, TINY;

#define SYM_DEG   "\x01"
#define SYM_MIN   "\x02"
#define SYM_UP    "\x03"
#define SYM_DOWN  "\x04"
#define SYM_SUN   "\x05"
#define SYM_RIGHT "\x06"
#define SYM_LEFT  "\x07"
#define SYM_DOT   "\x0e"
#define SYM_PM    "\x0f"

int  text_width(const Font *f, const char *s);
int  draw_text(const Font *f, int x, int y, const char *s, int inv);   /* y = top of the box; returns end x */
int  draw_text_r(const Font *f, int xr, int y, const char *s, int inv); /* right aligned at xr */
void gfx_rect(int x, int y, int w, int h, int v);
void gfx_frame(int x, int y, int w, int h);
void gfx_pixel(int x, int y, int v);
void gfx_line(int x0, int y0, int x1, int y1, int v);
void gfx_hline_dotted(int x0, int x1, int y, int step);
void gfx_circle(int cx, int cy, int r, int v);
void gfx_disc(int cx, int cy, int r, int v);

#endif
