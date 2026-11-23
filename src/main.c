/*
 * NAV47 for the SwissMicros DM42n (DMCP5 program, .pg5)
 * Almanac 47 + Zones 47 + Tide 47 (with FLOW) in one native program.
 * Copyright (C) 2026 Victor Valdes <valdes.vj@gmail.com> - GPL-3.0-or-later (see LICENSE).
 *
 * DOES NOT REPLACE THE NAUTICAL ALMANAC OR OFFICIAL TIDE TABLES. Not for use as the primary means of navigation.
 */
#include "app.h"

/* ------------------------------------------------------------------ */
/*  State (heap: a DMCP5 program has 8 KB of static RAM); the types in app.h */
/* ------------------------------------------------------------------ */
Nav *N;
int page = PG_VALM, menu_page, shift;
static int vframe;
double toff;                      /* hours added to the time by the arrows */
static int edit_field = -1, sel_field;
int pick_cursor, flow_hw_step, flow_day_step;
static int split_first;           /* SPLIT scroll */
int st_day_off, pick_back;        /* TIDE (station) day; where STATNS returns */
int cmp_edit = -1;                 /* COMPARE: editing CUSTOM 0 a, 1 1/f */
double cmp_a;
char cmp_msg[32];
double voy_kn = 12;
double voy_etd0;                   /* VOYAGE: the departure kept, UT JD (ROUTE.TXT ETD); 0 = now */                /* VOYAGE: the speed, knots (ROUTE.TXT SPEED) */
int wp_cur, wp_edit = -1, wp_new;   /* wp_new: the edit is of a waypoint just added (EXIT removes it) */          /* ROUTE: the cursor; editing 0 lat, 1 lon */
double wp_lat;
char ebuf[16];
int elen;


/* ------------------------------------------------------------------ */
/*  Time                                                              */
/* ------------------------------------------------------------------ */
static double clock_jd_local(void) {
  tm_t tm;
  dt_t dt;
  rtc_read(&tm, &dt);
  return jd_ymd(dt.year, dt.month, dt.day, tm.hour + tm.min / 60.0 + tm.sec / 3600.0);
}

double ut_now(void) {
  double gj;
  double base = N->cfg.live ? (gps_ut(&gj) ? gj : clock_jd_local() - N->cfg.tz / 24.0) : N->cfg.fixed_jd;
  return base + toff / 24.0;
}

/* the zone of the times shown (SET TIMES, +/- on any page): the clock TZ for local time, 0 for UT */
double show_tz(void) { return N->cfg.lt ? N->cfg.tz : 0.0; }
const char *show_name(void) { return N->cfg.lt ? "LT" : "UT"; }

/* ------------------------------------------------------------------ */
/*  Formatting                                                        */
/* ------------------------------------------------------------------ */
static void fmt_dm(char *s, double deg, int ddig) {      /* ddd°mm.m' */
  double a = fabs(deg);
  int d = (int)a;
  double m = (a - d) * 60.0;
  if (m >= 59.95) { d++; m = 0.0; }
  sprintf(s, ddig == 3 ? "%03d" SYM_DEG "%04.1f" SYM_MIN : "%02d" SYM_DEG "%04.1f" SYM_MIN, d, m);
}

void fmt_lat(char *s, double v) { fmt_dm(s, v, 2); strcat(s, v < 0 ? "S" : "N"); }
void fmt_lon(char *s, double v) { fmt_dm(s, v, 3); strcat(s, v < 0 ? "W" : "E"); }

void fmt_hm(char *s, double h) {                   /* hours -> hh:mm, 99 -> --:-- */
  if (h >= 98) { strcpy(s, "--:--"); return; }
  h = fmod(h + 48.0, 24.0);
  int m = (int)floor(h * 60.0 + 0.5);
  if (m >= 1440) m -= 1440;
  sprintf(s, "%02d:%02d", m / 60, m % 60);
}

void fmt_off(char *s, double o) {                  /* +3, -5:30 */
  char sg = o < 0 ? '-' : '+';
  o = fabs(o);
  int h = (int)o, m = (int)floor((o - h) * 60 + 0.5);
  if (m) sprintf(s, "%c%d:%02d", sg, h, m); else sprintf(s, "%c%d", sg, h);
}

/* the top bar, the same on every page (views.c top_bar): the date and time shown (UT or LT) on the left, the
   position on the right, the page's name in the middle; bar_at: at another instant (a tide day stepped) */
static int bar_live;                                     /* this page shows the GPS's position in its top bar */
static double bar_lat, bar_lon;
static double bar_shown_lat = 99, bar_shown_lon;          /* the position the bar showed last (SET's pages) */
void bar_at(double ut, const char *name) {
  const char *tag = top_tag;
  if (fabs(ut - ut_now()) > 1.0 / 1440) top_tag = NULL;  /* another instant (VOYAGE, TIDE's other day): no live tag */
  top_bar(ut + show_tz() / 24.0, N->cfg.lt, bar_live ? bar_lat : N->cfg.lat, bar_live ? bar_lon : N->cfg.lon, name);
  top_tag = tag;
}
/* the latitude or longitude to 0.001', "26°19.567'N" */
void fmt_dm3(char *s, double v, int lat) {
  double a = fabs(v);
  int d = (int)a;
  double m = (a - d) * 60;
  if (m >= 59.9995) { d++; m = 0; }
  sprintf(s, lat ? "%02d" NT_DEG "%06.3f'%c" : "%03d" NT_DEG "%06.3f'%c", d, m, lat ? (v < 0 ? 'S' : 'N') : (v < 0 ? 'W' : 'E'));
}
void header(const char *name) { bar_at(ut_now(), name); }

/* the degree sign of the system font text */
void fmt_dmn(char *s, double deg, int ddig, char hemi) {     /* 26°30.0N 050°10.0E */
  double a = fabs(deg);
  int d = (int)a;
  double m = (a - d) * 60.0;
  if (m >= 59.95) { d++; m = 0.0; }
  sprintf(s, ddig == 3 ? "%03d" NT_DEG "%04.1f%c" : "%02d" NT_DEG "%04.1f%c", d, m, hemi);
}

void fmt_dmn2(char *s, double deg, int ddig, char hemi) {    /* to 0.01': 26°30.12N 050°10.05E (the route's points) */
  double a = fabs(deg);
  int d = (int)a;
  double m = (a - d) * 60.0;
  if (m >= 59.995) { d++; m = 0.0; }
  sprintf(s, ddig == 3 ? "%03d" NT_DEG "%05.2f%c" : "%02d" NT_DEG "%05.2f%c", d, m, hemi);
}

static const char *MON[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
static const char *WDAY[7] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };


