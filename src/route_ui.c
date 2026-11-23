/* route_ui.c - NAV47: the passage planning pages: ROUTE, PLAN, COMPARE, CHART (map47), VOYAGE, ETA, ETA TABLE.
 * Split from main.c; the shared state and the calls between the files are in app.h.
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#include "app.h"

/* ------------------------------------------------------------------ */
/*  ROUTE: passage planning                                           */
/* ------------------------------------------------------------------ */
/* geo47 PATCH: sailing RL 0 / GC 1 / CS 2, composite limit, PLAN split, leg cache */
static const char *SAIL_NAME[3] = { "RL", "GC", "CS" };
double cs_lim = 60.0;                 /* CS limiting latitude, deg N/S */
static int plan_split = SPLIT_DLON;          /* 0 none, 1 every NM, 2 N legs, 3 meridians */
static double plan_val = 10.0;
int legs_dirty = 1, legs_mode = -1, plan_dirty = 1, plan_cur, plan_rc;
char plan_msg[48];

/* a CUSTOM ellipsoid NAV47 accepts: an Earth-sized a, and a sphere or f < 0.01 */
static int ell_ok(double a, double rf) { return a >= 6.0e6 && a <= 7.0e6 && (rf == 0 || rf > 100); }

void route_save(void) {
  legs_dirty = plan_dirty = 1;
  if (!sys_disk_ok()) return;
  char *b = (char *)malloc(128 + 48 * MAXWP);
  if (!b) return;
  char *o = b;
  o += sprintf(o, "NAV47 ROUTE 1\n%s\nLIM %.1f\nSPLIT %d %.3f\nELLC %.4f %.9f\nELL %d\nSPEED %.4f\nETD %.6f\n", SAIL_NAME[N->cfg.gc % 3], cs_lim,
               plan_split, plan_val, ellipsoids[ELL_CUSTOM].a, ellipsoids[ELL_CUSTOM].rf, geo_ell_index, voy_kn, voy_etd0);
  for (int i = 0; i < N->nwp; i++) o += sprintf(o, N->wname[i][0] ? "%.7f %.7f %s\n" : "%.7f %.7f\n", N->wlat[i], N->wlon[i], N->wname[i]);
  sys_disk_write_enable(1);
  check_create_dir(DIR);
  FIL f;
  UINT bw;
  if (f_open(&f, ROUTEFILE, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) { f_write(&f, b, (UINT)strlen(b), &bw); f_close(&f); }
  sys_disk_write_enable(0);
  free(b);
}

void route_load(void) {
  char *t = read_text(ROUTEFILE);
  if (!t) return;
  char *l = strtok(t, "\n");
  N->nwp = 0;
  while (l) {
    double a, b, v;
    int m;
    size_t ln = strlen(l);
    while (ln && (l[ln - 1] == '\r' || l[ln - 1] == ' ' || l[ln - 1] == '\t')) l[--ln] = 0;   /* CR LF files too */
    if (!strcmp(l, "GC")) N->cfg.gc = 1;
    else if (!strcmp(l, "RL")) N->cfg.gc = 0;
    else if (!strcmp(l, "CS")) N->cfg.gc = 2;
    else if (sscanf(l, "LIM %lf", &v) == 1) { if (v >= 10 && v <= 85) cs_lim = v; }
    else if (sscanf(l, "SPLIT %d %lf", &m, &v) == 2) { if (m >= 0 && m <= 3) { plan_split = m; plan_val = v; } }
    else if (sscanf(l, "ELLC %lf %lf", &a, &b) == 2) {     /* CUSTOM's a and 1/f */
      if (ell_ok(a, b)) { ellipsoids[ELL_CUSTOM].a = a; ellipsoids[ELL_CUSTOM].rf = b; }
      if (geo_ell_index == ELL_CUSTOM) geo_set_ellipsoid(ELL_CUSTOM);
    }
    else if (sscanf(l, "ELL %d", &m) == 1) geo_set_ellipsoid(m);
    else if (sscanf(l, "SPEED %lf", &v) == 1) { if (v >= 1 && v <= 1000) voy_kn = v; }
    else if (sscanf(l, "ETD %lf", &v) == 1) { voy_etd0 = v > 2400000 ? v : 0; }
    else if (sscanf(l, "%lf %lf%n", &a, &b, &m) == 2 && N->nwp < MAXWP && fabs(a) <= 90 && fabs(b) <= 180) {
      N->wlat[N->nwp] = a; N->wlon[N->nwp] = b;
      const char *nm = l + m;                                 /* "lat lon NAME": the web's names too */
      while (*nm == ' ' || *nm == ',' || *nm == ';' || *nm == '\t') nm++;
      snprintf(N->wname[N->nwp], WP_NAME, "%s", (*nm >= '0' && *nm <= '9') || *nm == '-' ? "" : nm);
      N->nwp++;
    }
    l = strtok(NULL, "\n");
  }
  free(t);
}

void route_insert(double lat, double lon) {
  legs_dirty = plan_dirty = 1;
  if (N->nwp >= MAXWP) return;
  int at = N->nwp ? wp_cur + 1 : 0;
  memmove(&N->wlat[at + 1], &N->wlat[at], sizeof(double) * (size_t)(N->nwp - at));
  memmove(&N->wlon[at + 1], &N->wlon[at], sizeof(double) * (size_t)(N->nwp - at));
  memmove(&N->wname[at + 1], &N->wname[at], WP_NAME * (size_t)(N->nwp - at));
  N->wlat[at] = lat; N->wlon[at] = lon; N->wname[at][0] = 0;
  N->nwp++;
  wp_cur = at;
}

void route_delete(void) {
  legs_dirty = plan_dirty = 1;
  if (!N->nwp) return;
  memmove(&N->wlat[wp_cur], &N->wlat[wp_cur + 1], sizeof(double) * (size_t)(N->nwp - wp_cur - 1));
  memmove(&N->wlon[wp_cur], &N->wlon[wp_cur + 1], sizeof(double) * (size_t)(N->nwp - wp_cur - 1));
  memmove(&N->wname[wp_cur], &N->wname[wp_cur + 1], WP_NAME * (size_t)(N->nwp - wp_cur - 1));
  N->nwp--;
  if (wp_cur >= N->nwp && wp_cur) wp_cur--;
}

static void leg(int i, double *crs, double *nm) {   /* geo47: cached, recomputed on change */
  if (legs_dirty || legs_mode != N->cfg.gc) {
    for (int k = 1; k < N->nwp; k++) {
      if (N->cfg.gc == 2)
        geo_leg_cs(N->wlat[k - 1], N->wlon[k - 1], N->wlat[k], N->wlon[k], cs_lim, &N->leg_crs[k], &N->leg_nm[k]);
      else
        (N->cfg.gc ? leg_gc : leg_rl)(N->wlat[k - 1], N->wlon[k - 1], N->wlat[k], N->wlon[k], &N->leg_crs[k], &N->leg_nm[k]);
    }
    legs_dirty = 0;
    legs_mode = N->cfg.gc;
    plan_dirty = 1;                       /* geo_leg_cs used geo_route as scratch */
  }
  *crs = N->leg_crs[i];
  *nm = N->leg_nm[i];
}

void draw_route(void) {
  char s[64];
  double tot = 0, crs, nm;
  for (int i = 1; i < N->nwp; i++) { leg(i, &crs, &nm); tot += nm; }
  if (N->cfg.gc == 2) sprintf(s, "ROUTE CS %.0f", cs_lim); else strcpy(s, N->cfg.gc ? "ROUTE GC" : "ROUTE RL");
  header(s);
  int xw = 4 + nt_w("100"), xla = xw + 10 + nt_w("00" NT_DEG "00.00N"), xlo = xla + 12 + nt_w("000" NT_DEG "00.00E"),
      xc = xlo + 12 + nt_w("000" NT_DEG), xd = xc + 12 + nt_w("0000.0"), xcu = xd + 12 + nt_w("00000.0");
  int e = (396 - xcu) / 5;
  if (e > 0) { xla += e; xlo += 2 * e; xc += 3 * e; xd += 4 * e; xcu += 5 * e; }
  nt_r(xw, 16, "WP"); nt_c(xla - nt_w("00" NT_DEG "00.00N") / 2, 16, "LAT"); nt_c(xlo - nt_w("000" NT_DEG "00.00E") / 2, 16, "LON");
  nt_r(xc, 16, N->cfg.gc ? "ICRS" : "CRS"); nt_r(xd, 16, "DIST"); nt_r(xcu, 16, "CUM");
  if (!N->nwp) {
    nt(4, 48, "No waypoints yet.");
    nt(4, 72, "HERE: the position (SET)");
    nt(4, 88, "ADD: a new one, then type");
    nt(4, 104, "  LAT DD.MMm ENTER");
    nt(4, 120, "  LON DDD.MMm ENTER");
    nt(4, 136, "  (+/- for S and W)");
    nt(4, 160, "RL rhumb, GC great circle,");
    nt(4, 176, "CS composite (4 6 limit)");
    nt(4, 192, "0: CHART and GLOBE (you, the coast)");
    return;
  }
  int vis = 10;
  if (wp_edit >= 0) nt_inv(2, 192, wp_edit ? "LON DDD.MMm  +/- WEST  ENTER" : "LAT DD.MMm  +/- SOUTH  ENTER");
  else if (wp_cur < N->nwp && N->wname[wp_cur][0]) { sprintf(s, "%d %s  1 WPTS 3 save", wp_cur + 1, N->wname[wp_cur]); nt(2, 192, s); }
  else nt(2, 192, N->cfg.gc == 2 ? "4 6 limit 1 WPTS R/S plan" : "ENTER edit 1 WPTS R/S plan");
  sprintf(s, "%d WP  TOTAL %.1f NM", N->nwp, tot);
  if (wp_edit < 0 || nt_w(s) < 150) nt_r(LCD_X - 2, 192, s);
  if (wp_cur >= N->nwp) wp_cur = N->nwp - 1;
  int top = wp_cur - vis / 2;
  if (top > N->nwp - vis) top = N->nwp - vis;
  if (top < 0) top = 0;
  double cum = 0;
  for (int i = 1; i < top; i++) { leg(i, &crs, &nm); cum += nm; }
  for (int i = top; i < N->nwp && i < top + vis; i++) {
    int y = 32 + NT_ROW * (i - top);
    sprintf(s, "%d", i + 1);
    if (i == wp_cur) nt_inv(xw - nt_w(s), y, s); else nt_r(xw, y, s);
    if (i == wp_cur && wp_edit >= 0) {
      if (wp_edit == 0) { sprintf(s, "%s_", ebuf); nt_r(xla, y, s); }
      else {
        fmt_dmn2(s, wp_lat, 2, wp_lat < 0 ? 'S' : 'N'); nt_r(xla, y, s);
        sprintf(s, "%s_", ebuf); nt_r(xlo, y, s);
      }
      continue;
    }
    fmt_dmn2(s, N->wlat[i], 2, N->wlat[i] < 0 ? 'S' : 'N'); nt_r(xla, y, s);
    fmt_dmn2(s, N->wlon[i], 3, N->wlon[i] < 0 ? 'W' : 'E'); nt_r(xlo, y, s);
    if (i > 0) {
      leg(i, &crs, &nm);
      cum += nm;
      sprintf(s, "%03.0f" NT_DEG, fmod(floor(crs + 0.5), 360.0)); nt_r(xc, y, s);
      sprintf(s, "%.1f", nm); nt_r(xd, y, s);
      sprintf(s, "%.1f", cum); nt_r(xcu, y, s);
    }
  }
}

void wp_start_edit(void) { if (N->nwp) { wp_edit = 0; elen = 0; ebuf[0] = 0; } }

void key_wp(int key) {
  int d = digit(key);
  if (key == KEY_ENTER) {
    double v = parse_dm(ebuf);
    if (wp_edit == 0) {
      wp_lat = elen && fabs(v) <= 90 ? v : N->wlat[wp_cur];
      wp_edit = 1; elen = 0; ebuf[0] = 0;
    } else {
      N->wlat[wp_cur] = wp_lat;
      if (elen && fabs(v) <= 180) N->wlon[wp_cur] = v;
      wp_edit = -1; wp_new = 0;
      route_save();
    }
    return;
  }
  if (key == KEY_EXIT) { wp_edit = -1; if (wp_new) route_delete(); wp_new = 0; return; }
  if (key == KEY_BSP) { if (elen) ebuf[--elen] = 0; return; }
  if (key == KEY_CHS) { if (ebuf[0] == '-') { memmove(ebuf, ebuf + 1, (size_t)elen); elen--; } else if (elen < 14) { memmove(ebuf + 1, ebuf, (size_t)elen + 1); ebuf[0] = '-'; elen++; } return; }
  if (key == KEY_DOT && elen < 14 && !strchr(ebuf, '.')) { ebuf[elen++] = '.'; ebuf[elen] = 0; return; }
  if (d >= 0 && elen < 14) { ebuf[elen++] = (char)('0' + d); ebuf[elen] = 0; }
}

/* ------------------------------------------------------------------ */
/*  PLAN: the route split into waypoints (geo47)                      */
/* ------------------------------------------------------------------ */
static const double STEP_DIST[] = { 10, 20, 30, 50, 60, 100, 120, 150, 200, 250, 300, 500 };
static const double STEP_LEGS[] = { 2, 3, 4, 5, 6, 8, 10, 12, 15, 20 };
static const double STEP_DLON[] = { 1, 2, 5, 10, 15, 20 };

void plan_build(void) {
  static const int sail[3] = { SAIL_RHUMB, SAIL_GEOD, SAIL_COMPOSITE };
  double v = plan_split == SPLIT_DIST ? plan_val * NM : plan_val;
  if (legs_dirty) { double c, n; leg(1, &c, &n); }        /* refresh the ROUTE cache first */
  route_init(&geo_route, &geo_ell);
  plan_rc = ROUTE_OK;
  for (int i = 1; i < N->nwp; i++) {
    int rc = route_add_leg(&geo_route, N->wlat[0], N->wlon[0], N->wlat[i], N->wlon[i],
                           (sail_t)sail[N->cfg.gc % 3], cs_lim, (split_t)plan_split, v);
    if (rc == ROUTE_ELIMIT || rc == ROUTE_ECOMP)           /* this leg cannot be CS: sail it GC */
      rc = route_add_leg(&geo_route, 0, 0, N->wlat[i], N->wlon[i], SAIL_GEOD, 0, (split_t)plan_split, v);
    if (rc != ROUTE_OK && plan_rc == ROUTE_OK) plan_rc = rc;
  }
  route_finish(&geo_route);
  plan_dirty = 0;
}

#define PLAN_HINT "5 split 4 6 step ENT csv sh nmea"
void draw_plan(void) {
  static const char *SPL[4] = { "", "NM", "LEGS", "DLON" };
  char s[64], r[32];
  if (!geo_route_alloc()) { header("PLAN"); nt(4, 48, "Not enough memory for PLAN."); return; }
  route_t *R = &geo_route;
  if (plan_dirty || legs_dirty) plan_build();
  if (plan_split == SPLIT_NONE) sprintf(s, "PLAN %s", SAIL_NAME[N->cfg.gc % 3]);
  else sprintf(s, "PLAN %s %g %s", SAIL_NAME[N->cfg.gc % 3], plan_val, SPL[plan_split]);
  header(s);
  int xw = 6 + nt_w("*000"), xla = xw + 10 + nt_w("00" NT_DEG "00.00N"), xlo = xla + 12 + nt_w("000" NT_DEG "00.00E"),
      xc = xlo + 12 + nt_w("000" NT_DEG), xd = xc + 12 + nt_w("0000.0"), xcu = xd + 12 + nt_w("00000.0"),
      xk = xcu + 8 + nt_w("P");
  int e = (396 - xk) / 6;
  if (e > 0) { xla += e; xlo += 2 * e; xc += 3 * e; xd += 4 * e; xcu += 5 * e; xk += 6 * e; }
  nt_r(xw, 16, "WP"); nt_c(xla - nt_w("00" NT_DEG "00.00N") / 2, 16, "LAT"); nt_c(xlo - nt_w("000" NT_DEG "00.00E") / 2, 16, "LON");
  nt_r(xc, 16, "STEER"); nt_r(xd, 16, "LEG"); nt_r(xcu, 16, "CUM");
  if (R->n < 2 && N->nwp >= 2) plan_build();             /* once more, from the start */
  if (R->n < 2) {
    if (N->nwp < 2) { sprintf(s, "Needs 2 waypoints on ROUTE (it has %d).", N->nwp); nt(4, 48, s); return; }
    sprintf(s, "PLAN failed: route %d WP, plan %d, code %d", N->nwp, R->n, plan_rc); nt(4, 48, s);
    sprintf(s, "free heap %d KB. EXIT, then R/S again.", sys_free_mem() / 1024); nt(4, 64, s);
    return;
  }
  int vis = 10;
  if (plan_cur >= R->n) plan_cur = R->n - 1;
  int top = plan_cur - vis / 2;
  if (top > R->n - vis) top = R->n - vis;
  if (top < 0) top = 0;
  for (int i = top; i < R->n && i < top + vis; i++) {
    const wpt_t *w = &R->w[i];
    int y = 32 + NT_ROW * (i - top);
    sprintf(s, "%s%d", (w->flags & WPT_USER) ? "*" : "", i + 1);
    if (i == plan_cur) nt_inv(xw - nt_w(s), y, s); else nt_r(xw, y, s);
    fmt_dmn2(s, w->lat, 2, w->lat < 0 ? 'S' : 'N'); nt_r(xla, y, s);
    fmt_dmn2(s, w->lon, 3, w->lon < 0 ? 'W' : 'E'); nt_r(xlo, y, s);
    if (i > 0) {
      sprintf(s, "%03.0f" NT_DEG, fmod(floor(R->w[i - 1].rl_crs + 0.5), 360.0)); nt_r(xc, y, s);
      sprintf(s, "%.1f", w->leg_m / NM); nt_r(xd, y, s);
      sprintf(s, "%.1f", w->cum_m / NM); nt_r(xcu, y, s);
      s[0] = w->kind; s[1] = 0; nt_r(xk, y, s);
    }
  }
  if (!plan_msg[0] && R->full) strcpy(plan_msg, "plan full: larger split");
  if (!plan_msg[0] && plan_rc) strcpy(plan_msg, "some legs sailed GC, not CS");
  if (plan_msg[0]) nt_inv(2, 192, plan_msg);
  else nt(2, 192, PLAN_HINT);
  sprintf(r, "%d WP %.1f NM", R->n, R->w[R->n - 1].cum_m / NM);
  if (nt_w(r) + nt_w(plan_msg[0] ? plan_msg : PLAN_HINT) < 388) nt_r(LCD_X - 2, 192, r);
}

static void plan_step(int dir) {
  const double *t; int n, k = 0;
  if (plan_split == SPLIT_DIST) { t = STEP_DIST; n = 12; }
  else if (plan_split == SPLIT_LEGS) { t = STEP_LEGS; n = 10; }
  else if (plan_split == SPLIT_DLON) { t = STEP_DLON; n = 6; }
  else return;
  while (k < n - 1 && t[k] < plan_val - 1e-9) k++;
  k += dir;
  if (k < 0) k = 0;
  if (k > n - 1) k = n - 1;
  plan_val = t[k];
}

static int plan_dirs(void) {
  if (!sys_disk_ok()) { strcpy(plan_msg, "disk busy (USB?)"); return 0; }
  sys_disk_write_enable(1);
  check_create_dir("/NAV47");
  check_create_dir("/NAV47/ROUTES");
  sys_disk_write_enable(0);
  return 1;
}

static void plan_export(void) {
  if (!plan_dirs()) return;
  strcpy(plan_msg, geo_save_csv("/NAV47/ROUTES/PLAN.CSV") == 0 ? "saved /NAV47/ROUTES/PLAN.CSV" : "save failed");
}

/* shift ENTER: $GPWPL + $GPRTE to /NAV47/ROUTES/PLAN.NMEA, and to the PC on console channel 2 when the USB is on */
static void nmea_usb(const char *line, void *ctx) { (void)ctx; usbview_line(USBVIEW_CH_NMEA, line); }

void plan_nmea(void) {
  if (!geo_routep || geo_route.n < 2) return;
  int f = plan_dirs() && geo_save_nmea("/NAV47/ROUTES/PLAN.NMEA") == 0;
  int u = usbview_is_on();
  if (u) route_nmea(&geo_route, GEO_NMEA_ID, nmea_usb, NULL);
  if (f) strcpy(plan_msg, u ? "NMEA: PLAN.NMEA + USB ch 2" : "saved /NAV47/ROUTES/PLAN.NMEA");
  else if (u) strcpy(plan_msg, "NMEA: USB ch 2 (file failed)");
  else if (!plan_msg[0]) strcpy(plan_msg, "NMEA save failed");
}

void plan_key(int key) {
  plan_msg[0] = 0;
  if ((key == KEY_8 || key == KEY_UP) && plan_cur > 0) plan_cur--;
  else if ((key == KEY_2 || key == KEY_DOWN) && geo_routep && plan_cur < geo_route.n - 1) plan_cur++;
  else if (key == KEY_5) {
    plan_split = (plan_split + 1) % 4;
    plan_val = plan_split == SPLIT_DIST ? 100 : plan_split == SPLIT_LEGS ? 4 : plan_split == SPLIT_DLON ? 10 : 0;
    route_save();
  }
  else if (key == KEY_4 || key == KEY_6) { plan_step(key == KEY_6 ? 1 : -1); route_save(); }
  else if (key == KEY_ENTER) plan_export();
  else if (key == KEY_RUN && N->nwp > 1) voy_open(PG_PLAN);
  else if (key == KEY_EXIT) { go(PG_ROUTE); menu_page = 3; }
}

/* ------------------------------------------------------------------ */
/*  COMPARE: one leg as geodesic, rhumb and composite (geo47)         */
/* ------------------------------------------------------------------ */
static int cmp_leg(void) { return wp_cur > 0 ? wp_cur : 1; }   /* the leg that ends at the cursor */

/* row 16: the ellipsoid in use (while editing CUSTOM: the field being typed) */
static void cmp_ell_line(void) {
  char s[64];
  const ellipsoid_t *e = &ellipsoids[geo_ell_index];
  if (cmp_edit == 0) { sprintf(s, "CUSTOM a m: %s_", ebuf); nt_inv(4, 16, s); return; }
  if (cmp_edit == 1) { sprintf(s, "CUSTOM a %.1f  1/f (0 sphere): %s_", cmp_a, ebuf); nt_inv(4, 16, s); return; }
  if (e->rf) sprintf(s, "%s  a %.1f m  1/f %.6f", e->name, e->a, e->rf);
  else sprintf(s, "%s  R %.1f m", e->name, e->a);
  nt(4, 16, s);
}

static void cmp_footer(void) {
  char s[64];
  if (cmp_msg[0]) { nt_inv(2, 192, cmp_msg); return; }
  if (cmp_edit >= 0) { nt_inv(2, 192, cmp_edit ? "1/f: 0 sphere or over 100  ENTER" : "a: 6000000..7000000 m  ENTER"); return; }
  sprintf(s, "7 9 %s%s  8 2 leg", ellipsoids[geo_ell_index].name, geo_ell_index == ELL_CUSTOM ? " ENTER edit" : "");
  nt(2, 192, s);
  if (nt_w(s) < 300) nt_r(LCD_X - 2, 192, "EXIT back");
}

void draw_cmp(void) {
  char s[64], t[32];
  int i = cmp_leg(), y;
  if (N->nwp < 2 || i >= N->nwp) { header("COMPARE"); nt(4, 48, "Needs 2 waypoints on ROUTE."); return; }
  double la1 = N->wlat[i - 1], lo1 = N->wlon[i - 1], la2 = N->wlat[i], lo2 = N->wlon[i];
  double sg, a1, a2, sr, ar, latv, lonv, sv;
  geod_inverse(&geo_ell, la1, lo1, la2, lo2, &sg, &a1, &a2);
  rhumb_inverse(&geo_rh, la1, lo1, la2, lo2, &sr, &ar);
  int in = geod_vertex(&geo_ell, la1, lo1, la2, lo2, &latv, &lonv, &sv);
  const int xg = 268, xr = 396;
  sprintf(s, "COMPARE LEG %d-%d", i, i + 1);
  header(s);
  cmp_ell_line();
  y = 32;  nt(4, y, "FROM");
  fmt_dmn2(t, la1, 2, la1 < 0 ? 'S' : 'N'); nt_r(xg, y, t); fmt_dmn2(t, lo1, 3, lo1 < 0 ? 'W' : 'E'); nt_r(xr, y, t);
  y = 48;  nt(4, y, "TO");
  fmt_dmn2(t, la2, 2, la2 < 0 ? 'S' : 'N'); nt_r(xg, y, t); fmt_dmn2(t, lo2, 3, lo2 < 0 ? 'W' : 'E'); nt_r(xr, y, t);
  y = 72;  nt_r(xg, y, "GEODESIC"); nt_r(xr, y, "RHUMB");
  y = 88;  nt(4, y, "DIST NM");
  sprintf(t, "%.1f", sg / NM); nt_r(xg, y, t); sprintf(t, "%.1f", sr / NM); nt_r(xr, y, t);
  y = 104; nt(4, y, "INITIAL CRS");
  sprintf(t, "%05.1f" NT_DEG, fmod(a1 + 360.0, 360.0)); nt_r(xg, y, t);
  sprintf(t, "%05.1f" NT_DEG, fmod(ar + 360.0, 360.0)); nt_r(xr, y, t);
  y = 120; nt(4, y, "FINAL CRS");
  sprintf(t, "%05.1f" NT_DEG, fmod(a2 + 360.0, 360.0)); nt_r(xg, y, t);
  sprintf(t, "%05.1f" NT_DEG, fmod(ar + 360.0, 360.0)); nt_r(xr, y, t);
  y = 136; nt(4, y, "GC SAVES");
  sprintf(t, "%.1f NM  %.1f%%", (sr - sg) / NM, sr > 0 ? 100.0 * (sr - sg) / sr : 0.0); nt_r(xr, y, t);
  y = 152; nt(4, y, in ? "VERTEX" : "VERTEX (off leg)");
  fmt_dmn2(t, latv, 2, latv < 0 ? 'S' : 'N'); nt_r(xg, y, t); fmt_dmn2(t, lonv, 3, lonv < 0 ? 'W' : 'E'); nt_r(xr, y, t);
  y = 168;
  if (in && fabs(latv) > cs_lim) {
    double cc, cn;
    int rc = geo_leg_cs(la1, lo1, la2, lo2, cs_lim, &cc, &cn);
    plan_dirty = 1;                                    /* geo_route was used as scratch */
    sprintf(s, "CS %.0f" NT_DEG, cs_lim); nt(4, y, s);
    if (rc == ROUTE_OK) { sprintf(t, "%.1f NM  %05.1f" NT_DEG, cn, cc); nt_r(xr, y, t); }
    else nt_r(xr, y, "not possible (end beyond)");
  } else {
    sprintf(s, "CS %.0f" NT_DEG, cs_lim); nt(4, y, s); nt_r(xr, y, "limit not reached");
  }
  cmp_footer();
}

/* CUSTOM: ENTER takes a, then 1/f; an empty field keeps the value; out of range: the field again */
void cmp_edit_key(int key) {
  int d = digit(key);
  cmp_msg[0] = 0;
  if (key == KEY_ENTER) {
    double v = elen ? atof(ebuf) : (cmp_edit == 0 ? ellipsoids[ELL_CUSTOM].a : ellipsoids[ELL_CUSTOM].rf);
    if (cmp_edit == 0) {
      if (!ell_ok(v, 0)) strcpy(cmp_msg, "a: 6000000..7000000 m");
      else { cmp_a = v; cmp_edit = 1; }
    } else if (!ell_ok(cmp_a, v)) strcpy(cmp_msg, "1/f: 0 (sphere) or over 100");
    else {
      ellipsoids[ELL_CUSTOM].a = cmp_a; ellipsoids[ELL_CUSTOM].rf = v;
      geo_set_ellipsoid(ELL_CUSTOM);
      cmp_edit = -1;
      route_save();
    }
    elen = 0; ebuf[0] = 0;
    return;
  }
  if (key == KEY_EXIT) { cmp_edit = -1; return; }
  if (key == KEY_BSP) { if (elen) ebuf[--elen] = 0; return; }
  if (key == KEY_DOT && elen < 14 && !strchr(ebuf, '.')) { ebuf[elen++] = '.'; ebuf[elen] = 0; return; }
  if (d >= 0 && elen < 14) { ebuf[elen++] = (char)('0' + d); ebuf[elen] = 0; }
}

void cmp_key(int key) {
  cmp_msg[0] = 0;
  if (key == KEY_ENTER && geo_ell_index == ELL_CUSTOM) { cmp_edit = 0; elen = 0; ebuf[0] = 0; }
  else if (key == KEY_7 || key == KEY_9) {             /* ellipsoid: the presets and CUSTOM */
    int n = ELL_COUNT, e = (geo_ell_index + (key == KEY_9 ? 1 : n - 1)) % n;
    geo_set_ellipsoid(e);
    route_save();                                      /* saves ELL, marks legs and plan to redo */
  }
  else if ((key == KEY_8 || key == KEY_UP) && cmp_leg() > 1) wp_cur = cmp_leg() - 1;
  else if ((key == KEY_2 || key == KEY_DOWN) && cmp_leg() < N->nwp - 1) wp_cur = cmp_leg() + 1;
  else if (key == KEY_EXIT) { go(PG_ROUTE); menu_page = 3; }
}

/* ------------------------------------------------------------------ */
/*  CHART: the plan on Mercator over the coastline (map47)            */
/* ------------------------------------------------------------------ */
#define CHART_Y0 16
#define CHART_H  175
static map_t chart_m;
int chart_back = PG_PLAN, chart_refit = 1, chart_globe;
int chart_live, chart_follow;                     /* the GPS's chart: the vessel, her track; FOLLOW keeps her centred */
uint32_t anim_frame;
static char chart_detail;                                /* 'C' or 'L': the coastline last drawn, 0 none */

void chart_open(int back) { chart_back = back; chart_refit = 1; chart_live = 0; go(PG_CHART); }

/* no route: you, and the nearest coast in view */
static void chart_fit_here(void) {
  double d = 20, lat = N->cfg.lat, lon = N->cfg.lon;
  if (sys_disk_ok() && map_coast_nearest("/NAV47/COAST_C.BIN", lat, lon, &d) == 0 && d < 3) {
    double dl;
    if (map_coast_nearest("/NAV47/COAST_L.BIN", lat, lon, &dl) == 0) d = dl;   /* close: the finer coast */
  }
  d = d * 1.3 + 0.3;                                     /* the coast inside, with a margin */
  if (chart_globe) {
    map_center(&chart_m, lat, lon);
    float r = (float)sin((d > 80 ? 80 : d) * M_PI / 180);
    chart_m.k = (chart_m.h * 0.5f - 8) / r;
    map_zoom(&chart_m, 1);
    if (d > 80) chart_m.k = map_kmin(&chart_m);
  } else {
    double c = cos(lat * M_PI / 180);
    if (c < 0.1) c = 0.1;
    map_fit(&chart_m, lat - d, lat + d, lon - d / c, lon + d / c);
  }
}

/* points to see on the globe: the plan, and its geodesics sampled (they bulge) */
static int chart_pt(int i, double *la, double *lo) {
  route_t *R = &geo_route;
  if (i < R->n) { *la = R->w[i].lat; *lo = R->w[i].lon; return 1; }
  i -= R->n;
  int leg = i / 7 + 1;
  if (leg >= N->nwp) return 0;
  struct geod_geodesicline l;
  geod_inverseline(&l, &geo_ell, N->wlat[leg - 1], N->wlon[leg - 1], N->wlat[leg], N->wlon[leg], 0);
  geod_position(&l, l.s13 * (i % 7 + 1) / 8, la, lo, NULL);
  return 1;
}

static void chart_fit_globe(void) {
  double x = 0, y = 0, z = 0, la, lo;
  for (int i = 0; chart_pt(i, &la, &lo); i++) {          /* the centre: the points' mean direction */
    double p = la * M_PI / 180, q = lo * M_PI / 180;
    x += cos(p) * cos(q); y += cos(p) * sin(q); z += sin(p);
  }
  map_center(&chart_m, atan2(z, hypot(x, y)) * 180 / M_PI, atan2(y, x) * 180 / M_PI);
  float k = map_kmax(&chart_m);
  for (int i = 0; chart_pt(i, &la, &lo); i++) {
    float r = map_globe_fitpt(&chart_m, la, lo);
    if (r == 0) { k = 0; break; }                        /* too far round: the whole Earth */
    if (r < k) k = r;
  }
  chart_m.k = k;
  map_zoom(&chart_m, 1);                                 /* clamps: at least the whole disc */
}

/* the plan, the user legs' geodesics (their vertices) and a margin */
static void chart_fit(void) {
  map_init(&chart_m, 0, CHART_Y0, LCD_X, CHART_H, &geo_ell);
  chart_m.globe = chart_globe;
  if (N->nwp < 2 || !geo_routep) { chart_fit_here(); return; }
  if (chart_globe) { chart_fit_globe(); return; }
  route_t *R = &geo_route;
  double s = 90, n = -90, w = 1e9, e = -1e9, lon = R->w[0].lon;
  for (int i = 0; i < R->n; i++) {
    double la = R->w[i].lat;
    if (i) { double d = R->w[i].lon - R->w[i - 1].lon; lon += d > 180 ? d - 360 : d < -180 ? d + 360 : d; }
    if (la < s) s = la;
    if (la > n) n = la;
    if (lon < w) w = lon;
    if (lon > e) e = lon;
  }
  for (int i = 1; i < N->nwp; i++) {
    double vl, vo, sv;
    if (geod_vertex(&geo_ell, N->wlat[i - 1], N->wlon[i - 1], N->wlat[i], N->wlon[i], &vl, &vo, &sv)) {
      if (vl < s) s = vl;
      if (vl > n) n = vl;
    }
  }
  double ml = (n - s) * 0.08 + 0.3, mo = (e - w) * 0.08 + 0.3;
  map_fit(&chart_m, s - ml, n + ml, w - mo, e + mo);
}

void draw_chart(void) {
  char s[64];
  int route = N->nwp >= 2;
  if (!route) sprintf(s, chart_globe ? "GLOBE" : "CHART");
  else sprintf(s, N->cfg.gc == 2 ? "%s CS %.0f" : N->cfg.gc ? "%s GC" : "%s RL", chart_globe ? "GLOBE" : "CHART", cs_lim);
  header(s);
  if (route) {
    if (!geo_route_alloc()) { nt(4, 48, "Not enough memory for CHART."); return; }
    if (plan_dirty || legs_dirty) plan_build();
  }
  route_t *R = route ? &geo_route : NULL;
  if (R && R->n < 2) { route = 0; R = NULL; }             /* no plan: the chart without it */
  double vla, vlo, vh;
  int vessel = chart_live && gps_live(&vla, &vlo, &vh);
  if (chart_refit) {
    chart_fit(); chart_refit = 0;
    if (vessel && chart_follow) {                          /* the GPS's chart: 3 nm round the vessel */
      double c = cos(vla * M_PI / 180);
      map_init(&chart_m, 0, CHART_Y0, LCD_X, CHART_H, &geo_ell);
      chart_m.globe = chart_globe;
      if (chart_globe) { map_center(&chart_m, vla, vlo); chart_m.k = (CHART_H * 0.5f) / (float)(1.5 / 60 * M_PI / 180); map_zoom(&chart_m, 1); }
      else map_fit(&chart_m, vla - 0.75 / 60, vla + 0.75 / 60, vlo - 1.5 / 60 / c, vlo + 1.5 / 60 / c);
    }
  }
  if (vessel && chart_follow) {                            /* FOLLOW: centred on her, a margin before it moves */
    float x, y;
    map_xy(&chart_m, vla, vlo, &x, &y);
    if (chart_globe || x < LCD_X * 0.25f || x > LCD_X * 0.75f || y < CHART_Y0 + CHART_H * 0.25f || y > CHART_Y0 + CHART_H * 0.75f)
      map_center(&chart_m, vla, vlo);
  }
  chart_m.e = (float)sqrt(geo_ell.f * (2 - geo_ell.f));   /* the ellipsoid may have changed */
  int step = map_grid(&chart_m);
  chart_detail = 0;
  if (sys_disk_ok()) {
    if (map_span_lon(&chart_m) < 40 && map_coast(&chart_m, "/NAV47/COAST_L.BIN") >= 0) chart_detail = 'L';
    else if (map_coast(&chart_m, "/NAV47/COAST_C.BIN") >= 0) chart_detail = 'C';
  }
  int gc = N->cfg.gc % 3;
  if (route && gc == 2) {                                /* the limit parallel, on the route's side */
    double m = 0;
    for (int i = 0; i < N->nwp; i++) m += N->wlat[i];
    double l = m < 0 ? -cs_lim : cs_lim;
    if (chart_globe) for (int i = 0; i < 72; i++) map_seg(&chart_m, l, i * 5, l, i * 5 + 5, MAP_DOT);
    else {
      float x, y;
      map_xy(&chart_m, l, chart_m.lon0, &x, &y);
      if (y >= CHART_Y0 && y < CHART_Y0 + CHART_H) gfx_hline_dotted(0, LCD_X - 1, (int)lroundf(y), 8);
    }
  }
  if (route) {
    for (int i = 1; i < N->nwp; i++) {                   /* the other sailings, for comparison */
      if (gc != 0) map_seg(&chart_m, N->wlat[i - 1], N->wlon[i - 1], N->wlat[i], N->wlon[i], MAP_DASH);
      if (gc != 1) map_geod(&chart_m, &geo_ell, N->wlat[i - 1], N->wlon[i - 1], N->wlat[i], N->wlon[i], MAP_DOT);
    }
    for (int i = 1; i < R->n; i++) {                     /* the plan: thick */
      const wpt_t *a = &R->w[i - 1], *b = &R->w[i];
      if (b->kind == SEG_GEOD) map_geod(&chart_m, &geo_ell, a->lat, a->lon, b->lat, b->lon, MAP_THICK);
      else map_seg(&chart_m, a->lat, a->lon, b->lat, b->lon, MAP_THICK);
    }
    for (int i = 0; i < R->n; i++) map_mark(&chart_m, R->w[i].lat, R->w[i].lon, (R->w[i].flags & WPT_USER) ? 1 : 0);
    if (plan_cur < R->n) map_mark(&chart_m, R->w[plan_cur].lat, R->w[plan_cur].lon, 2);
  }
  if (!vessel) map_mark(&chart_m, N->cfg.lat, N->cfg.lon, 3);         /* you (SET's position) */
  ports_chart(&chart_m);                                  /* the world's ports (PORTS.CSV), under your objects */
  if (chart_live) gps_chart_layer(&chart_m, anim_frame, chart_follow);
  else obj_chart(&chart_m);
  map_space(&chart_m);
  lcd_fill_rect(0, CHART_Y0 + CHART_H, LCD_X, MENU_Y - CHART_Y0 - CHART_H, 0);   /* the hint's strip: nothing of the chart in it */
  gfx_rect(0, CHART_Y0 + CHART_H, LCD_X, 1, 1);
  if (chart_live) nt(2, 192, chart_follow ? "1 FOLLOW on  7 9 zoom  R/S MOB" : "1 follow  8246 pan  7 9 zoom");
  else nt(2, 192, chart_globe ? "4 6 turn 8 2 tilt 7 9 zoom" : "8246 pan 7 9 zoom 5 fit");
  if (vessel) {                                            /* the GPS's chart: MOB, else where she is */
    if (gps_mob_line(s)) { draw_text_r(&TINY, LCD_X - 2, 195, s, 1); return; }
    if (!chart_detail) { nt_r(LCD_X - 2, 192, "no COAST_C.BIN"); return; }
    char a[20], b[20];
    fmt_dm3(a, vla, 1); fmt_dm3(b, vlo, 0);
    sprintf(s, "%s %s", a, b);
    draw_text_r(&TINY, LCD_X - 2, 195, s, 0);
    return;
  }
  if (chart_detail) sprintf(s, "%c grid %d" NT_DEG, chart_detail, step);
  else strcpy(s, "no COAST_C.BIN");
  nt_r(LCD_X - 2, 192, s);
}

void chart_key(int key) {
  if (chart_live && key == KEY_1) { chart_follow ^= 1; return; }
  if (chart_live && key == KEY_RUN) { gps_mob(); return; }
  if (chart_live && key == KEY_ENTER) { gps_key(GP_NAV, KEY_ENTER); return; }
  if (chart_live && (key == KEY_4 || key == KEY_6 || key == KEY_8 || key == KEY_2 || key == KEY_UP || key == KEY_DOWN)) chart_follow = 0;
  if (key == KEY_4) map_pan(&chart_m, -0.25f, 0);
  else if (key == KEY_6) map_pan(&chart_m, 0.25f, 0);
  else if (key == KEY_8 || key == KEY_UP) map_pan(&chart_m, 0, 0.25f);
  else if (key == KEY_2 || key == KEY_DOWN) map_pan(&chart_m, 0, -0.25f);
  else if (key == KEY_9) map_zoom(&chart_m, 1.5f);
  else if (key == KEY_7) map_zoom(&chart_m, 1 / 1.5f);
  else if (key == KEY_5) chart_refit = 1;
  else if (key == KEY_0) { chart_globe ^= 1; map_set_globe(&chart_m, chart_globe); }
  else if (key == KEY_EXIT) { go(chart_back); if (chart_back == PG_ROUTE) menu_page = 3; if (IS_GPS(chart_back)) menu_page = 4; chart_live = 0; }
}

/* ------------------------------------------------------------------ */
/*  VOYAGE: the plan sailed (or flown) step by step on a globe that follows the vessel */
/* ------------------------------------------------------------------ */
#define VOY_STEPS 96
#define VOY_W     220                                  /* the globe's box: x 0..219, rows 16..190 */
static map_t voy_m;
int voy_step, voy_play, voy_back = PG_PLAN, voy_edit;   /* voy_edit: typing the speed */
double voy_etd;                                 /* the departure, UT (JD) */
static const double VOY_KN[] = { 4, 6, 8, 10, 12, 14, 16, 18, 20, 25, 30, 40, 250, 300, 400, 450, 480, 500, 550 };

void voy_open(int back) {
  voy_back = back; voy_step = 0; voy_play = 0; voy_etd = voy_etd0 > 0 ? voy_etd0 : ut_now();
  map_init(&voy_m, 0, CHART_Y0, VOY_W, CHART_H, &geo_ell);
  voy_m.globe = 1;
  voy_m.k = (VOY_W / 2.0f) / sinf(25 * (float)M_PI / 180);   /* about 50 deg across */
  go(PG_VOY);
}

/* the point s metres along the plan, and the course there */
static void voy_pos(double s, double *lat, double *lon, double *crs) {
  route_t *R = &geo_route;
  int i = 1;
  while (i < R->n - 1 && R->w[i].cum_m < s) i++;
  const wpt_t *a = &R->w[i - 1], *b = &R->w[i];
  double ds = s - a->cum_m;
  if (ds < 0) ds = 0;
  if (ds > b->cum_m - a->cum_m) ds = b->cum_m - a->cum_m;
  if (b->kind == SEG_GEOD) {
    struct geod_geodesicline l;
    geod_inverseline(&l, &geo_ell, a->lat, a->lon, b->lat, b->lon, 0);
    geod_position(&l, ds, lat, lon, crs);
  } else {
    *crs = a->rl_crs;
    if (rhumb_direct(&geo_rh, a->lat, a->lon, a->rl_crs, ds, lat, lon) != 0) { *lat = b->lat; *lon = b->lon; }
  }
  *crs = fmod(*crs + 360.0, 360.0);
}

static void fmt_dt(char *s, double ut) {               /* dd-mm hh:mm in the times shown (UT or LT) */
  int y, m, d;
  double h;
  jd_to_ymd(ut + show_tz() / 24.0 + 1.0 / 2880, &y, &m, &d, &h);
  sprintf(s, "%02d-%02d %02d:%02d", d, m, (int)h, (int)((h - (int)h) * 60));
}

static void fmt_dur(char *s, double hours) {           /* 3d 04:30 */
  long mi = (long)floor(hours * 60 + 0.5);
  if (mi >= 1440) sprintf(s, "%ldd %02ld:%02ld", mi / 1440, (mi / 60) % 24, mi % 60);
  else sprintf(s, "%02ld:%02ld", mi / 60, mi % 60);
}

void draw_voy(void) {
  char s[64], t[32];
  int route = N->nwp >= 2;
  if (!route || !geo_route_alloc()) { header("VOYAGE"); nt(4, 48, "Needs a route (ROUTE, 2 waypoints)."); return; }
  if (plan_dirty || legs_dirty) plan_build();
  route_t *R = &geo_route;
  if (R->n < 2) { header("VOYAGE"); nt(4, 48, "No plan: PLAN first (R/S on ROUTE)."); return; }
  double tot = R->w[R->n - 1].cum_m, sd = tot * voy_step / VOY_STEPS;
  double hours = tot / NM / voy_kn, el = hours * voy_step / VOY_STEPS;
  double lat, lon, crs;
  voy_pos(sd, &lat, &lon, &crs);
  int plane = voy_kn >= 100;
  bar_at(voy_etd + el / 24, plane ? "FLIGHT" : "VOYAGE");          /* the counter: the top bar's date and time */
  map_center(&voy_m, lat, lon);                                     /* the globe follows */
  voy_m.e = (float)sqrt(geo_ell.f * (2 - geo_ell.f));
  map_grid(&voy_m);
  if (sys_disk_ok()) {
    if (!(map_span_lon(&voy_m) < 40 && map_coast(&voy_m, "/NAV47/COAST_L.BIN") >= 0)) map_coast(&voy_m, "/NAV47/COAST_C.BIN");
  }
  for (int i = 1; i < R->n; i++) {                      /* sailed: thick; to go: dashed */
    const wpt_t *a = &R->w[i - 1], *b = &R->w[i];
    int g = b->kind == SEG_GEOD;
    if (b->cum_m <= sd) {
      if (g) map_geod(&voy_m, &geo_ell, a->lat, a->lon, b->lat, b->lon, MAP_THICK);
      else map_seg(&voy_m, a->lat, a->lon, b->lat, b->lon, MAP_THICK);
    } else if (a->cum_m >= sd) {
      if (g) map_geod(&voy_m, &geo_ell, a->lat, a->lon, b->lat, b->lon, MAP_DASH);
      else map_seg(&voy_m, a->lat, a->lon, b->lat, b->lon, MAP_DASH);
    } else {
      if (g) { map_geod(&voy_m, &geo_ell, a->lat, a->lon, lat, lon, MAP_THICK); map_geod(&voy_m, &geo_ell, lat, lon, b->lat, b->lon, MAP_DASH); }
      else { map_seg(&voy_m, a->lat, a->lon, lat, lon, MAP_THICK); map_seg(&voy_m, lat, lon, b->lat, b->lon, MAP_DASH); }
    }
  }
  for (int i = 0; i < R->n; i++) if (R->w[i].flags & WPT_USER) map_mark(&voy_m, R->w[i].lat, R->w[i].lon, 1);
  obj_chart(&voy_m);
  map_icon(&voy_m, lat, lon, crs, plane, !voy_play && (voy_step == 0 || voy_step >= VOY_STEPS));   /* stopped: its side */
  map_space(&voy_m);
  gfx_rect(0, CHART_Y0 + CHART_H, LCD_X, 1, 1);
  gfx_rect(VOY_W, CHART_Y0, 1, CHART_H, 1);
  /* the panel */
  const int X = VOY_W + 6, XR = LCD_X - 2;
  int y = 18;
  fmt_dt(t, voy_etd); nt(X, y, "DEP"); nt_r(XR, y, t); y += 16;
  fmt_dt(t, voy_etd + hours / 24); nt(X, y, "ARR"); nt_r(XR, y, t); y += 16;
  fmt_dur(t, el); nt(X, y, "GONE"); nt_r(XR, y, t); y += 16;
  fmt_dur(t, hours - el); nt(X, y, "TO GO"); nt_r(XR, y, t); y += 16;
  sprintf(t, "%.1f", sd / NM); nt(X, y, "RUN NM"); nt_r(XR, y, t); y += 16;
  sprintf(t, "%.1f", (tot - sd) / NM); nt(X, y, "DTG NM"); nt_r(XR, y, t); y += 16;
  sprintf(t, "%03.0f" NT_DEG "  %.4g kn", crs, voy_kn); nt(X, y, "CRS"); nt_r(XR, y, t); y += 16;
  fmt_dmn2(t, lat, 2, lat < 0 ? 'S' : 'N'); nt_r(XR, y, t); y += 16;
  double lo = fmod(lon + 540.0, 360.0) - 180;
  fmt_dmn2(t, lo, 3, lo < 0 ? 'W' : 'E'); nt_r(XR, y, t); y += 16;
  int pw = XR - X, px = (int)((double)pw * voy_step / VOY_STEPS);            /* the progress */
  gfx_frame(X, y + 2, pw, 8); gfx_rect(X, y + 2, px, 8, 1);
  if (voy_step >= VOY_STEPS) strcpy(s, plane ? "LANDED  R/S again" : "ARRIVED  R/S again");
  else sprintf(s, "%s 4 6 step ENT kn 0 ETA", voy_play ? "R/S pause" : "R/S play");
  if (voy_edit) { sprintf(s, "SPEED kn: %s_  ENTER ok  EXIT", ebuf); nt_inv(2, 192, s); return; }
  nt(2, 192, s);
}

static void voy_speed(int dir) {
  int n = (int)(sizeof VOY_KN / sizeof *VOY_KN), k = 0;
  while (k < n - 1 && VOY_KN[k] < voy_kn - 1e-9) k++;
  if (dir > 0 && VOY_KN[k] <= voy_kn + 1e-9 && k < n - 1) k++;
  else if (dir < 0 && k > 0) k--;
  voy_kn = VOY_KN[k];
  route_save();
}

/* the speed typed: digits and a point, ENTER keeps it (1 to 1000 kn) */
void voy_edit_key(int key) {
  int d = digit(key);
  if (key == KEY_ENTER) {
    double v = atof(ebuf);
    if (elen && v >= 1 && v <= 1000) { voy_kn = v; route_save(); }
    voy_edit = 0;
    return;
  }
  if (key == KEY_EXIT) { voy_edit = 0; return; }
  if (key == KEY_BSP) { if (elen) ebuf[--elen] = 0; return; }
  if (key == KEY_DOT && elen < 6 && !strchr(ebuf, '.')) { ebuf[elen++] = '.'; ebuf[elen] = 0; return; }
  if (d >= 0 && elen < 6) { ebuf[elen++] = (char)('0' + d); ebuf[elen] = 0; }
}

void voy_key(int key) {
  if (key == KEY_ENTER) { voy_play = 0; voy_edit = 1; elen = 0; ebuf[0] = 0; return; }
  if (key == KEY_RUN) {
    if (voy_step >= VOY_STEPS) voy_step = 0;
    voy_play = !voy_play;
  }
  else if (key == KEY_6 && voy_step < VOY_STEPS) { voy_step++; voy_play = 0; }
  else if (key == KEY_4 && voy_step > 0) { voy_step--; voy_play = 0; }
  else if (key == KEY_9) map_zoom(&voy_m, 1.5f);
  else if (key == KEY_7) map_zoom(&voy_m, 1 / 1.5f);
  else if (key == KEY_ADD) voy_speed(1);
  else if (key == KEY_SUB) voy_speed(-1);
  else if (key == KEY_1 || key == KEY_3) { voy_etd += (key == KEY_3 ? 1.0 : -1.0) / 24; voy_etd0 = voy_etd; route_save(); }
  else if (key == KEY_0) go(PG_ETA);
  else if (key == KEY_5) { voy_step = 0; voy_play = 0; }
  else if (key == KEY_EXIT) { voy_play = 0; go(voy_back); if (voy_back == PG_ROUTE) menu_page = 3; }
}

/* ------------------------------------------------------------------ */
/*  ETA: ETD, ETA and speed for the plan's distance: set two, the third follows */
/* ------------------------------------------------------------------ */
enum { E_ETD_D, E_ETD_T, E_ETA_D, E_ETA_T, E_SPEED, E_COUNT };
int eta_sel = E_ETA_D, eta_edit = -1, eta_keep_eta;   /* keep_eta: a new ETD keeps the ETA (the speed follows) */
static char eta_msg[40];

static double plan_nm(void) {
  if (!geo_route_alloc()) return 0;
  if (plan_dirty || legs_dirty) plan_build();
  return geo_route.n >= 2 ? geo_route.w[geo_route.n - 1].cum_m / NM : 0;
}
double etd_now(void) { return voy_etd0 > 0 ? voy_etd0 : ut_now(); }

static void eta_line(int y, int f, const char *label, const char *val, const char *hint) {
  nt(4, y, label);
  if (eta_edit == f) { char s[40]; sprintf(s, "%s_", ebuf); nt_inv(150, y, s); nt(300, y, hint); }
  else if (eta_sel == f) nt_inv(150, y, val); else nt(150, y, val);
}

void draw_eta(void) {
  char s[64], a[24];
  header("ETA");
  double nm = plan_nm();
  if (nm <= 0) { nt(4, 48, "Needs a plan: ROUTE, 2 waypoints, R/S."); return; }
  double etd = etd_now(), hrs = nm / voy_kn, eta = etd + hrs / 24, tz = show_tz();
  int y, m, d;
  double h;
  sprintf(s, "%.1f NM  (%s %s)", nm, SAIL_NAME[N->cfg.gc % 3], N->cfg.lt ? "times LT" : "times UT");
  nt(4, 20, "DISTANCE"); nt(150, 20, s);
  jd_to_ymd(etd + tz / 24 + 0.5 / 1440, &y, &m, &d, &h);
  sprintf(a, "%04d-%02d-%02d", y, m, d); eta_line(44, E_ETD_D, "ETD date", a, "YYYY.MMDD");
  sprintf(a, "%02d:%02d%s", (int)h, (int)((h - (int)h) * 60), voy_etd0 > 0 ? "" : "  (now)"); eta_line(60, E_ETD_T, "ETD time", a, "HH.MM");
  jd_to_ymd(eta + tz / 24 + 0.5 / 1440, &y, &m, &d, &h);
  sprintf(a, "%04d-%02d-%02d", y, m, d); eta_line(84, E_ETA_D, "ETA date", a, "YYYY.MMDD");
  sprintf(a, "%02d:%02d", (int)h, (int)((h - (int)h) * 60)); eta_line(100, E_ETA_T, "ETA time", a, "HH.MM");
  sprintf(a, "%.2f kn", voy_kn); eta_line(124, E_SPEED, "SPEED", a, "1..1000");
  fmt_dur(a, hrs); nt(4, 148, "TIME"); nt(150, 148, a);
  sprintf(s, "a new ETD keeps the %s", eta_keep_eta ? "ETA (the speed follows)" : "SPEED (the ETA follows)");
  nt(4, 168, s);
  if (eta_msg[0]) nt_inv(2, 192, eta_msg);
  else nt(2, 192, "8 2 field ENTER type 0 keep EXIT");
}

/* the typed value into its field */
static void eta_commit(int f) {
  double v = atof(ebuf), nm = plan_nm(), etd = etd_now(), eta = etd + nm / voy_kn / 24, tz = show_tz();
  int y, m, d;
  double h;
  eta_msg[0] = 0;
  if (!elen) return;
  if (f == E_SPEED) {
    if (v >= 1 && v <= 1000) voy_kn = v; else strcpy(eta_msg, "speed: 1 to 1000 kn");
  } else {
    double old = f <= E_ETD_T ? etd : eta, jd = old;
    jd_to_ymd(old + tz / 24, &y, &m, &d, &h);
    if (f == E_ETD_D || f == E_ETA_D) {                       /* YYYY.MMDD, the time kept */
      int yy = (int)v, mm = (int)floor((v - yy) * 100 + 1e-6), dd = (int)lround(((v - yy) * 100 - mm) * 100);
      if (yy > 1900 && mm >= 1 && mm <= 12 && dd >= 1 && dd <= 31) jd = jd_ymd(yy, mm, dd, h) - tz / 24;
      else { strcpy(eta_msg, "date: YYYY.MMDD"); return; }
    } else {                                                  /* HH.MM, the date kept */
      double hh = flow_dm(v);
      if (hh >= 0 && hh < 24) jd = jd_ymd(y, m, d, hh) - tz / 24;
      else { strcpy(eta_msg, "time: HH.MM"); return; }
    }
    if (f <= E_ETD_T) {                                       /* a new departure */
      if (eta_keep_eta) {
        double hrs = (eta - jd) * 24;
        if (hrs <= 0 || nm / hrs > 1000 || nm / hrs < 1) { strcpy(eta_msg, "ETD after the ETA, or 1..1000 kn"); return; }
        voy_kn = nm / hrs;
      }
      voy_etd0 = jd;
    } else {                                                  /* a new arrival: the speed it needs */
      double hrs = (jd - etd) * 24;
      if (hrs <= 0) { strcpy(eta_msg, "ETA before the ETD"); return; }
      if (nm / hrs > 1000 || nm / hrs < 1) { strcpy(eta_msg, "needs < 1 or > 1000 kn"); return; }
      voy_kn = nm / hrs;
      if (voy_etd0 <= 0) voy_etd0 = etd;                      /* the ETD is now fixed too */
    }
  }
  route_save();
}

void eta_edit_key(int key) {
  int d = digit(key);
  if (key == KEY_ENTER) { eta_commit(eta_edit); eta_edit = -1; return; }
  if (key == KEY_EXIT) { eta_edit = -1; return; }
  if (key == KEY_BSP) { if (elen) ebuf[--elen] = 0; return; }
  if (key == KEY_DOT && elen < 10 && !strchr(ebuf, '.')) { ebuf[elen++] = '.'; ebuf[elen] = 0; return; }
  if (d >= 0 && elen < 10) { ebuf[elen++] = (char)('0' + d); ebuf[elen] = 0; }
}

void eta_key(int key) {
  eta_msg[0] = 0;
  if (key == KEY_8 || key == KEY_UP) eta_sel = (eta_sel + E_COUNT - 1) % E_COUNT;
  else if (key == KEY_2 || key == KEY_DOWN) eta_sel = (eta_sel + 1) % E_COUNT;
  else if (key == KEY_ENTER) { eta_edit = eta_sel; elen = 0; ebuf[0] = 0; }
  else if (key == KEY_0) eta_keep_eta ^= 1;
  else if (key == KEY_EXIT) { go(PG_VOY); voy_etd = etd_now(); }
}

/* ------------------------------------------------------------------ */
/*  ETA TABLE: the ETAs at the set speed and three slower ones (0.5 kn apart, or the step chosen) */
/* ------------------------------------------------------------------ */
double etat_base = -1, etat_step = 0.5;
int etat_sel;

void draw_etat(void) {
  char s[64], a[24], d[24];
  header("ETA TABLE");
  double nm = plan_nm();
  if (nm <= 0) { nt(4, 48, "Needs a plan: ROUTE, 2 waypoints, R/S."); return; }
  if (etat_base < 0) etat_base = voy_kn;
  double etd = etd_now(), tz = show_tz();
  int y, m, dd;
  double h;
  jd_to_ymd(etd + tz / 24 + 0.5 / 1440, &y, &m, &dd, &h);
  sprintf(s, "%.1f NM   ETD %02d-%02d %02d:%02d %s", nm, dd, m, (int)h, (int)((h - (int)h) * 60), show_name());
  nt(4, 20, s);
  nt(4, 44, "SPEED"); nt_r(200, 44, "TIME"); nt_r(396, 44, "ETA");
  gfx_rect(2, 61, 396, 1, 1);
  for (int k = 0; k < 4; k++) {
    double kn = etat_base - k * etat_step, yy = 70 + 28 * k;
    if (kn < 0.5) break;
    double hrs = nm / kn;
    jd_to_ymd(etd + hrs / 24 + tz / 24 + 0.5 / 1440, &y, &m, &dd, &h);
    sprintf(s, "%.2f kn%s", kn, fabs(kn - voy_kn) < 1e-6 ? " *" : ""); fmt_dur(a, hrs);
    sprintf(d, "%02d-%02d %02d:%02d", dd, m, (int)h, (int)((h - (int)h) * 60));
    if (k == etat_sel) { gfx_rect(2, (int)yy - 2, 396, 20, 1); nt_inv(6, (int)yy, s); nt_inv(200 - nt_w(a), (int)yy, a); nt_inv(396 - nt_w(d), (int)yy, d); }
    else { nt(6, (int)yy, s); nt_r(200, (int)yy, a); nt_r(396, (int)yy, d); }
  }
  sprintf(s, "* set  8 2 speed 4 6 step %.2g kn 5 ENT use", etat_step);
  nt(2, 192, s);
}

void etat_key(int key) {
  static const double STEPS[3] = { 0.25, 0.5, 1.0 };
  int k = etat_step < 0.3 ? 0 : etat_step < 0.7 ? 1 : 2;
  if (key == KEY_8 || key == KEY_UP) { if (etat_sel > 0) etat_sel--; else etat_base += etat_step; }
  else if (key == KEY_2 || key == KEY_DOWN) { if (etat_sel < 3 && etat_base - (etat_sel + 1) * etat_step >= 0.5) etat_sel++; else if (etat_base - etat_step >= 0.5 + 3 * etat_step) etat_base -= etat_step; }
  else if (key == KEY_4 && k > 0) etat_step = STEPS[k - 1];
  else if (key == KEY_6 && k < 2) etat_step = STEPS[k + 1];
  else if (key == KEY_5) { etat_base = voy_kn; etat_sel = 0; }
  else if (key == KEY_ENTER) {                                /* this line's speed for the voyage */
    double kn = etat_base - etat_sel * etat_step;
    if (kn >= 1 && kn <= 1000) { voy_kn = kn; route_save(); }
  }
  else if (key == KEY_EXIT) go(PG_ETA);
}

/* the animation: a step every 250 ms while playing */
void voy_tick(void) {
  if (voy_step < VOY_STEPS) voy_step++;
  if (voy_step >= VOY_STEPS) voy_play = 0;
}
