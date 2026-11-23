/* dp_ui.c - NAV47: DP OPS (station keeping off a platform, DPCAP47's engine), ABS SKP and their INPUT.
 * Split from main.c; the shared state and the calls between the files are in app.h.
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#include "app.h"

/* ------------------------------------------------------------------ */
/*  DP OPS: station keeping off a platform (src/dpops.c, DPCAP47's engine). The platform comes in /NAV47/DPOPS.TXT  */
/*  (made on the web page), the vessel in a DPCAP47 .DPV (VESSEL line, or /NAV47/VESSEL.DPV; else the example). The */
/*  wind is SET's (FLOW's own), the current the FLOW program's at the platform (or typed), the sea DNV's for the     */
/*  wind (or typed).                                                                                                  */
/* ------------------------------------------------------------------ */
#define DPOPSFILE  "/NAV47/DPOPS.TXT"
#define DPVESSEL   "/NAV47/VESSEL.DPV"
typedef struct {
  Vessel v;
  DpPlat p;
  DpWx w;
  DpRes r;
  int loaded, face, manual, showfail, tonnes, sel, edit;
  float hdg, limw;
  char msg[48], csrc[24], vsrc[24];
} DpS;
static DpS *DP;

/* the current now at a position, as T&F: the FLOW atlas (springs scaled, the wind added), else a stream station
   within 30 NM (AUTO); 0: none */
static int stream_at(double lat, double lon, double ut, double *set, double *kn, char *src, int srcn) {
  const FlowAtlas *fl = atlas();
  int rs = 0, rr = 0;
  const TStation *ref = fl ? find_station(fl->ref, &rs, &rr) : NULL;
  double FU[13], FV[13], U[13], V[13], u, v;
  long n0, on;
  int om = 0;
  double h0, t0, hh, rng, fac;
  if (ref && flow_cell(fl, lat, lon, FU, FV)) {
    st_local(ref, ut, &n0, &h0);
    flow_pick(ref, n0, h0, &on, &om);
    flow_cycle(ref, on, om, &t0, &hh, &rng, &fac);
    flow_rows(FU, FV, fac, N->cfg.wdir, N->cfg.wkn, U, V);
    flow_now(U, V, ((double)(n0 - on) * 24.0 + h0) - t0, &u, &v);
    setkn(u, v, set, kn);
    snprintf(src, (size_t)srcn, "FLOW atlas");
    return 1;
  }
  double dn;
  const TStation *st = nearest_station(lat, lon, 1, &dn, NULL, NULL, NULL);
  if (st && dn <= 30 && st->kind != TK_TIDE) {
    double val, dir;
    st_local(st, ut, &n0, &h0);
    stream_now(st, n0, h0, &val, &dir);
    *kn = fabs(val); *set = dir;
    snprintf(src, (size_t)srcn, "%.12s %.0fNM", st->name, dn);
    return 1;
  }
  *set = 0; *kn = 0;
  snprintf(src, (size_t)srcn, "no stream: type it");
  return 0;
}

static void dp_weather(void) {
  Cfg *c = &N->cfg;
  DpWx *w = &DP->w;
  w->wfrom = (float)c->wdir; w->wkn = (float)c->wkn;
  double se = c->dcset, kn = c->dckn;
  if (c->dcauto) {
    double lat = DP->p.lat || DP->p.lon ? DP->p.lat : c->lat, lon = DP->p.lat || DP->p.lon ? DP->p.lon : c->lon;
    stream_at(lat, lon, ut_now(), &se, &kn, DP->csrc, sizeof DP->csrc);
  } else strcpy(DP->csrc, "typed");
  w->cset = (float)se; w->ckn = (float)kn;
  if (c->dsauto) { w->vfrom = w->wfrom; dpo_waves_auto(w); }
  else { w->vfrom = (float)c->dsfrom; w->hs = (float)c->dhs; w->tp = (float)c->dtp; }
}

static void dp_load(void) {
  if (!DP) { DP = (DpS *)calloc(1, sizeof *DP); if (!DP) return; }
  char err[48];
  char *t = read_text(DPOPSFILE);
  DP->msg[0] = 0;
  if (!t || dpo_parse(t, &DP->p, err, sizeof err)) {
    if (t) snprintf(DP->msg, sizeof DP->msg, "DPOPS.TXT %.30s", err);
    dpo_default(&DP->p);
  }
  free(t);
  t = read_text(DP->p.vessel[0] ? DP->p.vessel : DPVESSEL);
  memset(&DP->v, 0, sizeof DP->v);
  if (!t || dp_parse(&DP->v, t, err, sizeof err)) {
    if (t && !DP->msg[0]) snprintf(DP->msg, sizeof DP->msg, "vessel: %.30s", err);
    memset(&DP->v, 0, sizeof DP->v);
    dp_parse(&DP->v, DPO_EXAMPLE_VESSEL, err, sizeof err);
    strcpy(DP->vsrc, "example");
  } else strcpy(DP->vsrc, "file");
  free(t);
  dp_prepare(&DP->v);
  dpo_limits(&DP->v, &DP->p);
  DP->loaded = 1; DP->manual = 0; DP->limw = -1;
  DP->face = DP->p.face < 0 ? 0 : DP->p.face;
}