/* ------------------------------------------------------------------ */
/*  ZONES (as ZONE47)                                                 */
/* ------------------------------------------------------------------ */
static void draw_zones(void) {
  double ut = ut_now();
  const Zone *z = &ZONES[N->cfg.zone];
  if (N->cfg.city >= z->n) N->cfg.city = 0;
  const City *c = &z->c[N->cfg.city];
  int on;
  double off = city_offset(z, c, ut, &on);
  char s[64], t[16], o[12];
  int y, m, d;
  double h;
  jd_to_ymd(ut + off / 24.0, &y, &m, &d, &h);
  long n = jdn(y, m, d);
  header("ZONES");                                       /* the clock (UT or LT) in the bar */
  fmt_off(o, off);                                        /* the zone's own time, and its difference from the bar */
  sprintf(s, "UTC%s", off == 0 ? "" : o);
  nt_inv(2, 17, s);
  fmt_hm(t, h);
  nt(100, 17, t);
  sprintf(s, "%s %d %s", WDAY[(n + 1) % 7], d, MON[m - 1]);
  nt(160, 17, s);
  double dd = off - show_tz();
  if (dd == 0) strcpy(s, "same time"); else { fmt_off(o, dd); sprintf(s, "%s h", o); }
  nt_r(LCD_X - 2, 17, s);
  /* map: 120 x 45 dots, 3 px each, from x 20; zone column c (5 dots, 15 deg) is UTC c - 11 */
  int mx = 20, my = 36;
  for (int j = 0; j < MAP_NCOL; j++) {
    int hl = (j / 5) == z->col;
    if (hl) gfx_rect(mx + 3 * j, my, 3, 3 * MAP_NROW, 1);
    for (int i = 0; i < MAP_NROW; i++) {
      int land = (int)((MAP_COLS[j] >> i) & 1ULL);
      if (land) gfx_rect(mx + 3 * j, my + 3 * i, 2, 2, hl ? 0 : 1);
    }
  }
  int ty = my + 3 * MAP_NROW + 1;
  for (int k = 0; k <= 24; k++) gfx_rect(mx + 15 * k - 1, ty, 1, 2, 1);
  for (int k = -10; k <= 12; k += 2) {
    sprintf(s, "%+d", k);
    if (!k) strcpy(s, "0");
    draw_text(&TINY, mx + 15 * (k + 11) + 7 - text_width(&TINY, s) / 2, ty + 2, s, 0);
  }
  if (c->kind == DST_IRREGULAR) sprintf(s, "%s %s  %d/%d  DST varies", c->name, c->cc, N->cfg.city + 1, z->n);
  else if (c->kind == DST_RULE) {
    fmt_off(o, c->dst);
    sprintf(s, on ? "%s %s  %d/%d  summer %s h" : "%s %s  %d/%d  winter, DST %s h", c->name, c->cc, N->cfg.city + 1, z->n, o);
  } else sprintf(s, "%s %s  %d/%d", c->name, c->cc, N->cfg.city + 1, z->n);
  nt(2, 192, s);
}

/* ------------------------------------------------------------------ */
/*  SET                                                               */
/* ------------------------------------------------------------------ */
enum { F_LAT, F_LON, F_TZ, F_LT, F_LIVE, F_DATE, F_TIME, F_DUT1, F_WDIR, F_WKN, F_COUNT };
static const char *FNAME[F_COUNT] = { "LATITUDE", "LONGITUDE", "CLOCK TZ", "SHOW TIMES", "TIME", "DATE (UT)", "TIME (UT)",
                                      "DUT1", "WIND FROM", "WIND KN" };
static const char *FHELP[F_COUNT] = { "DD.MMm, +/- south", "DDD.MMm, +/- west", "clock - UT, hours",
                                      "ENTER, or +/- on any page", "ENTER switches", "YYYY.MMDD", "HH.MM",
                                      "UT1-UTC s, web: Almanac", "degrees true", "knots" };

static void field_text(int f, char *s) {
  Cfg *c = &N->cfg;
  int y, m, d;
  double h;
  jd_to_ymd(c->fixed_jd + 0.5 / 1440, &y, &m, &d, &h);
  switch (f) {
  case F_LAT: fmt_lat(s, c->lat); break;
  case F_LON: fmt_lon(s, c->lon); break;
  case F_TZ: fmt_off(s, c->tz); break;
  case F_LT: strcpy(s, c->lt ? "LOCAL (LT)" : "UT"); break;
  case F_LIVE: strcpy(s, c->live ? "LIVE clock" : "FIXED"); break;
  case F_DATE: sprintf(s, "%04d-%02d-%02d", y, m, d); break;
  case F_TIME: { int mi = (int)floor(h * 60); sprintf(s, "%02d:%02d", mi / 60, mi % 60); break; }
  case F_DUT1: sprintf(s, "%+.2f s", c->dut1); break;
  case F_WDIR: sprintf(s, "%03.0f" SYM_DEG, c->wdir); break;
  case F_WKN: sprintf(s, "%.0f kn", c->wkn); break;
  }
}

double parse_dm(const char *s) {          /* DD.MMm -> degrees */
  double v = atof(s);
  return flow_dm(v);
}

static void field_commit(int f) {
  Cfg *c = &N->cfg;
  double v = atof(ebuf);
  int y, m, d;
  double h;
  jd_to_ymd(c->fixed_jd + 0.5 / 1440, &y, &m, &d, &h);
  switch (f) {
  case F_LAT: if (fabs(parse_dm(ebuf)) <= 90) c->lat = parse_dm(ebuf); break;
  case F_LON: if (fabs(parse_dm(ebuf)) <= 180) c->lon = parse_dm(ebuf); break;
  case F_TZ: if (fabs(v) <= 14) c->tz = flow_dm(v) ; break;
  case F_DATE: {
    int yy = (int)v, mm = (int)floor((v - yy) * 100 + 1e-6), dd = (int)lround(((v - yy) * 100 - mm) * 100);
    if (yy > 1900 && mm >= 1 && mm <= 12 && dd >= 1 && dd <= 31) c->fixed_jd = jd_ymd(yy, mm, dd, h);
    break;
  }
  case F_TIME: {
    double hh = flow_dm(v);
    if (hh >= 0 && hh < 24) c->fixed_jd = jd_ymd(y, m, d, hh);
    break;
  }
  case F_DUT1: if (fabs(v) <= 0.9) c->dut1 = v; break;   /* |UT1 - UTC| < 0.9 s by definition */
  case F_WDIR: if (v >= 0 && v <= 360) c->wdir = v; break;
  case F_WKN: if (v >= 0 && v < 100) c->wkn = v; break;
  }
  N->valid = 0;
}

static void draw_set(void) {
  header("SET");
  draw_text_r(&TINY, LCD_X - 2, 16, "8 2 field  ENTER edit", 0);
  char v[48];
  int y = 22;
  for (int f = 0; f < F_COUNT; f++) {
    if ((f == F_DATE || f == F_TIME) && N->cfg.live) continue;
    field_text(f, v);
    if (f == F_LAT || f == F_LON) {
      if (f == F_LAT) fmt_dmn(v, N->cfg.lat, 2, N->cfg.lat < 0 ? 'S' : 'N');
      else fmt_dmn(v, N->cfg.lon, 3, N->cfg.lon < 0 ? 'W' : 'E');
    }
    if (f == F_WDIR) sprintf(v, "%03.0f" NT_DEG, N->cfg.wdir);
    if (edit_field == f) sprintf(v, "%s_", ebuf);
    if (f == sel_field) nt_inv(6, y, FNAME[f]); else nt(6, y, FNAME[f]);
    nt(140, y, v);
    draw_text_r(&TINY, LCD_X - 2, y + 5, FHELP[f], 0);
    y += N->cfg.live ? 20 : 17;
  }
  draw_text(&TINY, 2, MENU_Y - LH, "As Almanac 47: 26.30 = 26" SYM_DEG "30'. Fixed time: date and UT (live off)", 0);
}

/* ------------------------------------------------------------------ */
/*  Files                                                             */
/* ------------------------------------------------------------------ */
char *read_text(const char *path) {
  if (!sys_disk_ok() || !file_exists(path)) return NULL;
  FIL f;
  if (f_open(&f, path, FA_READ) != FR_OK) return NULL;
  UINT sz = (UINT)f_size(&f), br = 0;
  char *b = (char *)malloc(sz + 1);
  if (b) { f_read(&f, b, sz, &br); b[br] = 0; }
  f_close(&f);
  return b;
}

static int load_file(const char *path) {
  char *t = read_text(path);
  if (!t) { snprintf(N->msg, sizeof N->msg, "cannot read %s", path); return 0; }
  char err[48];
  int r;
  if (!strncmp(t, "NAV47 FLOW", 10)) {
    r = flw_parse(t, &N->ffl, err, sizeof err);
    if (r > 0) { N->have_ffl = 1; snprintf(N->msg, sizeof N->msg, "atlas: %d sea cells", r); }
  } else {
    r = tid_parse(t, &N->fr, &N->nfr, err, sizeof err);
    if (r > 0) snprintf(N->msg, sizeof N->msg, "%d stations loaded", r);
  }
  free(t);               /* the parsers copy what they keep */
  if (r <= 0) { snprintf(N->msg, sizeof N->msg, "%s", err); return 0; }
  return 1;
}

