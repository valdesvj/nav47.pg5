/*
 * NAV47 PC simulator: links the real src/main.c and the engines against a fake DMCP5 layer and writes the
 * 400x240 LCD as PBM snapshots (test/sim/render.py turns them into PNG).
 *
 *   build/sim KEY KEY ... SNAP:name ...
 *   keys: F1..F6 0..9 ENTER EXIT BSP UP DOWN SHIFT CHS DOT, WAIT:ms; HOSTL: the PC sends L on the serial console
 *         (DMCP marks every row unsent and gives the program key 77)
 *   env:  NAV47_CLOCK="2026-11-02 12:00"  (the calculator clock), NAV47_FILE=path (the file chosen in FILES)
 *   files: ./simdisk/ is the calculator disk
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "dmcp.h"
#include "gfx.h"
#include "simfont.h"

sys_sdb_t sim_sdb;
static disp_stat_t ds20, ds24, dsReg;
static uint8_t fb[LCD_Y][LCD_X];
static char **script;
static int nscript, spos, pending = -1, idle;
static uint32_t fake_ms;
static int clk_y = 2026, clk_m = 11, clk_d = 2, clk_h = 12, clk_mi = 0;

void lcd_clear_buf(void) { memset(fb, 0, sizeof fb); }
/* a refresh: DMCP's serial console sends the changed rows (test/sim/sercon_sim.c; nothing in the TinyUSB sim) */
__attribute__((weak)) void sim_lcd_out(int forced) { (void)forced; }
__attribute__((weak)) void sim_host_L(void) {}
void lcd_refresh(void) { sim_lcd_out(0); }
void lcd_refresh_wait(void) {}
void lcd_refresh_lines(int ln, int cnt) { (void)ln; (void)cnt; }
void lcd_forced_refresh(void) { sim_lcd_out(1); }
void lcd_set_buf_cleared(int v) { (void)v; }
int  lcd_get_buf_cleared(void) { return 0; }
void LCD_power_on(void) {}
void LCD_power_off(int c) { (void)c; }
void draw_power_off_image(int a) { (void)a; }
void lcd_fill_rect(uint32_t x, uint32_t y, uint32_t dx, uint32_t dy, int val) {
  for (uint32_t j = y; j < y + dy && j < LCD_Y; j++)
    for (uint32_t i = x; i < x + dx && i < LCD_X; i++) fb[j][i] = (uint8_t)(val ? 1 : 0);
}
/* the DMCP font layer: t20 / t24 stand-in (test/sim/gen_simfont.py), cell SIMF_W x SIMF_H, baseline SIMF_B */
static line_font_t simf = { "sim t20", SIMF_W, SIMF_H, SIMF_B, 32, 96, 1, 1, 0, 0 };
int lcd_lineHeight(disp_stat_t *ds) { return ds->f->height + ds->ya + ds->yb; }
int lcd_baseHeight(disp_stat_t *ds) { return ds->f->baseline + ds->yb; }
int lcd_fontWidth(disp_stat_t *ds) { return ds->f->width; }
int lcd_charWidth(disp_stat_t *ds, int c) { (void)c; return ds->f->width + ds->xspc; }
int lcd_textWidth(disp_stat_t *ds, const char *t) { return (int)strlen(t) * lcd_charWidth(ds, 0); }
void lcd_setXY(disp_stat_t *ds, int x, int y) { ds->x = (int16_t)x; ds->y = (int16_t)y; }
void lcd_switchFont(disp_stat_t *ds, int nr) { (void)nr; ds->f = &simf; }
void lcd_writeText(disp_stat_t *ds, const char *s) {
  int y0 = ds->y + ds->ya;
  for (; *s; s++) {
    int c = (unsigned char)*s;
    if (ds->bgfill) lcd_fill_rect((uint32_t)ds->x, (uint32_t)ds->y, (uint32_t)lcd_charWidth(ds, c), (uint32_t)lcd_lineHeight(ds), ds->inv);
    if (c >= 32 && c < 128)
      for (int r = 0; r < SIMF_H; r++) {
        unsigned m = SIMF[(c - 32) * SIMF_H + r];
        for (int i = 0; i < SIMF_W; i++)
          if (m & (1u << (SIMF_W - 1 - i))) {
            int X = ds->x + i, Y = y0 + r;
            if (X >= 0 && Y >= 0 && X < LCD_X && Y < LCD_Y) fb[Y][X] = (uint8_t)(ds->inv ? 0 : 1);
          }
      }
    ds->x = (int16_t)(ds->x + lcd_charWidth(ds, c));
  }
}
void lcd_draw_menu_keys(const char *keys[]) {
  int y0 = LCD_Y - LCD_MENU_LINES + 2, w = LCD_X / 6;
  lcd_fill_rect(0, LCD_Y - LCD_MENU_LINES, LCD_X, LCD_MENU_LINES, 0);
  disp_stat_t d = ds24;
  d.inv = 1; d.bgfill = 0;
  for (int i = 0; i < 6; i++) {
    int x0 = i * w + 2;
    lcd_fill_rect((uint32_t)x0, (uint32_t)y0, (uint32_t)(w - 4), LCD_MENU_LINES - 4, 1);
    if (keys[i] && keys[i][0]) {
      int tw = lcd_textWidth(&d, keys[i]);
      if (tw > w - 6) d.xspc = -1, tw = lcd_textWidth(&d, keys[i]);
      lcd_setXY(&d, x0 + (w - 4 - tw) / 2, y0 + 3);
      lcd_writeText(&d, keys[i]);
      d.xspc = 0;
    }
  }
}
/* the DMCP line buffer, packed as on the DM42n but with its own oddities (offset, mirrored, LSB first, inverted),
   so usbview.c has to learn it; and the USB serial port into ./acm.out */