static void dp_save_plat(void) {
  if (!sys_disk_ok()) return;
  char *b = (char *)malloc(1024);
  if (!b) return;
  dpo_write(&DP->p, b, 1024);
  sys_disk_write_enable(1);
  check_create_dir(DIR);
  FIL f;
  UINT bw;
  if (f_open(&f, DPOPSFILE, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) { f_write(&f, b, (UINT)strlen(b), &bw); f_close(&f); }
  sys_disk_write_enable(0);
  free(b);
}

/* a key pressed while BEST or ABS SKP computes: stop (the key is used up, it does nothing else). Only a press: DMCP
   queues a release (0) after each key, and the one of the key that started the run comes in while it runs */
static int dp_key_stop(void) {
  int stop = 0;
  while (!key_empty()) { int k = key_pop(); if (k > 0 && k != USBVIEW_KEY_REDRAW) stop = 1; }
  if (stop) dp_abort = 1;
  return stop;
}

static void dp_busy(int n, int tot) {
  if (dp_key_stop() || (n % 8 && n != tot)) return;
  char s[40];
  sprintf(s, "computing %d / %d  any key: stop", n, tot);
  gfx_rect(206, 150, 194, 16, 0);
  draw_text(&TINY, 206, 154, s, 0);
  lcd_refresh();
}

/* BEST: every heading the side and the tolerance allow (every face with FACE AUTO), then the wind she holds to */
static void dp_best(void) {
  dp_weather();
  dp_abort = 0;
  if (strstr(DP->msg, "stopped")) DP->msg[0] = 0;          /* a stop's note: gone with the next run */
  dpo_best(&DP->v, &DP->p, &DP->w, 5, &DP->r, dp_busy);
  DP->face = DP->r.face; DP->hdg = DP->r.hdg; DP->manual = 0;
  if (dp_abort) {                                          /* stopped: the best of the headings tried, no wind limit */
    DP->limw = -1;
    snprintf(DP->msg, sizeof DP->msg, "BEST stopped");
    dp_abort = 0;
    return;
  }
  DP->limw = dpo_limit_wind(&DP->v, &DP->p, &DP->w, DP->face, DP->hdg);
}

static void dp_eval_here(void) {
  dp_weather();
  dpo_eval(&DP->v, &DP->p, &DP->w, DP->face, DP->hdg, &DP->r);
  DP->limw = -1;
}

void dp_enter(void) {
  if (!DP || !DP->loaded) dp_load();
  if (DP) dp_best();
}

/* the picture: north up, framed on the platform and the vessel; the platform hatched, its working face thick, the
   standoff line dashed, the face's centre line dotted (the vessel centred on it); the vessel, its thrusters' forces
   (arrows), the weather's whole push (a thick arrow, tonnes); the weather's directions bottom left */
#define DP_T 9.80665f                                     /* kN a tonne-force */
static void dp_arrow_w(int x, int y, float to, int len, int thick) {   /* an arrow towards true `to` */
  for (int k = -thick; k <= thick; k++) {
    int ox = (int)lroundf(k * cosf(to * (float)(M_PI / 180))), oy = (int)lroundf(k * sinf(to * (float)(M_PI / 180)));
    gfx_line(x + ox, y + oy, x + ox + (int)lroundf(len * sinf(to * (float)(M_PI / 180))), y + oy - (int)lroundf(len * cosf(to * (float)(M_PI / 180))), 1);
  }
  arrow(x + (int)lroundf((len - 1) * sinf(to * (float)(M_PI / 180))), y - (int)lroundf((len - 1) * cosf(to * (float)(M_PI / 180))), to, 1 + thick);
}

static void dp_picture(void) {
  DpS *d = DP;
  DpRes *r = &d->r;
  float hull[7][2], pc[4][2], o = d->p.orient * (float)(M_PI / 180);
  float a0x = sinf(o), a0y = cosf(o), a1x = cosf(o), a1y = -sinf(o);
  static const float sx[4] = { 1, 1, -1, -1 }, sy[4] = { 1, -1, -1, 1 };
  for (int k = 0; k < 4; k++) {
    pc[k][0] = sx[k] * d->p.len / 2 * a0x + sy[k] * d->p.wid / 2 * a1x;
    pc[k][1] = sx[k] * d->p.len / 2 * a0y + sy[k] * d->p.wid / 2 * a1y;
  }
  dpo_hull(&d->v, &d->p, r->hdg, r->px, r->py, hull);
  /* the frame: the box round the platform and the vessel, a margin for the arrows */
  float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
  for (int k = 0; k < 4; k++) { x0 = fminf(x0, pc[k][0]); x1 = fmaxf(x1, pc[k][0]); y0 = fminf(y0, pc[k][1]); y1 = fmaxf(y1, pc[k][1]); }
  for (int k = 0; k < 7; k++) { x0 = fminf(x0, hull[k][0]); x1 = fmaxf(x1, hull[k][0]); y0 = fminf(y0, hull[k][1]); y1 = fmaxf(y1, hull[k][1]); }
  float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, R = 1;
  for (int k = 0; k < 4; k++) R = fmaxf(R, hypotf(pc[k][0] - cx, pc[k][1] - cy));
  for (int k = 0; k < 7; k++) R = fmaxf(R, hypotf(hull[k][0] - cx, hull[k][1] - cy));
  const int X0 = 100, Y0 = 110, RING = 84;                    /* the drawing inside a compass ring, as a DP console */
  float sc = 64 / R;
#define PX(x) (X0 + (int)lroundf(((x) - cx) * sc))
#define PY(y) (Y0 - (int)lroundf(((y) - cy) * sc))
  /* the platform: hatched, its outline */
  int bx0 = 999, bx1 = -999, by0 = 999, by1 = -999;
  for (int k = 0; k < 4; k++) { bx0 = imin(bx0, PX(pc[k][0])); bx1 = imax(bx1, PX(pc[k][0])); by0 = imin(by0, PY(pc[k][1])); by1 = imax(by1, PY(pc[k][1])); }
  for (int y = imax(by0, 17); y <= imin(by1, MENU_Y - 1); y++)
    for (int x = imax(bx0, 0); x <= imin(bx1, 199); x++) {
      if ((x + y) & 1) continue;
      float wx = cx + (x - X0) / sc, wy = cy - (y - Y0) / sc;
      if (fabsf(wx * a0x + wy * a0y) <= d->p.len / 2 && fabsf(wx * a1x + wy * a1y) <= d->p.wid / 2) gfx_pixel(x, y, 1);
    }
  for (int k = 0; k < 4; k++) gfx_line(PX(pc[k][0]), PY(pc[k][1]), PX(pc[(k + 1) % 4][0]), PY(pc[(k + 1) % 4][1]), 1);
  static const int fk[4][2] = { { 0, 1 }, { 3, 0 }, { 2, 3 }, { 1, 2 } };   /* face 0 +a0 (corners 0 1), 1 +a1 (3 0), 2 -a0, 3 -a1 */
  int f = d->face & 3;
  float nb = dpo_face_normal(&d->p, f) * (float)(M_PI / 180), nx = sinf(nb), ny = cosf(nb);
  for (int t = 1; t <= 3; t++) {                              /* the working face: 3 px, just outside */
    int ox = (int)lroundf(t * nx), oy = -(int)lroundf(t * ny);
    gfx_line(PX(pc[fk[f][0]][0]) + ox, PY(pc[fk[f][0]][1]) + oy, PX(pc[fk[f][1]][0]) + ox, PY(pc[fk[f][1]][1]) + oy, 1);
  }
  /* the standoff line (dashed, along the face) and the face's centre line (dotted, the vessel centred on it) */
  float dd = ((f & 1) ? d->p.wid : d->p.len) / 2 + d->p.standoff, span = dpo_loa(&d->v, &d->p) * 0.6f;
  for (float t = -span; t <= span; t += 2.5f / sc) {
    if (((int)((t + span) * sc / 3)) & 1) continue;
    int x = PX(nx * dd + ny * t), y = PY(ny * dd - nx * t);
    if ((x - X0) * (x - X0) + (y - Y0) * (y - Y0) < (RING - 3) * (RING - 3)) gfx_pixel(x, y, 1);
  }
  float far = dd + dpo_loa(&d->v, &d->p) + d->v.b;
  for (float t = ((f & 1) ? d->p.wid : d->p.len) / 2; t <= far; t += 4 / sc) {
    int x = PX(nx * t), y = PY(ny * t);
    if ((x - X0) * (x - X0) + (y - Y0) * (y - Y0) < (RING - 3) * (RING - 3)) gfx_pixel(x, y, 1);
  }
  /* the vessel: white, its outline, the helideck at the bow */
  for (int y = 17; y < MENU_Y; y++)
    for (int x = 0; x < 200; x++) {
      float wx = cx + (x - X0) / sc, wy = cy - (y - Y0) / sc;
      int in = 0;
      for (int i = 0, j = 6; i < 7; j = i++)
        if ((hull[i][1] > wy) != (hull[j][1] > wy) &&
            wx < (hull[j][0] - hull[i][0]) * (wy - hull[i][1]) / (hull[j][1] - hull[i][1]) + hull[i][0]) in = !in;
      if (in) gfx_pixel(x, y, 0);
    }
  for (int i = 0; i < 7; i++) gfx_line(PX(hull[i][0]), PY(hull[i][1]), PX(hull[(i + 1) % 7][0]), PY(hull[(i + 1) % 7][1]), 1);
  float L = dpo_loa(&d->v, &d->p), h = r->hdg * (float)(M_PI / 180), fx = sinf(h), fy = cosf(h), sbx = cosf(h), sby = -sinf(h);
  gfx_circle(PX(r->px + fx * L * 0.33f), PY(r->py + fy * L * 0.33f), imax(2, (int)lroundf(d->v.b * 0.32f * sc)), 1);
  /* the thrusters' forces: an arrow each, from the thruster, the longest 26 px; the weather's push, thick, at midships */
  float (*F)[2] = d->showfail ? r->ff : r->f, fmax = r->fmag;
  for (int i = 0; i < d->v.nthr; i++) fmax = fmaxf(fmax, hypotf(F[i][0], F[i][1]));
  for (int i = 0; i < d->v.nthr; i++) {
    const Thruster *t = &d->v.thr[i];
    float wx = r->px + t->x * fx + t->y * sbx, wy = r->py + t->x * fy + t->y * sby;
    int tx = PX(wx), ty = PY(wy);
    gfx_rect(tx - 1, ty - 1, 3, 3, 1);
    float m = hypotf(F[i][0], F[i][1]);
    if (m < 0.01f * fmax || m < 0.5f) continue;
    float ex = F[i][0] * fx + F[i][1] * sbx, ey = F[i][0] * fy + F[i][1] * sby;      /* the thrust in the world */
    float to = atan2f(ex, ey) * (float)(180 / M_PI);
    dp_arrow_w(tx, ty, to, 4 + (int)lroundf(22 * m / fmax), 0);
  }
#undef PX
#undef PY
  /* the weather's whole push (x 1.25): a circle in the lower right corner, its direction above, tonnes below */
  {
    const int CX = 181, CY = 183, CR = 11;
    char s[16];
    gfx_rect(CX - 14, CY - CR - 2, 29, 2 * CR + 5, 0);
    gfx_circle(CX, CY, CR, 1);
    if (r->fmag > 0.5f) {
      arrow(CX - (int)lround((CR - 2) * dsin(r->fto)), CY + (int)lround((CR - 2) * dcos(r->fto)), r->fto, 2 * CR - 4);
      sprintf(s, "%03.0f", r->fto);
    } else strcpy(s, "-");
    draw_text(&TINY, CX - 3 * (int)strlen(s), CY - CR - 11, s, 0);
    sprintf(s, "%.1ft", r->fmag / DP_T);
    draw_text(&TINY, CX - 3 * (int)strlen(s), CY + CR + 3, s, 0);
  }
  /* the compass ring; the wind (W), the current (C) and the sea (S) as arrows on it pointing in, from where they come
     (the current's from where it sets from); arrows close together set apart */
  for (int a = 0; a < 360; a += 3) gfx_pixel(X0 + (int)lround(RING * dsin(a)), Y0 - (int)lround(RING * dcos(a)), 1);
  for (int a = 0; a < 360; a += 90) {
    gfx_line(X0 + (int)lround((RING - 4) * dsin(a)), Y0 - (int)lround((RING - 4) * dcos(a)),
             X0 + (int)lround((RING + 3) * dsin(a)), Y0 - (int)lround((RING + 3) * dcos(a)), 1);
  }
  struct { float from; const char *l; float v; } wx[3] = { { d->w.wfrom, "W", d->w.wkn }, { d->w.cset + 180, "C", d->w.ckn },
                                                       { d->w.vfrom, "S", d->w.hs } };
  for (int k = 0; k < 3; k++) {
    if (wx[k].v <= 0) continue;
    double a = wx[k].from;
    int shift = 0;
    for (int j = 0; j < k; j++) {
      double dd = fabs(fmod(wx[j].from - a + 540, 360) - 180);
      if (wx[j].v > 0 && dd < 14) shift++;
    }
    int px0 = (int)lround(shift * 9 * dcos(a)), py0 = (int)lround(shift * 9 * dsin(a));   /* sideways */
    int tx = X0 + (int)lround((RING + 8) * dsin(a)) + px0, ty = Y0 - (int)lround((RING + 8) * dcos(a)) + py0;
    arrow(tx, ty, a + 180, 18);
    int lx = X0 + (int)lround((RING - 19) * dsin(a)) + px0 - 2, ly = Y0 - (int)lround((RING - 19) * dcos(a)) + py0 - 4;   /* inside, at its tip */
    lx = imax(1, imin(194, lx)); ly = imax(18, imin(198, ly));
    gfx_rect(lx - 1, ly - 1, 7, 9, 0);
    draw_text(&TINY, lx, ly, wx[k].l, 0);
  }
  if (r->mid > d->p.standoff + 0.05f || r->clear < d->p.standoff - 0.05f) {   /* turned: moved out for a corner */
    char s[32];
    sprintf(s, "MID %.1fm CORNER %.1fm", r->mid, r->clear);
    gfx_rect(0, 198, 120, 9, 0); draw_text(&TINY, 1, 199, s, 0);
  }
  gfx_rect(0, 17, 22, 24, 0);
  arrow(8, 36, 0, 14); draw_text(&TINY, 12, 20, "N", 0);
  for (int y = 17; y < MENU_Y; y += 2) lcd_fill_rect(201, (uint32_t)y, 1, 1, 1);
}

void draw_dp(void) {
  header("DP OPS");
  if (!DP) { nt(4, 40, "No memory for DP OPS."); return; }
  DpS *d = DP;
  DpRes *r = &d->r;
  char s[64];
  dp_picture();
  const int X = 206;
  sprintf(s, "FACE %03.0f%s %s%s", dpo_face_normal(&d->p, d->face), d->p.face < 0 ? "*" : "", DPO_SIDE[d->p.side],
          dpo_tol(&d->p) == 0 && d->p.side != DPS_STERN && d->p.side != DPS_BOW ? " PAR" : "");
  nt(X, 18, s);
  sprintf(s, "HDG %03.0f%s  %.0fm OFF", r->hdg, d->manual ? " SET" : " BEST", r->mid);
  nt(X, 34, s);
  sprintf(s, "WIND %03.0f %.0fkn", d->w.wfrom, d->w.wkn);
  nt(X, 50, s);
  if (r->wleft < 0.995f) { sprintf(s, "LEE %.0f%%", (1 - r->wleft) * 100); draw_text(&TINY, X + 138, 54, s, 0); }
  sprintf(s, "CUR %03.0f %.1fkn", d->w.cset, d->w.ckn);
  nt(X, 66, s);
  draw_text(&TINY, X + 138, 70, N->cfg.dcauto ? "FLOW" : "TYPED", 0);
  if (d->w.hs > 0) sprintf(s, "SEA %03.0f %.1fm %.0fs", d->w.vfrom, d->w.hs, d->w.tp); else strcpy(s, "SEA none");
  nt(X, 82, s);
  sprintf(s, "PUSH %.1ft >%03.0f %s", r->fmag / DP_T, r->fto, r->blow >= 0 ? "OFF" : "ON");   /* the weather's, x 1.25 */
  nt(X, 98, s);
  const char *st = r->held_lim ? "HOLDS IN LIMITS" : r->held_full ? "OVER THE LIMITS" : "NO HOLD";
  gfx_rect(X - 2, 114, 196, 16, 1);
  nt_inv(X, 114, st);
  sprintf(s, "FAIL G%d %s", r->fail_grp, r->fail_held ? "HOLDS" : "NO HOLD");
  nt(X, 130, s);
  if (d->limw >= 0) sprintf(s, "WIND LIMIT %s%.0fkn", d->limw >= DPO_WIND_TOP ? ">" : "", d->limw);
  else strcpy(s, "ENTER: BEST, LIMIT");
  nt(X, 146, s);
  /* the thrusters' bars: 100 % = 24 px, the limit a tick; intact or after the worst failure (0); % or tonnes (3) */
  int n = d->v.nthr, cw = n ? 192 / n : 192, top = 172, H = 24;
  if (d->showfail) sprintf(s, "GROUP %d LOST, %s", r->fail_grp, d->tonnes ? "TONNES" : "% OF FULL");
  else sprintf(s, "INTACT, %s", d->tonnes ? "TONNES" : "% OF FULL (- THE LIMIT)");
  draw_text(&TINY, X, 163, s, 0);
  for (int i = 0; i < n; i++) {
    float u = d->showfail ? r->fail_wuse[i] : r->use[i], lim = d->p.lim[i] > 0 ? d->p.lim[i] : 1;
    float (*F)[2] = d->showfail ? r->ff : r->f;
    int x = X + i * cw + 3, w = imin(cw - 8, 18), hgt = (int)lroundf(fminf(u, 1) * H);
    gfx_frame(x, top, w, H + 1);
    gfx_rect(x, top + H + 1 - hgt, w, hgt, 1);
    if (lim < 1 && !d->showfail) { int ly = top + H - (int)lroundf(lim * H); gfx_rect(x - 2, ly, w + 4, 1, 1); gfx_rect(x - 2, ly, 1, 1, 0); }
    if (d->tonnes) sprintf(s, "%.1f", hypotf(F[i][0], F[i][1]) / DP_T); else sprintf(s, "%.0f", u * 100);
    draw_text(&TINY, x + w + 1, top + H - 6, s, 0);
    draw_text(&TINY, x, top + H + 3, d->v.thr[i].name, 0);
  }
  if (d->msg[0]) draw_text(&TINY, 80, MENU_Y - LH, d->msg, 0);
  else draw_text(&TINY, 2, MENU_Y - LH, ". ABS SKP", 0);
}

static int abs_run(void);
/* SET's wind (DP OPS and ABS SKP use it): its direction ddir degrees, its speed dkn knots; to 10 degrees and 1 kn, saved */
static void wind_step(int ddir, int dkn) {
  Cfg *c = &N->cfg;
  c->wdir = fmod(c->wdir + 360 + ddir, 360);
  c->wkn = fmin(99, fmax(0, c->wkn + dkn));
  c->wdir = 10 * floor(c->wdir / 10 + 0.5); if (c->wdir >= 360) c->wdir -= 360;
  c->wkn = floor(c->wkn + 0.5);
  cfg_save();
}
static int abs_wind;                                         /* the wind changed on ABS SKP: DP OPS again on the way back */

void dp_key(int key) {
  DpS *d = DP;
  if (!d) { if (key == KEY_EXIT) go(PG_VALM); return; }
  if (key == KEY_ENTER) dp_best();
  else if (key == KEY_4 || key == KEY_6) { d->hdg = fmodf(d->r.hdg + (key == KEY_4 ? 355 : 5), 360); d->manual = 1; dp_eval_here(); }
  else if (key == KEY_UP || key == KEY_DOWN) { step_time(key == KEY_UP ? 1 : -1); d->manual ? dp_eval_here() : dp_best(); }
  else if (key == KEY_5) { toff = 0; dp_best(); }
  else if (key == KEY_7 || key == KEY_9 || key == KEY_8 || key == KEY_2) {   /* the wind: 7 9 its direction, 8 2 its speed */
    wind_step(key == KEY_7 ? -10 : key == KEY_9 ? 10 : 0, key == KEY_8 ? 1 : key == KEY_2 ? -1 : 0);
    d->manual ? dp_eval_here() : dp_best();
  }
  else if (key == KEY_0) d->showfail ^= 1;
  else if (key == KEY_3) d->tonnes ^= 1;                       /* the bars: % of full thrust or tonnes */
  else if (key == KEY_1) { d->p.par ^= 1; dp_save_plat(); dp_best(); }   /* side to: parallel to the face, or within TOL */
  else if (key == KEY_DOT) { if (abs_run()) go(PG_DPABS); }  /* ABS SKP: the vessel's capability, every direction */
  else if (key == KEY_EXIT) go(PG_VALM);
}

void dp_soft(int fn) {
  DpS *d = DP;
  if (!d) { go(PG_VALM); return; }
  if (fn == 0) dp_best();
  else if (fn == 1) { d->p.face = d->p.face >= 3 ? -1 : d->p.face + 1; dp_save_plat(); if (d->p.face >= 0) d->face = d->p.face; dp_best(); }
  else if (fn == 2) { d->p.side = (d->p.side + 1) % DPS_N; dp_save_plat(); dp_best(); }
  else if (fn == 3) { go(PG_DPIN); d->sel = 0; d->edit = -1; }
  else if (fn == 4) { dp_load(); dp_best(); if (!d->msg[0]) snprintf(d->msg, sizeof d->msg, "%.14s, %.20s (%.7s)", d->p.name, d->v.name, d->vsrc); }
  else go(PG_VALM);
}

/* ------------------------------------------------------------------ */
/*  ABS SKP: the ABS Guide for Dynamic Positioning Systems, Section 9, station keeping performance (DPCAP47's engine, */
/*  dp_abs: the North Sea wind-wave table, 1-min mean wind, a fixed current, a margin on the weather, the ABS bollard */
/*  pull from the propellers' diameters). The vessel's capability in every direction, not today's set-up at the       */
/*  platform: SKP(a, b, c, d, e, f) and the limiting wind a direction, intact and after the worst-case failure.       */
/* ------------------------------------------------------------------ */
#define ABS_D2R ((float)M_PI / 180)
static AbsResult *ABS;
static int abs_have, abs_cur10 = -1, abs_margin = 20;     /* the current: -1 today's (0.5 to 3 kn), else tenths of a knot */
static float abs_cur;

static float abs_current(void) {
  if (abs_cur10 >= 0) return abs_cur10 / 10.0f;
  float k = roundf(DP->w.ckn * 2) / 2;                    /* today's, to half a knot, inside the Guide's 0.5 .. 3 */
  return k < 0.5f ? 0.5f : k > 3 ? 3 : k;
}

static void abs_busy(int n, int tot) {
  if (dp_key_stop() || (n % 12 && n != tot)) return;
  char s[40];
  sprintf(s, "ABS SKP: %d of %d", n, tot);
  gfx_rect(0, 100, LCD_X, 18, 0);
  nt_c(LCD_X / 2, 100, s);
  nt_c(LCD_X / 2, 124, "any key: stop");
  lcd_refresh();
}

/* 1: a result to show; 0: no memory, or stopped by a key (DP OPS says so) */
static int abs_run(void) {
  if (!ABS && !(ABS = (AbsResult *)malloc(sizeof *ABS))) { abs_have = 0; return 0; }
  dp_weather();
  abs_cur = abs_current();
  lcd_clear_buf();
  header("ABS SKP");
  nt_c(LCD_X / 2, 80, DP->v.name);
  lcd_refresh();
  dp_abort = 0;
  abs_have = dpo_abs(&DP->v, abs_cur, abs_margin / 100.0f, ABS, abs_busy);   /* dp_abs's steps, the copy on the heap */
  if (dp_abort) { abs_have = 0; snprintf(DP->msg, sizeof DP->msg, "ABS stopped"); }
  dp_abort = 0;
  return abs_have;
}

/* the limiting wind at a direction (degrees from the bow), between the 10-degree ones */
static float abs_at(const float *w, float rel) {
  rel = fmodf(rel + 360, 360);
  int h = (int)(rel / 10) % NHEAD, k = (h + 1) % NHEAD;
  float f = (rel - h * 10) / 10;
  return w[h] + f * (w[k] - w[h]);
}

static void abs_curve(int cx, int cy, float R, const float *w, int dashed) {
  for (int h = 0; h < NHEAD; h++)
    for (int j = 0; j < 4; j++) {                         /* each 10 degrees in 4 pieces, every other one when dashed */
      if (dashed && j % 2) continue;
      float a0 = (h * 10 + j * 2.5f), a1 = a0 + 2.5f;
      float r0 = abs_at(w, a0) / 40.0f * R, r1 = abs_at(w, a1) / 40.0f * R;
      int x0 = cx + (int)lroundf(r0 * sinf(a0 * ABS_D2R)), y0 = cy - (int)lroundf(r0 * cosf(a0 * ABS_D2R));
      int x1 = cx + (int)lroundf(r1 * sinf(a1 * ABS_D2R)), y1 = cy - (int)lroundf(r1 * cosf(a1 * ABS_D2R));
      gfx_line(x0, y0, x1, y1, 1);
      if (!dashed) gfx_line(x0 + 1, y0, x1 + 1, y1, 1);
    }
}

static void skp_txt(char *s, float p) { if (p < 0 || p > 100.5f) strcpy(s, "-"); else sprintf(s, "%.0f", p); }

void draw_dpabs(void) {
  char s[64], a[8], b[8], c[8], d[8];
  header("ABS SKP");
  if (!DP || !abs_have) { nt(6, 40, "ABS SKP: no result (no memory?)"); return; }
  const AbsResult *r = ABS;
  const Vessel *v = &DP->v;
  /* the polar: 0 the bow, the limiting wind (1-min mean, m/s) from each direction; 10 20 30 40 m/s */
  const int cx = 100, cy = 112;
  const float R = 86;
  for (int k = 1; k <= 4; k++) {
    int rr = (int)(R * k / 4);
    for (int t = 0; t < 360; t += k == 4 ? 3 : 6) gfx_pixel(cx + (int)lroundf(rr * sinf(t * ABS_D2R)), cy - (int)lroundf(rr * cosf(t * ABS_D2R)), 1);
    sprintf(s, "%d", k * 10);
    draw_text(&TINY, cx - (int)(rr * 0.7071f) - 10, cy + (int)(rr * 0.7071f) - 4, s, 0);
  }
  gfx_line(cx, cy - (int)R, cx, cy + (int)R, 1);
  gfx_line(cx - (int)R, cy, cx + (int)R, cy, 1);
  draw_text(&TINY, cx - 96, cy - (int)R + 2, "0 = BOW", 0);
  draw_text(&TINY, cx + (int)R - 12, cy + 2, "90", 0);
  gfx_rect(cx - 2, cy - 7, 5, 14, 1);                    /* the vessel, the bow up */
  gfx_line(cx - 2, cy - 7, cx, cy - 11, 1); gfx_line(cx + 2, cy - 7, cx, cy - 11, 1);
  abs_curve(cx, cy, R, r->intact, 0);
  const float *wcf = r->d >= 0 ? r->group[r->d_grp] : r->single[r->c_thr];
  abs_curve(cx, cy, R, wcf, 1);
  /* today's wind from its direction relative to the heading on DP OPS */
  float rel = fmodf(DP->w.wfrom - DP->r.hdg + 720, 360);
  int ex = cx + (int)lroundf((R + 6) * sinf(rel * ABS_D2R)), ey = cy - (int)lroundf((R + 6) * cosf(rel * ABS_D2R));
  int ix = cx + (int)lroundf((R - 26) * sinf(rel * ABS_D2R)), iy = cy - (int)lroundf((R - 26) * cosf(rel * ABS_D2R));
  gfx_line(ex, ey, ix, iy, 1); gfx_line(ex + 1, ey, ix + 1, iy, 1);
  gfx_disc(ix, iy, 2, 1);
  draw_text(&TINY, ex + (ex >= cx ? 2 : -8), ey - 4, "W", 0);
  /* the panel */
  const int X = 208;
  int y = 17;
  nt(X, y, "ABS SKP  1-min wind"); y += 17;
  skp_txt(a, r->a); skp_txt(b, r->b); skp_txt(c, r->c); skp_txt(d, r->d);
  snprintf(s, sizeof s, "(%s, %s, %s, %s, %.1f, NS)", a, b, c, d, abs_cur);
  nt_inv(X + 2, y, s); y += 18;
  snprintf(s, sizeof s, "a intact          %s %%", a); draw_text(&TINY, X, y, s, 0); y += LH;
  snprintf(s, sizeof s, "b least  %-7.7s  %s %%", v->thr[r->b_thr].name, b); draw_text(&TINY, X, y, s, 0); y += LH;
  snprintf(s, sizeof s, "c most   %-7.7s  %s %%", v->thr[r->c_thr].name, c); draw_text(&TINY, X, y, s, 0); y += LH;
  if (r->d >= 0) snprintf(s, sizeof s, "d worst failure G%d  %s %%", r->d_grp, d); else strcpy(s, "d worst failure: no groups");
  draw_text(&TINY, X, y, s, 0); y += LH;
  snprintf(s, sizeof s, "e current %.1f kn%s", abs_cur, abs_cur10 < 0 ? " (today's)" : ""); draw_text(&TINY, X, y, s, 0); y += LH;
  snprintf(s, sizeof s, "f North Sea table   margin %d %%", abs_margin); draw_text(&TINY, X, y, s, 0); y += LH + 4;
  gfx_rect(X, y + 4, 18, 2, 1); draw_text(&TINY, X + 22, y, "intact", 0);
  for (int i = 0; i < 18; i += 6) gfx_rect(X + 80 + i, y + 4, 3, 1, 1);
  draw_text(&TINY, X + 102, y, "worst failure", 0); y += LH + 6;
  snprintf(s, sizeof s, "TODAY %03.0f%s %.0fkn  HDG %03.0f", DP->w.wfrom, NT_DEG, DP->w.wkn, DP->r.hdg); nt(X, y, s); y += 16;
  snprintf(s, sizeof s, "rel %03.0f: holds to %.0f / %.0f m/s", rel, abs_at(r->intact, rel), abs_at(wcf, rel));
  draw_text(&TINY, X, y + 3, s, 0); y += LH + 4;
  draw_text(&TINY, X, y, "TODAY: SET's wind, 10-min mean;", 0); y += LH;
  draw_text(&TINY, X, y, "the ABS limits are 1-min means", 0);
  draw_text(&TINY, 2, MENU_Y - LH, ". DNV: DP OPS  CUR 7  MARGIN 8  WIND 4 6, UP DOWN", 0);
}

void dpabs_key(int key) {
  if (key == KEY_7) { abs_cur10 = abs_cur10 < 0 ? 5 : abs_cur10 >= 30 ? -1 : abs_cur10 + 5; if (!abs_run()) go(PG_DP); }   /* today's, 0.5 .. 3 kn */
  else if (key == KEY_8) { abs_margin = abs_margin >= 30 ? 0 : abs_margin + 10; if (!abs_run()) go(PG_DP); }
  else if (key == KEY_4 || key == KEY_6 || key == KEY_UP || key == KEY_DOWN) {   /* today's wind: 4 6 its direction, UP DOWN its speed */
    float e0 = abs_cur;
    wind_step(key == KEY_4 ? -10 : key == KEY_6 ? 10 : 0, key == KEY_UP ? 1 : key == KEY_DOWN ? -1 : 0);
    dp_weather();                                          /* the marker moves; the polar too only if today's current (e) changed */
    abs_wind = 1;
    if (abs_cur10 < 0 && abs_current() != e0 && !abs_run()) go(PG_DP);
  }
  else if (key == KEY_DOT || key == KEY_EXIT) {
    go(PG_DP);
    if (abs_wind) { abs_wind = 0; DP->manual ? dp_eval_here() : dp_best(); }   /* DP OPS with the new wind */
  }
}

/* INPUT: the weather (the wind is SET's / FLOW's own), the current (FLOW or typed), the sea (DNV or typed),
   the standoff and the tolerance (saved to DPOPS.TXT) */
enum { DI_WDIR, DI_WKN, DI_CAUTO, DI_CSET, DI_CKN, DI_SAUTO, DI_SDIR, DI_HS, DI_TP, DI_OFF, DI_TOL, DI_N };
static const char *const DI_NAME[DI_N] = { "WIND FROM", "WIND KN", "CURRENT", "CUR SET TO", "CUR KN", "SEA", "SEA FROM",
                                           "HS m", "TP s", "STANDOFF m", "HEADING +/-" };

void draw_dpin(void) {
  header("DP INPUT");
  if (!DP) return;
  dp_weather();
  Cfg *c = &N->cfg;
  DpWx *w = &DP->w;
  char s[48];
  for (int i = 0; i < DI_N; i++) {
    int y = 18 + 16 * i;
    switch (i) {
    case DI_WDIR: sprintf(s, "%03.0f" NT_DEG, w->wfrom); break;
    case DI_WKN: sprintf(s, "%.1f kn", w->wkn); break;
    case DI_CAUTO: sprintf(s, "%s", c->dcauto ? "FLOW" : "TYPED"); break;
    case DI_CSET: sprintf(s, "%03.0f" NT_DEG "%s", w->cset, c->dcauto ? "  (FLOW)" : ""); break;
    case DI_CKN: sprintf(s, "%.2f kn%s", w->ckn, c->dcauto ? "  (FLOW)" : ""); break;
    case DI_SAUTO: sprintf(s, "%s", c->dsauto ? "DNV, FROM THE WIND" : "TYPED"); break;
    case DI_SDIR: sprintf(s, "%03.0f" NT_DEG, w->vfrom); break;
    case DI_HS: sprintf(s, "%.1f", w->hs); break;
    case DI_TP: sprintf(s, "%.1f", w->tp); break;
    case DI_OFF: sprintf(s, "%.0f", DP->p.standoff); break;
    default: sprintf(s, "%.0f" NT_DEG, DP->p.tol); break;
    }
    if (i == DP->sel) { gfx_rect(0, y - 1, 140, 16, 1); nt_inv(4, y, DI_NAME[i]); } else nt(4, y, DI_NAME[i]);
    if (DP->edit == i) { char e[24]; sprintf(e, "%s_", ebuf); nt_inv(150, y, e); } else nt(150, y, s);
  }
  draw_text(&TINY, 260, 70, DP->csrc, 0);
  draw_text(&TINY, 2, MENU_Y - LH, "8 2 field  ENTER type  0 FLOW / DNV or typed  EXIT DP OPS", 0);
}

static void dpin_commit(int f) {
  if (!elen) return;
  double v = atof(ebuf);
  Cfg *c = &N->cfg;
  switch (f) {
  case DI_WDIR: if (v >= 0 && v <= 360) c->wdir = fmod(v, 360); break;
  case DI_WKN: if (v >= 0 && v < 100) c->wkn = v; break;
  case DI_CSET: if (v >= 0 && v <= 360) { c->dcset = fmod(v, 360); c->dcauto = 0; } break;
  case DI_CKN: if (v >= 0 && v < 10) { c->dckn = v; c->dcauto = 0; } break;
  case DI_SDIR: if (v >= 0 && v <= 360) { c->dsfrom = fmod(v, 360); c->dsauto = 0; } break;
  case DI_HS: if (v >= 0 && v < 20) { c->dhs = v; c->dsauto = 0; } break;
  case DI_TP: if (v >= 0 && v < 25) { c->dtp = v; c->dsauto = 0; } break;
  case DI_OFF: if (v >= 1 && v <= 500) { DP->p.standoff = (float)v; dp_save_plat(); } break;
  case DI_TOL: if (v >= 0 && v <= 90) { DP->p.tol = (float)v; dp_save_plat(); } break;
  }
  cfg_save();
}

void dpin_key(int key) {
  DpS *d = DP;
  if (!d) return;
  Cfg *c = &N->cfg;
  if (d->edit >= 0) {
    int dg = digit(key);
    if (key == KEY_ENTER) { dpin_commit(d->edit); d->edit = -1; }
    else if (key == KEY_EXIT) d->edit = -1;
    else if (key == KEY_BSP) { if (elen) ebuf[--elen] = 0; }
    else if (key == KEY_DOT && elen < 10 && !strchr(ebuf, '.')) { ebuf[elen++] = '.'; ebuf[elen] = 0; }
    else if (dg >= 0 && elen < 10) { ebuf[elen++] = (char)('0' + dg); ebuf[elen] = 0; }
    return;
  }
  if (key == KEY_8 || key == KEY_UP) d->sel = (d->sel + DI_N - 1) % DI_N;
  else if (key == KEY_2 || key == KEY_DOWN) d->sel = (d->sel + 1) % DI_N;
  else if (key == KEY_0 || (key == KEY_ENTER && (d->sel == DI_CAUTO || d->sel == DI_SAUTO))) {
    if (d->sel >= DI_CAUTO && d->sel <= DI_CKN) {             /* typed: start from what FLOW gave */
      if (c->dcauto) { c->dcset = d->w.cset; c->dckn = d->w.ckn; }
      c->dcauto ^= 1;
    } else if (d->sel >= DI_SAUTO && d->sel <= DI_TP) {
      if (c->dsauto) { c->dsfrom = d->w.vfrom; c->dhs = d->w.hs; c->dtp = d->w.tp; }
      c->dsauto ^= 1;
    }
    cfg_save();
  } else if (key == KEY_ENTER) { d->edit = d->sel; elen = 0; ebuf[0] = 0; }
  else if (key == KEY_EXIT) { go(PG_DP); dp_best(); }
}

int dp_editing(void) { return DP && DP->edit >= 0; }