static int file_sel_cb(const char *fpath, const char *fname, void *data) {
  (void)fname; (void)data;
  if (load_file(fpath) && N->cfg.nfiles < 4) {
    int dup = 0;
    for (int i = 0; i < N->cfg.nfiles; i++) if (!strcmp(N->cfg.files[i], fpath)) dup = 1;
    if (!dup) { strncpy(N->cfg.files[N->cfg.nfiles], fpath, 39); N->cfg.files[N->cfg.nfiles][39] = 0; N->cfg.nfiles++; }
  }
  return MRET_EXIT;
}

static void draw_files(void) {
  header("FILES");
  char s[96];
  int y = 26;
  draw_text(&TINY, 2, y, "Stations (.TID) and atlas (.FLW) from " TIDEDIR, 0); y += LH;
  draw_text(&TINY, 2, y, "PC: tools/gen_tides.py files ~/opt/tide47 OUT", 0); y += LH + 4;
  sprintf(s, "compiled in: %d public, %d private regions, atlas %s", PUB_NREGIONS, PRIV_NREGIONS,
          FLOW_ATLAS_PRESENT ? "yes" : "no");
  draw_text(&TINY, 2, y, s, 0); y += LH + 4;
  draw_text(&TINY, 2, y, "LOADED FROM FILES (reloaded at start):", 0); y += LH;
  for (int i = 0; i < N->cfg.nfiles; i++, y += LH) draw_text(&TINY, 10, y, N->cfg.files[i], 0);
  for (int r = 0; r < N->nfr; r++, y += LH) {
    sprintf(s, "region %s: %s, %d stations", N->fr[r].code, N->fr[r].title, N->fr[r].n);
    draw_text(&TINY, 10, y, s, 0);
  }
  if (N->have_ffl) { sprintf(s, "atlas: %dx%d, reference %s", N->ffl.nlat, N->ffl.nlon, N->ffl.ref); draw_text(&TINY, 10, y, s, 0); y += LH; }
  /* your own files in /NAV47: LOAD reads them again */
  y = MENU_Y - 6 * LH - 4;
  draw_text(&TINY, 2, y, "YOUR FILES IN /NAV47 (LOAD reads them again):", 0); y += LH;
  sprintf(s, "OBJECTS.CSV  your platforms and zones: %d", obj_count()); draw_text(&TINY, 10, y, s, 0); y += LH;
  sprintf(s, "WAYPTS.CSV   your waypoints (WPTS): %d", wpdb_count()); draw_text(&TINY, 10, y, s, 0); y += LH;
  draw_text(&TINY, 10, y, "BOW.BMP      over the bow, for BOW (400 x 150 at most)", 0); y += LH;
  draw_text(&TINY, 10, y, "SHIP.BMP     from above, bow up, for the CHART (64 x 160)", 0);
  if (N->msg[0]) draw_text(&TINY, 2, MENU_Y - LH, N->msg, 1);
}