uint8_t *lcd_line_addr(int y) {
  static uint8_t lb[LCD_Y][54];
  memset(lb[y], 0xFF, 54);
  for (int x = 0; x < LCD_X; x++)
    if (fb[y][x]) { int X = LCD_X - 1 - x; lb[y][2 + X / 8] &= (uint8_t)~(1u << (X % 8)); }
  return lb[y];
}
static FILE *acm;
void usb_acm_on(void) { if (!acm) acm = fopen("acm.out", "w"); }
int usb_is_on(void) { return acm != NULL; }
void usb_turn_off(void) { if (acm) fclose(acm); acm = NULL; }
void acm_puts(const char *s) { if (acm) { fputs(s, acm); fflush(acm); } }
void qrcode_disp(QRCode *qr, int xo, int yo, int z) { (void)qr; (void)xo; (void)yo; (void)z; }

uint32_t sys_current_ms(void) { return fake_ms; }
void rtc_read(tm_t *tm, dt_t *dt) {
  long mins = clk_h * 60 + clk_mi + (long)(fake_ms / 60000);
  memset(tm, 0, sizeof *tm);
  tm->hour = (uint8_t)((mins / 60) % 24); tm->min = (uint8_t)(mins % 60);
  dt->year = (uint16_t)clk_y; dt->month = (uint8_t)clk_m; dt->day = (uint8_t)clk_d;
}
uint32_t get_rtc_ticks(void) { return 1; }
void sys_delay(uint32_t ms) { fake_ms += ms; }
void sys_timer_start(int ix, uint32_t ms) { (void)ix; (void)ms; }
void start_buzzer_freq(uint32_t f) { (void)f; }
void stop_buzzer(void) {}
int  switch_usb_powered_freq(void) { return 0; }
int  usb_powered(void) { return 0; }
void reset_auto_off(void) {}
void rtc_wakeup_delay(void) {}
int  sys_free_mem(void) { return 610 * 1024; }
int  sys_largest_free_mem(void) { return 560 * 1024; }
void sys_reset(void) { exit(3); }
int  handle_menu(const smenu_t *m, int a, int l) { (void)m; (void)a; (void)l; return 0; }
void wait_for_key_release(int t) { (void)t; }

