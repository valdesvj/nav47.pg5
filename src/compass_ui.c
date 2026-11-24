/* compass_ui.c - NAV47: COMPASS, a body's azimuth against the gyro and the magnetic compass: the gyro error, the
 * true heading, the compass error, the variation (WMM) and the deviation; CARD, the deviation cards, the ship's
 * original and the corrected one (the checks saved into it), drawn over each other, the five coefficients and the
 * advice to compensate over 5 degrees. /NAV47/DEVCARD.CSV (the original), /NAV47/DEVCORR.CSV (the corrected).
 * The state in Nav (the heap): no static RAM. Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#include <main.h>
#include <dmcp.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "compass.h"
#include "gpsui.h"
#include "wmm.h"
#include "almdata.h"

#define CS (N->cmp)
static const char *const CFILE[2] = { DIR "/DEVCARD.CSV", DIR "/DEVCORR.CSV" };
static const char *const CNAME[2] = { "ORIGINAL", "CORRECTED" };
static int cmp_back = PG_GFIX;
double gps_gyro(void);

enum { F_POS, F_LAT, F_LON, F_TIME, F_BODY, F_GBRG, F_GHDG, F_CHDG, F_N };
static const char *const FLAB[F_N] = { "POSITION", "LAT", "LON", "TIME UT", "BODY", "GYRO BRG", "GYRO HDG", "COMPASS" };

/* ---------------------------------------------------------------- the cards' files */
static void card_sort(int k) {
  for (int i = 1; i < CS.n[k]; i++)
    for (int j = i; j > 0 && CS.h[k][j] < CS.h[k][j - 1]; j--) {
      double t = CS.h[k][j]; CS.h[k][j] = CS.h[k][j - 1]; CS.h[k][j - 1] = t;
      t = CS.d[k][j]; CS.d[k][j] = CS.d[k][j - 1]; CS.d[k][j - 1] = t;
    }
}
static void card_load(int k) {
  CS.n[k] = 0;
  char *t = read_text(CFILE[k]);
  if (!t) return;
  for (char *l = strtok(t, "\r\n"); l && CS.n[k] < DEVN; l = strtok(NULL, "\r\n")) {
    double h, d;
    if (l[0] == '#' || sscanf(l, "%lf,%lf", &h, &d) != 2 || h < 0 || h >= 360.5 || fabs(d) > 90) continue;
    CS.h[k][CS.n[k]] = fmod(h, 360.0); CS.d[k][CS.n[k]] = d; CS.n[k]++;
  }
  free(t);
  card_sort(k);
}
static void card_save(int k) {
  if (!sys_disk_ok()) return;
  char b[64 + DEVN * 24], *o = b;
  o += sprintf(o, "# NAV47 deviation card, %s: compass heading, deviation (+ east)\n", CNAME[k]);
  for (int i = 0; i < CS.n[k]; i++) o += sprintf(o, "%.1f,%.2f\n", CS.h[k][i], CS.d[k][i]);
  sys_disk_write_enable(1);
  check_create_dir(DIR);
  FIL f; UINT bw;
  if (f_open(&f, CFILE[k], FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) { f_write(&f, b, (UINT)strlen(b), &bw); f_close(&f); }
  sys_disk_write_enable(0);
}
/* a heading's deviation into a card: replaces one within 2.5 deg, else a new line */
static int card_put(int k, double h, double d) {
  h = fmod(fmod(h, 360.0) + 360.0, 360.0);
  for (int i = 0; i < CS.n[k]; i++) if (fabs(fmod(CS.h[k][i] - h + 540.0, 360.0) - 180.0) < 2.5) { CS.d[k][i] = d; return i; }
  if (CS.n[k] >= DEVN) return -1;
  CS.h[k][CS.n[k]] = h; CS.d[k][CS.n[k]] = d; CS.n[k]++;
  card_sort(k);
  for (int i = 0; i < CS.n[k]; i++) if (CS.h[k][i] == h) return i;
  return 0;
}
static void cards_load(void) { if (!CS.loaded) { card_load(0); card_load(1); CS.loaded = 1; } }

/* ---------------------------------------------------------------- the observation */
typedef struct { double lat, lon, jd, hc, zn, gh, var; int gps, var_ok; const char *tag; CmpOut o; } Obs;
static void observe(Obs *b) {
  b->gps = 0; b->tag = "SET";
  if (CS.man) { b->lat = CS.lat; b->lon = CS.lon; }
  else if (gps_bar(&b->lat, &b->lon, &b->tag) && strcmp(b->tag, "SET")) b->gps = 1;   /* the top bar's source: GPS, DGPS, DR... */
  else { b->lat = N->cfg.lat; b->lon = N->cfg.lon; }
  b->jd = CS.fixed ? CS.jd : ut_now();
  al_compute(&N->al, b->jd, b->lat, b->lon, 0);
  N->valid = 0;                                          /* the views' almanac: theirs again */
  vbody_at(&N->al, CS.body, &b->hc, &b->zn);
  b->gh = CS.has_gh ? CS.ghdg : gps_gyro();
  double yr = wmm_year(b->jd);
  b->var_ok = wmm_valid(yr);
  b->var = b->var_ok ? wmm_var(b->lat, b->lon, yr) : NAN;
  cmp_errors(b->zn, CS.has_gb ? CS.gbrg : NAN, b->gh, CS.has_ch ? CS.chdg : NAN, b->var, &b->o);
}
/* the bodies in turn: the Sun, the Moon, the planets, the stars by brightness, those above the horizon */
static int body_ok(int id, double lat, double lon, double jd) {
  double hc, zn;
  vbody_at(&N->al, id, &hc, &zn); (void)lat; (void)lon; (void)jd;
  return hc > (id >= -1 && id <= 0 ? -1.0 : 0.0);
}
static void body_step(int dir) {
  Obs b; observe(&b);
  int ids[64], n = 0;                                    /* the Sun, the Moon, 4 planets, 58 stars */
  ids[n++] = 0; ids[n++] = -1; for (int p = 2; p <= 5; p++) ids[n++] = -p;
  for (int r = 0; r < 58; r++) ids[n++] = BRIGHT[r];
  int at = 0; for (int i = 0; i < n; i++) if (ids[i] == CS.body) at = i;
  for (int k = 1; k <= n; k++) {
    int i = ((at + dir * k) % n + n) % n;
    if (body_ok(ids[i], b.lat, b.lon, b.jd)) { CS.body = ids[i]; return; }
  }
}

/* ---------------------------------------------------------------- COMPASS */
void compass_open(int back) {
  cards_load();
  cmp_back = back;
  CS.edit = 0; CS.cedit = 0; CS.msg[0] = 0;
  go(PG_GCOMP);
}
static void degs(char *s, double v) { if (isnan(v)) strcpy(s, "--"); else sprintf(s, "%05.1f" NT_DEG, fmod(v + 360.0, 360.0)); }
static void ew(char *s, double v) { if (isnan(v)) strcpy(s, "--"); else sprintf(s, "%.1f" NT_DEG "%c", fabs(v), v >= 0 ? 'E' : 'W'); }

void draw_compass(void) {
  cards_load();
  header("COMPASS");
  Obs b; observe(&b);
  char s[64], v[32];
  for (int f = 0; f < F_N; f++) {
    int y = 18 + 16 * f;
    v[0] = 0;
    switch (f) {
    case F_POS: if (CS.man) strcpy(v, "MANUAL"); else snprintf(v, sizeof v, "%s%s", b.gps ? b.tag : "SET", b.gps ? "" : " (no fix)"); break;
    case F_LAT: fmt_dmn2(v, b.lat, 2, b.lat < 0 ? 'S' : 'N'); break;
    case F_LON: fmt_dmn2(v, b.lon, 3, b.lon < 0 ? 'W' : 'E'); break;
    case F_TIME: { double hh = fmod(b.jd - 0.5, 1.0) * 24; int sec = (int)floor(hh * 3600 + 0.5);
                   sprintf(v, "%02d:%02d:%02d %s", sec / 3600 % 24, sec / 60 % 60, sec % 60, CS.fixed ? "set" : "now"); break; }
    case F_BODY: snprintf(v, sizeof v, "%.10s", vbody_name(CS.body)); break;
    case F_GBRG: if (CS.has_gb) degs(v, CS.gbrg); else strcpy(v, "type it"); break;
    case F_GHDG: if (CS.has_gh) degs(v, CS.ghdg); else if (!isnan(b.gh)) { degs(v, b.gh); strcat(v, " nmea"); } else strcpy(v, "type it"); break;
    case F_CHDG: if (CS.has_ch) degs(v, CS.chdg); else strcpy(v, "type it"); break;
    }
    if (CS.edit && CS.sel == f) sprintf(v, "%s_", ebuf);
    if (f == CS.sel) nt_inv(4, y, FLAB[f]); else nt(4, y, FLAB[f]);
    nt(92, y, v);
  }
  /* the body's azimuth, then the results on the right */
  const int X = 228, XR = LCD_X - 4;
  sprintf(s, "Zn %05.1f" NT_DEG, b.zn); nt(X, 18, s);
  sprintf(s, "Hc %.1f" NT_DEG, b.hc); nt_r(XR, 18, s);
  gfx_hline_dotted(X - 4, LCD_X - 1, 34, 2);
  nt(X, 38, "GYRO ERR");
  if (isnan(b.o.gyro_err)) strcpy(s, "--"); else sprintf(s, "%.1f" NT_DEG " %s", fabs(b.o.gyro_err), b.o.gyro_err > 0 ? "LOW" : b.o.gyro_err < 0 ? "HIGH" : "");
  nt_r(XR, 38, s);
  nt(X, 54, "TRUE HDG"); degs(s, b.o.true_hdg); nt_r(XR, 54, s);
  nt(X, 70, "VAR"); if (b.var_ok) ew(s, b.var); else strcpy(s, "expired"); nt_r(XR, 70, s);
  nt(X, 86, "COMP ERR"); ew(s, b.o.comp_err); nt_r(XR, 86, s);
  if (!isnan(b.o.dev)) { gfx_rect(X - 4, 101, LCD_X - X + 4, 17, 1); nt_inv(X, 102, "DEVIATION"); ew(s, b.o.dev); nt_inv(XR - nt_w(s), 102, s); }
  else { nt(X, 102, "DEVIATION"); nt_r(XR, 102, "--"); }
  /* the cards at this compass heading */
  for (int k = 0; k < 2; k++) {
    DevFit fit; dev_fit(CS.n[k], CS.h[k], CS.d[k], &fit);
    double ch = CS.has_ch ? CS.chdg : NAN, d = isnan(ch) ? NAN : dev_at(CS.n[k], CS.h[k], CS.d[k], &fit, ch);
    if (CS.n[k]) { ew(v, d); snprintf(s, sizeof s, "%s card %s", k ? "corrected" : "original", v); }
    else snprintf(s, sizeof s, "%s card: none", k ? "corrected" : "original");
    draw_text(&TINY, X, 122 + 10 * k, s, 0);
  }
  if (CS.msg[0]) draw_text(&TINY, X, 144, CS.msg, 1);
  hint(CS.edit ? (CS.sel == F_LAT || CS.sel == F_LON ? "DD.MMm  +/- S or W  ENTER" : CS.sel == F_TIME ? "HH.MMSS (UT, today)  ENTER" : "degrees  ENTER")
               : "8 2 line  ENTER type  4 6 body  0 now / the GPS's");
}

/* ---------------------------------------------------------------- CARD */
static void curve_pts(int k, const DevFit *f, int x0, int y0, int w, int h, double scale, int dashed) {
  if (!CS.n[k]) return;
  int px = -1, py = 0;
  for (int i = 0; i <= w; i++) {
    double hd = 360.0 * i / w, d = dev_at(CS.n[k], CS.h[k], CS.d[k], f, hd);
    int y = y0 + h / 2 - (int)lround(d / scale * (h / 2));
    if (y < y0) y = y0; if (y > y0 + h) y = y0 + h;
    if (px >= 0 && (!dashed || (i / 4) % 2 == 0)) gfx_line(px, py, x0 + i, y, 1);
    px = x0 + i; py = y;
  }
  for (int i = 0; i < CS.n[k]; i++) {
    int x = x0 + (int)lround(CS.h[k][i] / 360.0 * w), y = y0 + h / 2 - (int)lround(CS.d[k][i] / scale * (h / 2));
    if (y < y0 || y > y0 + h) continue;
    if (k == 0) gfx_frame(x - 2, y - 2, 5, 5); else { gfx_line(x - 2, y - 2, x + 2, y + 2, 1); gfx_line(x - 2, y + 2, x + 2, y - 2, 1); }
  }
}
void draw_devcard(void) {
  cards_load();
  char s[96], t[24];
  sprintf(s, "CARD %s", CNAME[CS.card]);
  header(s);
  int k = CS.card, o = 1 - k;
  DevFit fk, fo;
  dev_fit(CS.n[k], CS.h[k], CS.d[k], &fk); dev_fit(CS.n[o], CS.h[o], CS.d[o], &fo);
  /* the table: this card's headings, its deviation, the other card's there, the difference */
  draw_text(&TINY, 4, 17, k ? "HDG    CORR    ORIG    DIFF" : "HDG    ORIG    CORR    DIFF", 0);
  if (CS.cur >= CS.n[k]) CS.cur = CS.n[k] ? CS.n[k] - 1 : 0;
  int vis = 8, top = CS.cur - vis / 2;
  if (top > CS.n[k] - vis) top = CS.n[k] - vis;
  if (top < 0) top = 0;
  for (int i = top; i < CS.n[k] && i < top + vis; i++) {
    int y = 27 + 15 * (i - top);
    double od = dev_at(CS.n[o], CS.h[o], CS.d[o], &fo, CS.h[k][i]);
    sprintf(s, "%03.0f", CS.h[k][i]);
    if (i == CS.cur && !CS.cedit) nt_inv(4, y, s); else nt(4, y, s);
    ew(t, CS.d[k][i]); nt_r(98, y, t);
    if (CS.n[o]) { ew(t, od); draw_text_r(&TINY, 148, y + 4, t, 0); ew(t, CS.d[k][i] - od); draw_text_r(&TINY, 192, y + 4, t, 0); }
  }
  if (!CS.n[k]) nt(4, 40, "empty: ADD");
  if (CS.cedit == 1) { sprintf(s, "HEADING %s_", ebuf); nt_inv(4, 150, s); }
  else if (CS.cedit == 2) { sprintf(s, "DEV at %03.0f: %s_ (+/- W)", CS.newh, ebuf); nt_inv(4, 150, s); }
  /* the plot: 0-360 across, the deviation up (east) and down (west) */
  const int X0 = 200, Y0 = 20, W = 196, H = 112;
  double m = 6;
  for (int q = 0; q < 2; q++) for (int i = 0; i < CS.n[q]; i++) if (fabs(CS.d[q][i]) + 1 > m) m = fabs(CS.d[q][i]) + 1;
  if (fabs(fk.max) + 1 > m) m = fabs(fk.max) + 1;
  if (fabs(fo.max) + 1 > m) m = fabs(fo.max) + 1;
  gfx_frame(X0, Y0, W + 1, H + 1);
  gfx_hline_dotted(X0, X0 + W, Y0 + H / 2, 2);
  for (int sg = -1; sg <= 1; sg += 2) { int y = Y0 + H / 2 - (int)lround(sg * DEV_LIMIT / m * (H / 2)); gfx_hline_dotted(X0, X0 + W, y, 6); }
  for (int q = 1; q < 4; q++) { int x = X0 + W * q / 4; for (int y = Y0; y < Y0 + H; y += 4) gfx_pixel(x, y, 1); }
  draw_text(&TINY, X0 + 2, Y0 + 2, "E", 0); draw_text(&TINY, X0 + 2, Y0 + H - 9, "W", 0);
  sprintf(s, "%.0f" NT_DEG, m); draw_text_r(&TINY, X0 + W - 2, Y0 + 2, s, 0);
  draw_text(&TINY, X0, Y0 + H + 2, "000    090    180    270", 0);
  curve_pts(0, k == 0 ? &fk : &fo, X0, Y0, W, H, m, 0);
  curve_pts(1, k == 1 ? &fk : &fo, X0, Y0, W, H, m, 1);
  draw_text(&TINY, X0, Y0 + H + 11, "-- orig  - - corr  ... 5" SYM_DEG, 0);
  /* the coefficients and the advice (this card) */
  int y = 156;
  if (CS.n[k]) {
    if (fk.fitted) snprintf(s, sizeof s, "A %+.1f B %+.1f C %+.1f D %+.1f E %+.1f  max %.1f%c at %03.0f", fk.c[0], fk.c[1], fk.c[2], fk.c[3], fk.c[4], fabs(fk.max), fk.max >= 0 ? 'E' : 'W', fk.at);
    else snprintf(s, sizeof s, "%d headings: 5 or more round the card for its coefficients; max %.1f%c", CS.n[k], fabs(fk.max), fk.max >= 0 ? 'E' : 'W');
    draw_text(&TINY, 4, y, s, 0); y += 9;
    char adv[480];
    dev_advice(&fk, adv, sizeof adv);
    if (!adv[0]) { draw_text(&TINY, 4, y, "within 5" SYM_DEG ": no compensation needed", 0); }
    for (char *l = adv, *e; *l && y < MENU_Y - 2 * LH; l = e ? e + 1 : l + strlen(l)) {   /* a line each, cut to the screen */
      e = strchr(l, '\n'); char line[70]; int n = e ? (int)(e - l) : (int)strlen(l); if (n > 66) n = 66;
      memcpy(line, l, (size_t)n); line[n] = 0;
      draw_text(&TINY, 4, y, line, l == adv); y += 9;
    }
  }
  if (CS.msg[0]) { hint(CS.msg); return; }
  hint(CS.confirm ? "press it again to confirm" : CS.cedit ? "ENTER ok  EXIT cancel" : "8 2 row  ENTER edit  ORIG/CORR: which card");
}

/* ---------------------------------------------------------------- keys */
int compass_editing(void) { return (page == PG_GCOMP && CS.edit) || (page == PG_GDEV && CS.cedit); }
static const char *CMENU[6] = { "POS", "NOW", "BODY+", "SAVE", "CARD", "BACK" };
static const char *DMENU[6] = { "ADD", "DEL", "CORR", "ADOPT", "CLEAR", "BACK" };
static const char *EMENU[6] = { "", "", "", "", "", "CANCEL" };
const char **compass_menu(void) {
  if (compass_editing()) return EMENU;
  DMENU[2] = CS.card ? "ORIG" : "CORR";
  return page == PG_GDEV ? DMENU : CMENU;
}
static void start_edit(void) { CS.edit = 1; elen = 0; ebuf[0] = 0; }
static int type_key(int key) {                           /* ebuf: digits, the point, the sign */
  int d = digit(key);
  if (key == KEY_BSP) { if (elen) ebuf[--elen] = 0; return 1; }
  if (key == KEY_CHS) { if (ebuf[0] == '-') { memmove(ebuf, ebuf + 1, (size_t)elen); elen--; } else if (elen < 14) { memmove(ebuf + 1, ebuf, (size_t)elen + 1); ebuf[0] = '-'; elen++; } return 1; }
  if (key == KEY_DOT && elen < 14 && !strchr(ebuf, '.')) { ebuf[elen++] = '.'; ebuf[elen] = 0; return 1; }
  if (d >= 0 && elen < 14) { ebuf[elen++] = (char)('0' + d); ebuf[elen] = 0; return 1; }
  return 0;
}
static void commit(void) {
  double v = atof(ebuf);
  CS.edit = 0;
  if (!elen) return;
  switch (CS.sel) {
  case F_LAT: { double p = parse_dm(ebuf); if (fabs(p) <= 90) { if (!CS.man) { Obs b; observe(&b); CS.lon = b.lon; } CS.lat = p; CS.man = 1; } break; }
  case F_LON: { double p = parse_dm(ebuf); if (fabs(p) <= 180) { if (!CS.man) { Obs b; observe(&b); CS.lat = b.lat; } CS.lon = p; CS.man = 1; } break; }
  case F_TIME: { double t = ut_now(), day = floor(t - 0.5) + 0.5, a = fabs(v); int hh = (int)a, mm = (int)((a - hh) * 100 + 1e-6);
                 double ss = ((a - hh) * 100 - mm) * 100; if (hh < 24 && mm < 60 && ss < 60) { CS.jd = day + (hh + mm / 60.0 + ss / 3600.0) / 24; CS.fixed = 1; } break; }
  case F_GBRG: if (v >= 0 && v <= 360) { CS.gbrg = v; CS.has_gb = 1; if (!CS.fixed) { CS.jd = ut_now(); CS.fixed = 1; } } break;   /* the time of the bearing */
  case F_GHDG: if (v >= 0 && v <= 360) { CS.ghdg = v; CS.has_gh = 1; } break;
  case F_CHDG: if (v >= 0 && v <= 360) { CS.chdg = v; CS.has_ch = 1; } break;
  }
}
static void card_key(int key) {
  int k = CS.card;
  if (CS.cedit) {
    if (key == KEY_EXIT || key == KEY_F6) { CS.cedit = 0; return; }
    if (key == KEY_ENTER) {
      double v = atof(ebuf);
      if (CS.cedit == 1) { if (elen && v >= 0 && v < 360) { CS.newh = v; CS.cedit = 2; elen = 0; ebuf[0] = 0; } else CS.cedit = 0; return; }
      if (elen && fabs(v) <= 90) { int i = card_put(k, CS.newh, v); if (i >= 0) { CS.cur = i; card_save(k); } else strcpy(CS.msg, "the card is full (36)"); }
      CS.cedit = 0; return;
    }
    type_key(key); return;
  }
  CS.confirm = 0;                                       /* a key other than ADOPT / CLEAR again: not confirmed */
  if (key == KEY_8 || key == KEY_UP) { if (CS.cur > 0) CS.cur--; }
  else if (key == KEY_2 || key == KEY_DOWN) { if (CS.cur + 1 < CS.n[k]) CS.cur++; }
  else if (key == KEY_ENTER && CS.n[k]) { CS.newh = CS.h[k][CS.cur]; CS.cedit = 2; elen = 0; ebuf[0] = 0; }   /* this heading's deviation */
  else if (key == KEY_EXIT) go(PG_GCOMP);
}
int compass_key(int key) {
  CS.msg[0] = 0;
  if (page == PG_GDEV) { card_key(key); return 1; }
  if (CS.edit) {
    if (key == KEY_EXIT || key == KEY_F6) CS.edit = 0;
    else if (key == KEY_ENTER) commit();
    else type_key(key);
    return 1;
  }
  if (key == KEY_8 || key == KEY_UP) CS.sel = (CS.sel + F_N - 1) % F_N;
  else if (key == KEY_2 || key == KEY_DOWN) CS.sel = (CS.sel + 1) % F_N;
  else if (key == KEY_4) body_step(-1);
  else if (key == KEY_6) body_step(1);
  else if (key == KEY_0) {                                 /* back to the live values */
    if (CS.sel == F_TIME || CS.sel == F_GBRG) CS.fixed = 0;
    if (CS.sel == F_GHDG) CS.has_gh = 0;
    if (CS.sel == F_POS || CS.sel == F_LAT || CS.sel == F_LON) CS.man = 0;
  }
  else if (key == KEY_ENTER) {
    if (CS.sel == F_POS) { if (!CS.man) { Obs b; observe(&b); CS.lat = b.lat; CS.lon = b.lon; } CS.man ^= 1; }
    else if (CS.sel == F_BODY) body_step(1);
    else start_edit();
  }
  else if (key == KEY_EXIT) { go(cmp_back); if (cmp_back == PG_GFIX) menu_page = 4; }
  return 1;
}
void compass_fn(int fn) {
  CS.msg[0] = 0;
  if (page == PG_GDEV) {
    int k = CS.card;
    if (CS.cedit) { if (fn == 5) CS.cedit = 0; return; }
    if (fn != 3 && fn != 4) CS.confirm = 0;
    if (fn == 0) { CS.cedit = 1; elen = 0; ebuf[0] = 0; }                               /* ADD: the heading, then its deviation */
    else if (fn == 1 && CS.n[k]) {                                                        /* DEL the row */
      for (int i = CS.cur; i + 1 < CS.n[k]; i++) { CS.h[k][i] = CS.h[k][i + 1]; CS.d[k][i] = CS.d[k][i + 1]; }
      CS.n[k]--; card_save(k);
    }
    else if (fn == 2) { CS.card ^= 1; CS.cur = 0; }
    else if (fn == 3) {                                                                  /* ADOPT: the corrected the new original */
      if (CS.confirm != -3) { CS.confirm = -3; return; }
      memcpy(CS.h[0], CS.h[1], sizeof CS.h[0]); memcpy(CS.d[0], CS.d[1], sizeof CS.d[0]); CS.n[0] = CS.n[1];
      card_save(0); CS.confirm = 0; CS.card = 0; strcpy(CS.msg, "the corrected card is the original now");
    }
    else if (fn == 4) {                                                                  /* CLEAR this card */
      if (CS.confirm != -4) { CS.confirm = -4; return; }
      CS.n[k] = 0; card_save(k); CS.confirm = 0;
    }
    else if (fn == 5) go(PG_GCOMP);
    return;
  }
  if (CS.edit) { if (fn == 5) CS.edit = 0; return; }
  if (fn == 0) { if (!CS.man) { Obs b; observe(&b); CS.lat = b.lat; CS.lon = b.lon; } CS.man ^= 1; }   /* POS: GPS / MANUAL */
  else if (fn == 1) { CS.fixed = 0; CS.has_gb = 0; }                                       /* NOW: a new observation */
  else if (fn == 2) body_step(1);
  else if (fn == 3) {                                                                    /* SAVE: into the corrected card */
    Obs b; observe(&b);
    if (isnan(b.o.dev)) { strcpy(CS.msg, "needs the bearing, the gyro and the compass headings"); return; }
    int i = card_put(1, CS.chdg, b.o.dev);
    if (i < 0) { strcpy(CS.msg, "the corrected card is full (36)"); return; }
    card_save(1);
    snprintf(CS.msg, sizeof CS.msg, "saved %03.0f %.1f%c (corrected)", CS.chdg, fabs(b.o.dev), b.o.dev >= 0 ? 'E' : 'W');
  }
  else if (fn == 4) { CS.cur = 0; CS.cedit = 0; CS.confirm = 0; go(PG_GDEV); }
  else if (fn == 5) { go(cmp_back); if (cmp_back == PG_GFIX) menu_page = 4; }
}