/* ------------------------------------------------------------------ */
/*  Config                                                            */
/* ------------------------------------------------------------------ */
void cfg_save(void) {
  if (!sys_disk_ok()) return;
  char *b = (char *)malloc(1024);
  if (!b) return;
  Cfg *c = &N->cfg;
  char *o = b;
  o += sprintf(o, "NAV47 CFG 1\nLAT %.9f\nLON %.9f\nTZ %.4f\nLIVE %d\nFIXED %.9f\nWIND %.1f %.1f\nTIDE %d %d %d\nTAUTO %d\nZONE %d %d\nSOUTH %d\nGC %d\nLT %d\n",
               c->lat, c->lon, c->tz, c->live, c->fixed_jd, c->wdir, c->wkn, c->tsrc, c->treg, c->tst, c->tauto, c->zone, c->city,
               c->south, c->gc, c->lt);
  o += sprintf(o, "DPWX %d %d %.1f %.2f %.1f %.1f %.1f\n", c->dcauto, c->dsauto, c->dcset, c->dckn, c->dsfrom, c->dhs, c->dtp);
  o += sprintf(o, "DUT1 %.3f\n", c->dut1);
  for (int i = 0; i < c->nfiles; i++) o += sprintf(o, "FILE %s\n", c->files[i]);
  gps_cfg_write(o);
  sys_disk_write_enable(1);
  check_create_dir(DIR);
  FIL f;
  UINT bw;
  if (f_open(&f, CFGFILE, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) { f_write(&f, b, (UINT)strlen(b), &bw); f_close(&f); }
  sys_disk_write_enable(0);
  free(b);
}

static void cfg_load(void) {
  Cfg *c = &N->cfg;
  c->lat = 26 + 30 / 60.0; c->lon = 50 + 10 / 60.0; c->tz = 3; c->live = 1; c->fixed_jd = jd_ymd(2026, 11, 2, 12);
  c->wdir = 0; c->wkn = 0; c->zone = -1; c->tauto = 1; c->dcauto = 1; c->dsauto = 1;
  char *t = read_text(CFGFILE);
  if (!t) return;
  char *l = strtok(t, "\n");
  c->nfiles = 0;
  while (l) {
    if (!strncmp(l, "LAT ", 4)) c->lat = atof(l + 4);
    else if (!strncmp(l, "LON ", 4)) c->lon = atof(l + 4);
    else if (!strncmp(l, "TZ ", 3)) c->tz = atof(l + 3);
    else if (!strncmp(l, "LIVE ", 5)) c->live = atoi(l + 5);
    else if (!strncmp(l, "FIXED ", 6)) c->fixed_jd = atof(l + 6);
    else if (!strncmp(l, "WIND ", 5)) sscanf(l + 5, "%lf %lf", &c->wdir, &c->wkn);
    else if (!strncmp(l, "TIDE ", 5)) sscanf(l + 5, "%d %d %d", &c->tsrc, &c->treg, &c->tst);
    else if (!strncmp(l, "TAUTO ", 6)) c->tauto = atoi(l + 6) != 0;
    else if (!strncmp(l, "ZONE ", 5)) sscanf(l + 5, "%d %d", &c->zone, &c->city);
    else if (!strncmp(l, "SOUTH ", 6)) c->south = atoi(l + 6);
    else if (!strncmp(l, "GC ", 3)) c->gc = atoi(l + 3);
    else if (!strncmp(l, "LT ", 3)) c->lt = atoi(l + 3) != 0;
    else if (!strncmp(l, "DUT1 ", 5)) { double v = atof(l + 5); if (fabs(v) <= 0.9) c->dut1 = v; }
    else if (!strncmp(l, "DPWX ", 5)) sscanf(l + 5, "%d %d %lf %lf %lf %lf %lf", &c->dcauto, &c->dsauto, &c->dcset, &c->dckn, &c->dsfrom, &c->dhs, &c->dtp);
    else if (!strncmp(l, "FILE ", 5) && c->nfiles < 4) { strncpy(c->files[c->nfiles], l + 5, 39); c->files[c->nfiles++][39] = 0; }
    else gps_cfg_line(l);
    l = strtok(NULL, "\n");
  }
  free(t);
}

/* ------------------------------------------------------------------ */
/*  INFO                                                              */
/* ------------------------------------------------------------------ */
/* INFO: paragraphs word-wrapped to the LCD in the tiny font, 8 2 scroll. A paragraph "HEAD:   text" wraps under
   its text (a hanging indent); "" is an empty line. */
static int info_top, info_lines;

/* one paragraph: the lines from *n on; those inside [top, top + vis) drawn from row y0 */
static void info_par(const char *p, int *n, int top, int vis, int y0) {
  char line[100];
  const char *c = strstr(p, ":   ");
  int xt = 2, w = LCD_X - 4 - 6;                       /* 6 px for the scroll bar */
  if (!*p) { (*n)++; return; }
  if (c && c - p < 12) {                               /* "HEAD:   text": the head, the text in its column */
    memcpy(line, p, (size_t)(c - p) + 1); line[c - p + 1] = 0;
    if (*n >= top && *n < top + vis) draw_text(&TINY, 2, y0 + (*n - top) * LH, line, 0);
    xt = 2 + text_width(&TINY, "ALMANAC: ");
    p = c + 1;
    while (*p == ' ') p++;
  }
  while (*p) {
    int len = 0, last = 0;
    while (p[len] && len < 98) {                         /* as many words as fit */
      memcpy(line, p, (size_t)len + 1); line[len + 1] = 0;
      if (xt + text_width(&TINY, line) > 2 + w) break;
      len++;
      if (p[len] == ' ' || !p[len]) last = len;
    }
    if (!p[len]) last = len;
    if (!last) last = len ? len : 1;                     /* one word longer than the line: cut it */
    memcpy(line, p, (size_t)last); line[last] = 0;
    if (*n >= top && *n < top + vis) draw_text(&TINY, xt, y0 + (*n - top) * LH, line, 0);
    (*n)++;
    p += last;
    while (*p == ' ') p++;
  }
}

static void draw_info(void) {
  header("INFO");
  char s1[96], s2[96], s3[64];
  sprintf(s1, "tzdata %s, free heap %d KB, tide stations: %d public regions, %d private, %d files loaded.",
          TZDATA_VERSION, sys_free_mem() / 1024, PUB_NREGIONS, PRIV_NREGIONS, N->nfr);
  sprintf(s2, "CPU %s.", usb_powered() ? "160 MHz (USB power)" : "80 MHz (battery)");
  sprintf(s3, "Version " PROGRAM_VERSION ".");
  const char *P[] = {
    "NAV47: Almanac 47 + Zones 47 + Tide 47 + passage planning for the DM42n, a native DMCP5 program.", s3,
    "(C) 2026 Victor Valdes, Master Mariner. GPL-3.0-or-later. Written with the help of AI (Claude).",
    "",
    "ALMANAC:   VSOP87D Sun and planets, IAU1980 nutation, Meeus Moon with a DE421 fit, the 57 navigational stars "
    "and Polaris (Almanac 47 NAVINIT_FULL series, double precision, no tables).",
    "VIEWS:   ALMAN SPLIT SKY ALLSK MOON ANIM, and SKY2: the dome, the zenith in the centre (SKY again, or 0).",
    "ZONES:   IANA tz database, summer time computed on the calculator. Map: GLOBE (NOAA).",
    "TIDES:   Schureman harmonic method (Tide 47). NOAA (public domain), TICON-3 (CC-BY 4.0). AUTO (0 on TIDE or T&F): "
    "the stations nearest the position (SET or HERE), every source; T&F: the atlas where it covers the position, else a "
    "stream station within 30 NM, else the tide. Choosing a station (ENTER, 8 2) ends AUTO.",
    "ROUTE:   RL rhumb line, GC great circle, CS composite (the limit with 4 6), on the WGS84 ellipsoid or another "
    "(COMPARE, 7 9). Geodesics: GeographicLib (C. F. F. Karney, MIT); rhumb lines exact on the ellipsoid.",
    "PLAN:   the route split every N NM, into N legs or on whole meridians; ENTER PLAN.CSV, shift ENTER "
    "PLAN.NMEA (and USB channel 2), R/S VOYAGE.",
    "CHART:   Mercator or GLOBE (F4) over the coastline, Natural Earth 110 m and 50 m (public domain), "
    "/NAV47/COAST_C.BIN and COAST_L.BIN. 8 2 4 6 pan or turn, 7 9 zoom, 5 fit; ROUTE 0 opens it.",
    "VOYAGE:   the plan sailed or flown on a globe that follows: the ship (from 100 kn a plane), the date and "
    "time on board, DEP ARR, run and to go. R/S play, 4 6 a step, 7 9 zoom, ENTER the speed (typed) or + - (steps), 1 3 the departure, 0 ETA.",
    "DP OPS:   (DP, menu 3) can she keep station off a platform? The platform (a square or a rectangle, its faces, its height) in /NAV47/DPOPS.TXT, made on the web page's DP tab; the vessel a DPCAP47 .DPV (VESSEL line, or /NAV47/VESSEL.DPV; else a made-up 95 m DP2 with three bow tunnels and two azimuths). DPCAP47's engine (DNV-ST-0111 level 1's forces, x 1.25; its thrust allocation). The picture: each thruster's force an arrow, the weather's whole push a thick arrow in tonnes (PUSH: x 1.25, where it pushes, OFF or ON the platform) with the wind, the current and the sea from their own directions; the platform's lee lightens the wind (taller than the vessel: 30 % of it left close in, all of it 5 heights downwind). ENTER (BEST) tries every heading within the tolerance of square to the face (every face with FACE AUTO), stern, starboard, port or bow to it, the hull STANDOFF m off; inside the working limits (LIM: the bow tunnels 30 %); then each redundancy group lost at full thrust; then the wind she holds to with its sea. 4 6 the heading by hand, 7 9 the wind's direction 10 degrees, 8 2 its speed 1 kn, UP DOWN an hour (the current follows), 5 now, 0 the bars and the arrows after the worst failure, 3 the bars in tonnes or %, 1 starboard or port side to: parallel to the face (PAR, the usual set-up) or within the tolerance. FACE, SIDE, INPUT (the wind as SET; the current FLOW's or typed; the sea DNV's or typed; the standoff, the tolerance), LOAD reads the files again. Static, planning only: not the vessel's DP consequence analysis.",
    "ETA TABLE:   (TABLE on ETA) the ETAs at the set speed and three slower ones, 0.5 kn apart: 8 2 the speed, 4 6 the step (0.25 0.5 1 kn), 5 the set speed, ENTER (USE) this line's speed for the voyage.",
    "ETA:   the plan's distance with ETD, ETA and speed: set two, the third follows (ETA typed: the speed it needs). 8 2 the field, ENTER types it (dates YYYY.MMDD, times HH.MM, in UT or LT), 0 or KEEP: a new ETD keeps the speed or the ETA, NOW: the ETD now. Saved in ROUTE.TXT (SPEED, ETD).",
    "FONTS:   C47 standardFont and tinyFont (C43 / C47 authors, GPL v3).",
    "",
    "DOES NOT REPLACE THE NAUTICAL ALMANAC, OFFICIAL CHARTS OR TIDE TABLES. Not for use as the primary "
    "means of navigation.",
    "",
    s1, s2,
  };
  const int y0 = 17, vis = (174 - y0) / LH;
  int n = 0;
  for (unsigned i = 0; i < sizeof P / sizeof *P; i++) info_par(P[i], &n, info_top, vis, y0);
  info_lines = n;
  if (info_top > n - vis) info_top = n - vis > 0 ? n - vis : 0;
  if (n > vis) {                                         /* the scroll bar */
    int h = vis * LH, bh = h * vis / n, by = y0 + (h - bh) * info_top / (n - vis);
    gfx_rect(LCD_X - 4, y0, 1, h, 1);
    gfx_rect(LCD_X - 6, by, 5, bh < 6 ? 6 : bh, 1);
  }
  char s[64], u[64];
  usbview_state(s, u, 64);
  nt_inv(4, 176, s);
  nt(4, 192, u);
  if (n > vis) { sprintf(s, "8 2 %d/%d", info_top + vis < n ? info_top + vis : n, n); nt_r(LCD_X - 2, 192, s); }
}


/* ------------------------------------------------------------------ */
/*  Menus, keys, main loop                                            */
/* ------------------------------------------------------------------ */
static const char *MENUS[6][6] = {
  { "ALMAN", "SPLIT", "SKY", "ALLSK", "MOON", ">>" },
  { "ANIM", "T&F", "TIDE", "ROUTE", "ZONES", ">>" },
  { "SET", "FILES", "INFO", "DP", "USB", ">>" },
  { "ADD", "EDIT", "DEL", "RL/GC", "HERE", ">>" },
  { "FIX", "SATS", "NAV", "BOW", "GPSSET", ">>" },
  { "WPTS", "LOAD", "SAVE", "NEW", "", ">>" },          /* your waypoints, the routes' library (/NAV47/ROUTES) */
};
static const char *FILE_MENU[6] = { "TID", "FLW", "CLEAR", "WPTS", "LOAD", "BACK" };
static int wpdb_back = PG_ROUTE;
static const char *EDIT_MENU[6] = { "", "", "", "", "", "CANCEL" };
static const char *PLAN_MENU[6] = { "CSV", "NMEA", "SPLIT", "CMP", "CHART", "BACK" };
static const char *CMP_MENU[6] = { "ELL-", "ELL+", "EDIT", "PLAN", "CHART", "BACK" };
static const char *CHART_MENU[6] = { "ZOOM+", "ZOOM-", "FIT", "GLOBE", "PLAN", "BACK" };
static const char *VOY_MENU[6] = { "PLAY", "SLOWER", "FASTER", "ETA", "CHART", "BACK" };
static const char *ETA_MENU[6] = { "NOW", "TABLE", "", "KEEP", "VOYAGE", "BACK" };
static const char *ETAT_MENU[6] = { "SPEED+", "SPEED-", "SET", "USE", "ETA", "BACK" };
static const char *DP_MENU[6] = { "BEST", "FACE", "SIDE", "INPUT", "LOAD", "BACK" };
static const char *DPIN_MENU[6] = { "", "", "", "", "", "BACK" };
static const char *ABS_MENU[6] = { "DNV", "CUR", "MARGIN", "", "", "BACK" };

#define ANIM_FRAMES 24
#define ANIM_STEP   0.5

/* the views' almanac at the current time (computed again only when the time or the position changes) */
static void view_almanac_now(void) {
  double j = ut_now();
  if (!N->valid || N->vjd != j || N->al.lat != N->cfg.lat || N->al.lon != N->cfg.lon || N->al.tz != show_tz() || view_dut1 != N->cfg.dut1) {
    view_dut1 = N->cfg.dut1;                              /* SET DUT1: the almanac at UT1 */
    al_compute(&N->al, j, N->cfg.lat, N->cfg.lon, show_tz());
    N->vjd = j;
    N->valid = 1;
  }
  N->al.local = N->cfg.lt;
}

/* ------------------------------------------------------------------ */
/*  What the GPS pages borrow (navctx.h)                              */
/* ------------------------------------------------------------------ */
double nav_ut(void) { return ut_now(); }
double nav_tz_shown(void) { return show_tz(); }
void nav_header(const char *name) { header(name); }
void nav_pos(double *lat, double *lon) { *lat = N->cfg.lat; *lon = N->cfg.lon; }
void nav_follow(double lat, double lon) {
  if (fabs(lat - N->cfg.lat) < 1e-7 && fabs(lon - N->cfg.lon) < 1e-7) return;
  N->cfg.lat = lat; N->cfg.lon = lon; N->valid = 0;
}
const route_t *nav_route(double *plan_kn) {
  *plan_kn = voy_kn;
  if (N->nwp < 2 || !geo_route_alloc()) return NULL;
  if (plan_dirty || legs_dirty) plan_build();
  return geo_route.n >= 2 ? &geo_route : NULL;
}
const struct geod_geodesic *nav_ell(void) { return &geo_ell; }
int nav_bodies(VBody *b) {                    /* the almanac again only a minute later, or 3' away */
  static VBody c[8];
  static int n;
  static double cj, cla, clo;
  double j = ut_now();
  if (!n || fabs(j - cj) > 1.0 / 1440 || fabs(cla - N->cfg.lat) > 0.05 || fabs(clo - N->cfg.lon) > 0.05) {
    view_almanac_now();
    n = vbodies(&N->al, c);
    cj = j; cla = N->cfg.lat; clo = N->cfg.lon;
  }
  memcpy(b, c, sizeof c);
  return n;
}
void nav_beep(void) {
#ifndef NAV47_SIM
  if (is_beep_mute()) return;
#endif
  start_buzzer_freq(2400000);
  sys_delay(120);
  stop_buzzer();
}
const char *nav_wp_name(int i) { return i >= 0 && i < N->nwp ? N->wname[i] : ""; }
int nav_here(double *lat, double *lon) {
  double h;
  if (gps_live(lat, lon, &h)) return 1;
  *lat = N->cfg.lat; *lon = N->cfg.lon;
  return 0;
}
void nav_route_insert(double lat, double lon, const char *name) {
  if (N->nwp >= MAXWP) return;
  route_insert(lat, lon);
  snprintf(N->wname[wp_cur], WP_NAME, "%s", name ? name : "");
  route_save();
}
void nav_chart_live(int back) {
  chart_back = PG_GFIX + back; chart_refit = 1; chart_live = 1; chart_follow = 1;
  go(PG_CHART);
}

/* the source in the top bar is not the one shown (another page than the GPS's) */
static int bar_tag_changed(void) {
  double a, b;
  const char *t;
  const char *want = gps_bar(&a, &b, &t) ? (gps_follows() ? t : "SET") : NULL;
  if (!want || !top_tag) return want != top_tag;
  if (strcmp(want, top_tag)) return 1;
  return bar_shown_lat != 99 && (N->cfg.lat != bar_shown_lat || N->cfg.lon != bar_shown_lon);   /* SET followed the GPS */
}

static void redraw(void) {
  lcd_clear_buf();
  { /* the top bar's position: the GPS pages and the GPS's chart show the vessel's (GPS or DR), the others SET's;
       the tag says which: GPS / DGPS / DR, or SET while the GPS runs but SET does not follow it */
    const char *t;
    int live = gps_bar(&bar_lat, &bar_lon, &t);
    bar_live = live && (IS_GPS(page) || (page == PG_CHART && chart_live));
    top_tag = !live ? NULL : bar_live || gps_follows() ? t : "SET";
  }
  bar_shown_lat = N->cfg.lat; bar_shown_lon = N->cfg.lon;   /* what SET's pages show from now */
  if (IS_VIEW(page)) {
    view_almanac_now();
    switch (page) {
    case PG_VALM: view_almanac(&N->al); break;
    case PG_VSPLIT: view_split(&N->al, &split_first); break;
    case PG_VSKY: view_sky(&N->al, vframe); break;
    case PG_VALLSKY: view_allsky(&N->al); break;
    case PG_VMOON: view_moon(&N->al, N->cfg.south); break;
    case PG_VANIM: view_anim(&N->al, ANIM_FRAMES, ANIM_STEP, vframe < ANIM_FRAMES ? vframe : ANIM_FRAMES - 1); break;
    case PG_VSKY2: view_sky2(&N->al); break;
    }
  } else
    switch (page) {
    case PG_TIDE: draw_tide(); break;
    case PG_STATION: draw_station(); break;
    case PG_ROUTE: draw_route(); break;
    case PG_PLAN: draw_plan(); break;
    case PG_CMP: draw_cmp(); break;
    case PG_CHART: draw_chart(); break;
    case PG_VOY: draw_voy(); break;
    case PG_ETA: draw_eta(); break;
    case PG_ETAT: draw_etat(); break;
    case PG_DP: draw_dp(); break;
    case PG_DPABS: draw_dpabs(); break;
    case PG_DPIN: draw_dpin(); break;
    case PG_ZONES: draw_zones(); break;
    case PG_SET: draw_set(); break;
    case PG_FILES: draw_files(); break;
    case PG_INFO: draw_info(); break;
    case PG_PICK: draw_pick(); break;
    case PG_GFIX: case PG_GSATS: case PG_GNAV: case PG_GBOW: case PG_GSET: gps_draw(page - PG_GFIX, anim_frame); break;
    case PG_WPDB: wpdb_draw(); break;
    }
  if (page != PG_ROUTE && menu_page == 3) menu_page = 0;
  CHART_MENU[4] = chart_live ? "NAV" : "PLAN";
  MENUS[2][4] = usbview_is_on() ? "USB*" : "USB";
  MENUS[0][2] = page == PG_VSKY ? "SKY2" : "SKY";
  CHART_MENU[3] = chart_globe ? "MERC" : "GLOBE";
  VOY_MENU[0] = voy_play ? "PAUSE" : "PLAY";
  lcd_draw_menu_keys(page == PG_ROUTE && rlib_active() ? (const char **)rlib_menu() : page == PG_WPDB ? (const char **)wpdb_menu() : page == PG_FILES ? FILE_MENU : (edit_field >= 0 || wp_edit >= 0 || cmp_edit >= 0 || voy_edit || eta_edit >= 0 || (page == PG_DPIN && dp_editing()) || (IS_GPS(page) && gps_editing())) ? EDIT_MENU :
                     page == PG_PLAN ? PLAN_MENU : page == PG_CMP ? CMP_MENU : page == PG_CHART ? CHART_MENU : page == PG_VOY ? VOY_MENU : page == PG_ETA ? ETA_MENU : page == PG_ETAT ? ETAT_MENU : page == PG_DP ? DP_MENU : page == PG_DPIN ? DPIN_MENU : page == PG_DPABS ? ABS_MENU : MENUS[menu_page]);
  lcd_refresh();
  lcd_refresh_wait();        /* the LCD goes out by DMA: done before the next frame clears the buffer (no flicker) */
  usbview_send();
}

int digit(int key) {
  static const int dk[10] = { KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9 };
  for (int i = 0; i < 10; i++) if (key == dk[i]) return i;
  return -1;
}

static void key_edit(int key) {
  int d = digit(key);
  if (key == KEY_ENTER) { field_commit(edit_field); edit_field = -1; cfg_save(); return; }
  if (key == KEY_EXIT) { edit_field = -1; return; }
  if (key == KEY_BSP) { if (elen) ebuf[--elen] = 0; return; }
  if (key == KEY_CHS) { if (ebuf[0] == '-') { memmove(ebuf, ebuf + 1, (size_t)elen); elen--; } else if (elen < 14) { memmove(ebuf + 1, ebuf, (size_t)elen + 1); ebuf[0] = '-'; elen++; } return; }
  if (key == KEY_DOT && elen < 14 && !strchr(ebuf, '.')) { ebuf[elen++] = '.'; ebuf[elen] = 0; return; }
  if (d >= 0 && elen < 14) { ebuf[elen++] = (char)('0' + d); ebuf[elen] = 0; }
}

void go(int p) { page = p; vframe = 0; }

static void softkey(int fn) {
  if (page == PG_ROUTE && rlib_active()) { rlib_key(KEY_F1 + fn); return; }   /* the library asks: its name's letters, YES / NO */
  if (page == PG_FILES) {
    if (fn == 0 || fn == 1) {
      check_create_dir(TIDEDIR);
      file_selection_screen(fn ? "Load FLOW atlas" : "Load tide stations", TIDEDIR, fn ? ".flw" : ".tid",
                            file_sel_cb, 0, 0, NULL);
      wait_for_key_release(-1);
      cfg_save();
    } else if (fn == 2) {
      N->cfg.nfiles = 0; N->nfr = 0; N->have_ffl = 0;
      if (N->cfg.tsrc == 2) N->cfg.tsrc = 0;
      strcpy(N->msg, "files cleared (memory is freed at the next start)");
      cfg_save();
    } else if (fn == 3) { wpdb_back = PG_FILES; wpdb_open(0); go(PG_WPDB); }
    else if (fn == 4) {                                  /* LOAD: your files again: objects, pictures */
      char pm[40];
      int k = obj_load();
      gps_pics_load(pm, sizeof pm);
      snprintf(N->msg, sizeof N->msg, "OBJECTS %d  %s", k < 0 ? 0 : k, pm);
    }
    else if (fn == 5) go(PG_SET);
    return;
  }
  if (page == PG_PLAN) {
    static const int k[6] = { KEY_ENTER, 0, KEY_5, 0, 0, KEY_EXIT };
    if (fn == 1) { plan_msg[0] = 0; plan_nmea(); }
    else if (fn == 3) go(PG_CMP);
    else if (fn == 4) chart_open(PG_PLAN);
    else plan_key(k[fn]);
    return;
  }
  if (page == PG_CMP) {
    static const int k[6] = { KEY_7, KEY_9, KEY_ENTER, 0, 0, KEY_EXIT };
    if (fn == 2 && geo_ell_index != ELL_CUSTOM) strcpy(cmp_msg, "EDIT: CUSTOM only (ELL+)");
    else if (fn == 3) { plan_cur = 0; go(PG_PLAN); }
    else if (fn == 4) chart_open(PG_CMP);
    else cmp_key(k[fn]);
    return;
  }
  if (page == PG_DP) { dp_soft(fn); return; }
  if (page == PG_DPABS) {
    static const int k[6] = { KEY_DOT, KEY_7, KEY_8, 0, 0, KEY_EXIT };
    if (k[fn]) dpabs_key(k[fn]);
    return;
  }
  if (page == PG_DPIN) { if (fn == 5) dpin_key(KEY_EXIT); return; }
  if (page == PG_ETAT) {
    static const int k[6] = { KEY_8, KEY_2, KEY_5, KEY_ENTER, KEY_EXIT, KEY_EXIT };
    if (fn == 5) { go(PG_VOY); voy_etd = etd_now(); } else etat_key(k[fn]);
    return;
  }
  if (page == PG_ETA) {
    if (fn == 1) { etat_base = voy_kn; etat_sel = 0; go(PG_ETAT); return; }
    if (fn == 0) { voy_etd0 = 0; route_save(); }
    else if (fn == 3) eta_keep_eta ^= 1;
    else if (fn == 4 || fn == 5) { go(PG_VOY); voy_etd = etd_now(); }
    return;
  }
  if (page == PG_VOY) {
    static const int k[6] = { KEY_RUN, KEY_SUB, KEY_ADD, KEY_0, 0, KEY_EXIT };
    if (fn == 4) { voy_play = 0; chart_open(PG_PLAN); }
    else voy_key(k[fn]);
    return;
  }
  if (page == PG_CHART) {
    static const int k[6] = { KEY_9, KEY_7, KEY_5, KEY_0, 0, KEY_EXIT };
    if (fn == 4 && chart_live) { chart_live = 0; go(chart_back); menu_page = 4; }
    else if (fn == 4) { plan_cur = 0; go(PG_PLAN); }
    else chart_key(k[fn]);
    return;
  }
  if (fn == 5) {                                       /* >> */
    /* the rows: 0 1 2, the GPS's (4), then ROUTE's own (3) on ROUTE */
    menu_page = menu_page == 3 ? 0 : menu_page == 2 ? 4 : menu_page == 4 ? 5 : menu_page == 5 ? (page == PG_ROUTE ? 3 : 0) : menu_page + 1;
    return;
  }
  switch (menu_page * 6 + fn) {
  case 0: go(PG_VALM); break;
  case 1: go(PG_VSPLIT); break;
  case 2: go(page == PG_VSKY ? PG_VSKY2 : PG_VSKY); break;         /* SKY again: SKY2, the dome */
  case 3: go(PG_VALLSKY); break;
  case 4: go(PG_VMOON); break;
  case 6: go(PG_VANIM); break;
  case 7: go(PG_TIDE); flow_hw_step = flow_day_step = 0; break;      /* T&F: tide and flow, the nearest station */
  case 8: go(PG_STATION); st_day_off = 0; break;                    /* TIDE: the chosen station */
  case 9: go(PG_ROUTE); menu_page = 3; break;
  case 10: go(PG_ZONES); break;
  case 12: go(PG_SET); sel_field = 0; break;
  case 13: go(PG_FILES); N->msg[0] = 0; break;
  case 14: go(PG_INFO); break;
  case 15: go(PG_DP); dp_enter(); break;                              /* DP OPS */
  case 16: if (usbview_is_on()) usbview_off(); else usbview_on(); go(PG_INFO); break;   /* INFO shows the state */
  case 18: route_insert(N->nwp ? N->wlat[wp_cur] : N->cfg.lat, N->nwp ? N->wlon[wp_cur] : N->cfg.lon); wp_start_edit(); wp_new = 1; break;
  case 19: wp_new = 0; wp_start_edit(); break;
  case 20: route_delete(); route_save(); break;
  case 21: N->cfg.gc = (N->cfg.gc + 1) % 3; route_save(); break;
  case 22: route_insert(N->cfg.lat, N->cfg.lon); route_save(); break;
  case 24: case 25: case 26: case 27: case 28: go(PG_GFIX + fn); menu_page = 4; break;   /* FIX SATS NAV BOW GPSSET */
  case 30: wpdb_back = page == PG_WPDB ? PG_ROUTE : page; wpdb_open(0); go(PG_WPDB); break;   /* WPTS */
  case 31: rlib_load(); wait_for_key_release(-1); go(PG_ROUTE); menu_page = 5; break;   /* LOAD a route */
  case 32: go(PG_ROUTE); menu_page = 5; rlib_save_start(); break;               /* SAVE it by name */
  case 33: go(PG_ROUTE); menu_page = 5; rlib_new_start(); break;                /* NEW: an empty route */
  }
}

void step_time(double hours) { toff += hours; }

static void handle_key(int key) {
  if (page == PG_ROUTE && rlib_active() && key != KEY_SHIFT) { rlib_key(key); return; }
  if (key == KEY_SHIFT) { shift = !shift; return; }
  if (shift) {
    shift = 0;
    if (key == KEY_EXIT) { cfg_save(); SET_ST(STAT_PGM_END); return; }
    if (key == KEY_ENTER && page == PG_PLAN) { plan_msg[0] = 0; plan_nmea(); return; }
    if (key == KEY_F4 && page == PG_ROUTE && menu_page == 3) { leg_sail_cycle(wp_cur); return; }   /* SHIFT RL/GC: the cursor's leg */
    if (key == KEY_0) {
      static const uint8_t mid[] = { MI_SET_TIME, MI_SET_DATE, MI_BEEP_MUTE, MI_SLOW_AUTOREP, MI_MSC, MI_SYSTEM_ENTER, 0 };
      static const smenu_t MID = { "NAV47 Setup", mid, NULL, NULL };
      SET_ST(STAT_MENU);
      handle_menu(&MID, MENU_RESET, 0);
      CLR_ST(STAT_MENU);
      wait_for_key_release(-1);
      return;
    }
  }
  if (IS_GPS(page) && gps_editing()) { gps_key(page - PG_GFIX, key); return; }
  if (page == PG_WPDB) {
    int r = wpdb_key(key);
    if (r == 2) { go(wpdb_back); if (wpdb_back == PG_ROUTE) menu_page = 3; }
    return;
  }
  if ((edit_field >= 0 || wp_edit >= 0 || cmp_edit >= 0 || voy_edit || eta_edit >= 0) && key == KEY_F6) key = KEY_EXIT;
  if (edit_field >= 0) { key_edit(key); return; }
  if (wp_edit >= 0) { key_wp(key); return; }
  if (cmp_edit >= 0) { cmp_edit_key(key); return; }
  if (voy_edit) { voy_edit_key(key); return; }
  if (eta_edit >= 0) { eta_edit_key(key); return; }
  if (key >= KEY_F1 && key <= KEY_F6) { softkey(key - KEY_F1); vframe = 0; return; }
  if (key == KEY_CHS) { N->cfg.lt ^= 1; N->valid = 0; cfg_save(); return; }   /* +/-: the times in UT or LT, every page */
  if (IS_VIEW(page)) {
    /* as Almanac 47: up / down one hour later / earlier; 4 6 ten minutes, 7 9 a day, 5 now */
    if (key == KEY_UP) step_time(1);
    else if (key == KEY_DOWN) step_time(-1);
    else if (key == KEY_4) step_time(-1.0 / 6);
    else if (key == KEY_6) step_time(1.0 / 6);
    else if (key == KEY_7) step_time(-24);
    else if (key == KEY_9) step_time(24);
    else if (key == KEY_5) toff = 0;
    else if (key == KEY_0 && page == PG_VMOON) { N->cfg.south ^= 1; cfg_save(); }
    else if (key == KEY_0 && (page == PG_VSKY || page == PG_VALLSKY)) go(PG_VSKY2);
    else if (key == KEY_0 && page == PG_VSKY2) go(PG_VSKY);
    else if (key == KEY_8 && page == PG_VSPLIT) split_first--;
    else if (key == KEY_2 && page == PG_VSPLIT) split_first++;
    else if (key == KEY_EXIT) go(PG_VALM);
    vframe = 0;
    return;
  }
  if (IS_GPS(page)) {
    if (gps_key(page - PG_GFIX, key)) return;
    if (key == KEY_0 && page == PG_GFIX) { go(PG_GSET); return; }
    if (key == KEY_EXIT) { go(PG_VALM); menu_page = 0; }
    return;
  }
  switch (page) {
  case PG_ZONES:
    if (key == KEY_4) { N->cfg.zone = (N->cfg.zone + NZONES - 1) % NZONES; N->cfg.city = 0; }
    else if (key == KEY_6) { N->cfg.zone = (N->cfg.zone + 1) % NZONES; N->cfg.city = 0; }
    else if (key == KEY_8 || key == KEY_UP) N->cfg.city = (N->cfg.city + ZONES[N->cfg.zone].n - 1) % ZONES[N->cfg.zone].n;
    else if (key == KEY_2 || key == KEY_DOWN) N->cfg.city = (N->cfg.city + 1) % ZONES[N->cfg.zone].n;
    else if (key == KEY_5) { N->cfg.zone = zone_nearest(N->cfg.tz, ut_now()); N->cfg.city = 0; }
    break;
  case PG_TIDE:
    if (key == KEY_4) flow_hw_step--;
    else if (key == KEY_6) flow_hw_step++;
    else if (key == KEY_1) { flow_day_step--; flow_hw_step = 0; }
    else if (key == KEY_3) { flow_day_step++; flow_hw_step = 0; }
    else if (key == KEY_5) flow_hw_step = flow_day_step = 0;
    else if (key == KEY_ENTER) pick_open();
    else if (key == KEY_0) { N->cfg.tauto ^= 1; cfg_save(); }
    break;
  case PG_STATION:
    if (key == KEY_8 || key == KEY_UP) station_step(-1);
    else if (key == KEY_2 || key == KEY_DOWN) station_step(1);
    else if (key == KEY_4) st_day_off--;
    else if (key == KEY_6) st_day_off++;
    else if (key == KEY_5) st_day_off = 0;
    else if (key == KEY_ENTER) pick_open();
    else if (key == KEY_0) N->cfg.tauto ^= 1;
    if (key != KEY_EXIT) cfg_save();
    break;
  case PG_INFO: {                                       /* 8 2 scroll, 7 9 a page */
    int vis = (174 - 17) / LH, mx = info_lines - vis > 0 ? info_lines - vis : 0;
    if (key == KEY_8 || key == KEY_UP) info_top--;
    else if (key == KEY_2 || key == KEY_DOWN) info_top++;
    else if (key == KEY_7) info_top -= vis - 1;
    else if (key == KEY_9) info_top += vis - 1;
    if (info_top > mx) info_top = mx;
    if (info_top < 0) info_top = 0;
    return;
  }
  case PG_PICK:
    if (key == KEY_8 || key == KEY_UP) pick_move(-1);
    else if (key == KEY_2 || key == KEY_DOWN) pick_move(1);
    else if (key == KEY_ENTER || key == KEY_5) { pick_choose(); cfg_save(); }
    else if (key == KEY_EXIT) go(pick_back == PG_TIDE ? PG_TIDE : PG_STATION);
    return;
  case PG_ROUTE:
    if ((key == KEY_8 || key == KEY_UP) && wp_cur > 0) wp_cur--;
    else if ((key == KEY_2 || key == KEY_DOWN) && wp_cur < N->nwp - 1) wp_cur++;
    else if (key == KEY_ENTER) wp_start_edit();
    else if (key == KEY_RUN && N->nwp > 1) { plan_cur = 0; go(PG_PLAN); }
    else if (key == KEY_5 && N->nwp > 1) go(PG_CMP);
    else if (key == KEY_0) chart_open(PG_ROUTE);
    else if (key == KEY_1) { wpdb_back = PG_ROUTE; wpdb_open(1); go(PG_WPDB); }      /* WPTS: a waypoint from the list */
    else if (key == KEY_3 && N->nwp) {                                                /* the cursor's waypoint into the list */
      wpdb_add(N->wname[wp_cur], N->wlat[wp_cur], N->wlon[wp_cur]);
    }
    else if (key == KEY_4 && N->cfg.gc == 2 && cs_lim > 10) { cs_lim -= 1; route_save(); }
    else if (key == KEY_6 && N->cfg.gc == 2 && cs_lim < 85) { cs_lim += 1; route_save(); }
    break;
  case PG_PLAN:
    plan_key(key);
    return;
  case PG_CMP:
    cmp_key(key);
    return;
  case PG_CHART:
    if (key == KEY_RUN && N->nwp > 1 && !chart_live) { voy_open(PG_CHART); return; }
    chart_key(key);
    return;
  case PG_VOY:
    voy_key(key);
    return;
  case PG_ETA:
    if (key == KEY_DIV || key == KEY_MUL) { etat_base = voy_kn; etat_sel = 0; go(PG_ETAT); return; }
    eta_key(key);
    return;
  case PG_ETAT:
    etat_key(key);
    return;
  case PG_DP:
    dp_key(key);
    return;
  case PG_DPABS:
    dpabs_key(key);
    return;
  case PG_DPIN:
    dpin_key(key);
    return;
  case PG_SET:
    if (key == KEY_8 || key == KEY_UP) { do sel_field = (sel_field + F_COUNT - 1) % F_COUNT; while (N->cfg.live && (sel_field == F_DATE || sel_field == F_TIME)); }
    else if (key == KEY_2 || key == KEY_DOWN) { do sel_field = (sel_field + 1) % F_COUNT; while (N->cfg.live && (sel_field == F_DATE || sel_field == F_TIME)); }
    else if (key == KEY_ENTER) {
      if (sel_field == F_LT) { N->cfg.lt ^= 1; N->valid = 0; cfg_save(); }
      else if (sel_field == F_LIVE) {
        if (N->cfg.live) N->cfg.fixed_jd = ut_now();
        N->cfg.live ^= 1;
        toff = 0;
        N->valid = 0;
        cfg_save();
      } else { edit_field = sel_field; elen = 0; ebuf[0] = 0; }
    }
    break;
  }
  if (key == KEY_EXIT) go(page == PG_FILES ? PG_SET : PG_VALM);
}

static void *nav_alloc(size_t n) { return calloc(1, n); }
void _exit(int code) { (void)code; sys_reset(); for (;;) {} }

void program_main(void) {
  geo_set_ellipsoid(ELL_WGS84);   /* geo47 */
  run_menu_item_app = NULL;
  menu_line_str_app = NULL;
  switch_usb_powered_freq();
  N = (Nav *)nav_alloc(sizeof(Nav));
  if (!N || !c47_init()) { lcd_clear_buf(); draw_text(&TINY, 10, 100, "NAV47: not enough memory", 0); lcd_refresh(); for (;;) wait_for_key_press(); }
  nt_init();
  gps_init();                     /* before the CFG: its GPS lines */
  cfg_load();
  route_load();
  obj_load();                     /* /NAV47/OBJECTS.CSV: the platforms and their zones */
  wpdb_init();                    /* /NAV47/WAYPTS.CSV: the waypoints' list */
  gps_start();
  if (N->cfg.zone < 0 || N->cfg.zone >= NZONES) N->cfg.zone = zone_nearest(N->cfg.tz, ut_now());
  int nf = N->cfg.nfiles;
  N->cfg.nfiles = 0;
  for (int i = 0; i < nf; i++) {
    char p[40];
    strcpy(p, N->cfg.files[i]);
    if (load_file(p)) { strcpy(N->cfg.files[N->cfg.nfiles], p); N->cfg.nfiles++; }
  }
  redraw();
  uint32_t last = sys_current_ms();
  for (;;) {
    /* the wait: 250 ms for the movies, 200 ms while the GPS reads (channel 2 holds three sentences), else a second */
    int live_chart = page == PG_CHART && chart_live;
    int anim = page == PG_VANIM || (page == PG_VOY && voy_play) || (IS_GPS(page) && gps_anim(page - PG_GFIX)) || live_chart;
    uint32_t wait = gpsin_is_on() ? 200 : anim ? 250 : 1000;
    if (usb_any_on() && !ST(STAT_PGM_END) && key_empty())
      usbser_idle(wait);                                 /* the USB runs: no sleep (it would stop the USB) */
    else if ((ST(STAT_PGM_END) && ST(STAT_SUSPENDED)) || (!ST(STAT_PGM_END) && key_empty())) {
      sys_timer_start(0, wait);
      CLR_ST(STAT_RUNNING);
      sys_sleep();
    }
    if (ST(STAT_PGM_END) || ST(STAT_SUSPENDED)) {
      if (!ST(STAT_SUSPENDED)) {
        cfg_save();
        usbview_off();
        gpsin_off();
        lcd_set_buf_cleared(0);
        draw_power_off_image(1);
        LCD_power_off(0);
        SET_ST(STAT_SUSPENDED);
        SET_ST(STAT_OFF);
      }
      continue;
    }
    SET_ST(STAT_RUNNING);
    CLR_ST(STAT_SUSPENDED);
    if (ST(STAT_OFF)) {
      LCD_power_on();
      rtc_wakeup_delay();
      CLR_ST(STAT_OFF);
      gps_start();                                     /* the GPS input on again, if it was */
      redraw();
    }
    if (ST(STAT_POWER_CHANGE)) { switch_usb_powered_freq(); CLR_ST(STAT_POWER_CHANGE); }
    /* SKY: the name loop, a name a second. ANIM: a frame every half second, the last held 3 s, then again.
       Live pages: the zone clock every second, the others every 30 s (a new time). */
    uint32_t t = sys_current_ms();
    int gnew = gps_poll(t);                              /* the NMEA in, a step a second */
    if (gnew && gpsin_is_on()) reset_auto_off();         /* a navigation display: on while the data comes */
    static uint32_t last_anim;
    if (key_empty()) {
      if ((IS_GPS(page) || live_chart) && (gnew || (anim && t - last_anim >= 250))) {
        if (anim && t - last_anim >= 250) { anim_frame++; last_anim = t; }
        last = t; redraw();
      }
      else if (gnew && bar_tag_changed()) { last = t; redraw(); }   /* the top bar's source: GPS, DR, SET */
      else if (page == PG_VSKY && t - last >= 1000) { last = t; vframe++; redraw(); }
      else if (page == PG_VOY && voy_play && t - last >= 250) { last = t; voy_tick(); redraw(); }
      else if (page == PG_VANIM && t - last >= (vframe >= ANIM_FRAMES - 1 ? 3000u : 500u)) {
        last = t;
        vframe = vframe >= ANIM_FRAMES - 1 ? 0 : vframe + 1;
        redraw();
      } else if (N->cfg.live && ((page == PG_ZONES && t - last >= 1000) || t - last >= 30000)) {
        last = t;
        if (page != PG_VANIM && page != PG_CHART && page != PG_VOY) redraw();
      }
    }
    if (!key_empty()) reset_auto_off();
    int key = key_pop();
    if (key == USBVIEW_KEY_REDRAW) {          /* DMCP's serial console: the PC sent L, it wants the whole screen */
      redraw();
      usbview_redraw_request();
    } else if (key > 0) {
      handle_key(key);
      redraw();
      last = sys_current_ms();
    }
  }
}