static void path(const char *p, char *o) { snprintf(o, 256, "simdisk%s", p); }
int sys_disk_ok(void) { return 1; }
int sys_disk_write_enable(int v) { (void)v; return 1; }
int check_create_dir(const char *d) { char p[256], c[300]; path(d, p); snprintf(c, sizeof c, "mkdir -p '%s'", p); return system(c); }
int file_exists(const char *f) { char p[256]; path(f, p); FILE *x = fopen(p, "rb"); if (x) fclose(x); return x != NULL; }
FRESULT f_open(FIL *fp, const TCHAR *p, BYTE mode) {
  char q[256]; path(p, q);
  FILE *f = fopen(q, (mode & FA_WRITE) ? "wb" : "rb");
  fp->obj.fs = (FATFS *)f;
  if (f && !(mode & FA_WRITE)) { fseek(f, 0, SEEK_END); fp->obj.objsize = (FSIZE_t)ftell(f); fseek(f, 0, SEEK_SET); }
  return f ? FR_OK : FR_NO_FILE;
}
FRESULT f_close(FIL *fp) { fclose((FILE *)fp->obj.fs); return FR_OK; }
FRESULT f_read(FIL *fp, void *b, UINT n, UINT *br) { *br = (UINT)fread(b, 1, n, (FILE *)fp->obj.fs); return FR_OK; }
FRESULT f_write(FIL *fp, const void *b, UINT n, UINT *bw) { *bw = (UINT)fwrite(b, 1, n, (FILE *)fp->obj.fs); return FR_OK; }
int file_selection_screen(const char *title, const char *base_dir, const char *ext, file_sel_fn_t sel_fn,
                          int disp_new, int overwrite_check, void *data) {
  (void)title; (void)base_dir; (void)ext; (void)disp_new; (void)overwrite_check;
  const char *f = getenv("NAV47_FILE");
  if (f) { const char *n = strrchr(f, '/'); return sel_fn(f, n ? n + 1 : f, data); }
  return 0;
}

static void snap(const char *name) {
  char fn[256];
  snprintf(fn, sizeof fn, "%s.pbm", name);
  FILE *f = fopen(fn, "w");
  fprintf(f, "P1\n%d %d\n", LCD_X, LCD_Y);
  for (int y = 0; y < LCD_Y; y++) { for (int x = 0; x < LCD_X; x++) fputc(fb[y][x] ? '1' : '0', f); fputc('\n', f); }
  fclose(f);
  snprintf(fn, sizeof fn, "%s.txt", name);
  fclose(fopen(fn, "w"));
  fprintf(stderr, "snap %s\n", name);
}

static int parse_key(const char *s) {
  static const struct { const char *n; int k; } map[] = {
    {"F1",KEY_F1},{"F2",KEY_F2},{"F3",KEY_F3},{"F4",KEY_F4},{"F5",KEY_F5},{"F6",KEY_F6},
    {"0",KEY_0},{"1",KEY_1},{"2",KEY_2},{"3",KEY_3},{"4",KEY_4},{"5",KEY_5},{"6",KEY_6},{"7",KEY_7},{"8",KEY_8},{"9",KEY_9},
    {"ENTER",KEY_ENTER},{"EXIT",KEY_EXIT},{"BSP",KEY_BSP},{"UP",KEY_UP},{"DOWN",KEY_DOWN},{"SHIFT",KEY_SHIFT},
    {"CHS",KEY_CHS},{"DOT",KEY_DOT},{"ADD",KEY_ADD},
  };
  for (unsigned i = 0; i < sizeof map / sizeof *map; i++) if (!strcmp(map[i].n, s)) return map[i].k;
  fprintf(stderr, "unknown key %s\n", s);
  return -1;
}
static void fill(void) {
  if (idle) return;
  while (pending < 0 && spos < nscript) {
    const char *t = script[spos++];
    if (!strncmp(t, "SNAP:", 5)) snap(t + 5);
    else if (!strncmp(t, "WAIT:", 5)) { fake_ms += (uint32_t)atoi(t + 5); idle = 1; return; }
    else if (!strcmp(t, "HOSTL")) { void sim_host_L(void); sim_host_L(); pending = 77; }
    else pending = parse_key(t);
  }
}
int key_empty(void) { fill(); return pending < 0; }
int key_pop(void) { fill(); int k = pending; pending = -1; return k; }
void wait_for_key_press(void) { if (key_empty()) exit(0); key_pop(); }
void sys_sleep(void) { if (idle) { idle = 0; return; } if (key_empty()) { fprintf(stderr, "script done\n"); exit(0); } }

void program_main(void);
int main(int argc, char **argv) {
  ds20.lnfill = ds24.lnfill = 1;
  ds20.f = ds24.f = dsReg.f = &simf;
  sim_sdb.pds_t20 = &ds20; sim_sdb.pds_t24 = &ds24; sim_sdb.pds_fReg = &dsReg;
  const char *c = getenv("NAV47_CLOCK");
  if (c) sscanf(c, "%d-%d-%d %d:%d", &clk_y, &clk_m, &clk_d, &clk_h, &clk_mi);
  script = argv + 1;
  nscript = argc - 1;
  program_main();
  return 0;
}
