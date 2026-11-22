/* wpdb.c - NAV47: the waypoints' database and its page WPTS. See wpdb.h.
 * The file: a header "name,lat,lon", then a waypoint a line (the positions as the objects' file: 27.2 or 27 12.0N).
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#include <main.h>
#include <dmcp.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfx.h"
#include "ntext.h"
#include "objects.h"
#include "navctx.h"
#include "wpdb.h"

#define D2R  (M_PI / 180)
#define VIS  10
#define ROW0 34

typedef struct { double lat, lon; char name[WP_NAME]; } Wp;
typedef struct { short k; float nm, brg; } Row;            /* k >= 0 a waypoint, < 0 the object -k - 1 */

static Wp *W;
static int NW, cur, top, pick, sortn = 1, objs = 1, rows_n;
static Row *rows;
/* editing: 0 the name, 1 the latitude, 2 the longitude; group: the letters' group chosen (-1 none) */
static int edit = -1, eidx, elen, group = -1, enew;
static char ebuf[WP_NAME + 4];
static double elat;
static char msg[40];

static const char *const GROUPS[6] = { "ABCDE", "FGHIJ", "KLMNO", "PQRST", "UVWXY", "Z-/. " };
static const char *MENU[6] = { "ADD", "EDIT", "DEL", "SORT", "ROUTE", "BACK" };
static const char *EMENU[6];
static char emenu_txt[6][8];

/* ------------------------------------------------------------------ the file */
static void save(void) {
  if (!W || !sys_disk_ok()) return;
  char *b = (char *)malloc(24 + (size_t)NW * 48);
  if (!b) return;
  char *o = b;
  o += sprintf(o, "name,lat,lon\n");
  for (int i = 0; i < NW; i++) o += sprintf(o, "%s,%.7f,%.7f\n", W[i].name, W[i].lat, W[i].lon);
  sys_disk_write_enable(1);
  check_create_dir("/NAV47");
  FIL f;
  UINT bw;
  if (f_open(&f, WPDB_FILE, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) { f_write(&f, b, (UINT)strlen(b), &bw); f_close(&f); }
  sys_disk_write_enable(0);
  free(b);
}

static void clean_name(char *d, const char *s) {
  while (*s == ' ' || *s == '"') s++;
  int n = 0;
  for (; *s && *s != '"' && n < WP_NAME - 1; s++) d[n++] = (char)(*s >= 'a' && *s <= 'z' ? *s - 32 : *s);
  while (n && d[n - 1] == ' ') n--;
  d[n] = 0;
}

static void load(void) {
  NW = 0;
  if (!sys_disk_ok() || !file_exists(WPDB_FILE)) return;
  FIL f;
  if (f_open(&f, WPDB_FILE, FA_READ) != FR_OK) return;
  UINT n = (UINT)f_size(&f), br = 0;
  if (n > 40000) n = 40000;
  char *b = (char *)malloc(n + 1);
  if (!b) { f_close(&f); return; }
  f_read(&f, b, n, &br);
  f_close(&f);
  b[br] = 0;
  for (char *l = strtok(b, "\n"); l && NW < WPDB_MAX; l = strtok(NULL, "\n")) {
    char *c = strpbrk(l, ",;\t");
    if (!c) continue;
    *c = 0;
    if (!strcmp(l, "name") || !strcmp(l, "NAME") || l[0] == '#') continue;
    double la, lo;
    if (!geo_parse_pos(c + 1, &la, &lo)) continue;
    clean_name(W[NW].name, l);
    W[NW].lat = la; W[NW].lon = lo;
    NW++;
  }
  free(b);
}

int wpdb_init(void) {
  if (W) return 1;
  W = (Wp *)calloc(WPDB_MAX, sizeof *W);
  rows = (Row *)calloc(WPDB_MAX + OBJ_MAX, sizeof *rows);
  if (!W || !rows) return 0;
  load();
  return 1;
}

int wpdb_count(void) { return NW; }

static int unique_name(char *out) {
  for (int k = NW + 1; k < 1000; k++) {
    sprintf(out, "WP%03d", k);
    int used = 0;
    for (int i = 0; i < NW; i++) if (!strcmp(W[i].name, out)) used = 1;
    if (!used) return k;
  }
  strcpy(out, "WP");
  return 0;
}

int wpdb_add(const char *name, double lat, double lon) {
  if (!wpdb_init() || NW >= WPDB_MAX) return -1;
  Wp *w = &W[NW];
  if (name && *name) clean_name(w->name, name); else unique_name(w->name);
  w->lat = lat; w->lon = lon;
  NW++;
  save();
  return NW - 1;
}

/* ------------------------------------------------------------------ the list */
static void build(void) {
  double la, lo;
  nav_here(&la, &lo);
  rows_n = 0;
  for (int i = 0; i < NW; i++) rows[rows_n++] = (Row){ (short)i, 0, 0 };
  if (objs) for (int i = 0; i < obj_count(); i++) rows[rows_n++] = (Row){ (short)(-i - 1), 0, 0 };
  for (int i = 0; i < rows_n; i++) {
    double a, b;
    if (rows[i].k >= 0) { a = W[rows[i].k].lat; b = W[rows[i].k].lon; }
    else { const Obj *o = obj_get(-rows[i].k - 1); a = o->lat; b = o->lon; }
    double dl = fmod(b - lo + 540.0, 360.0) - 180.0, e = dl * cos((a + la) / 2 * D2R), n = a - la;
    rows[i].nm = (float)(60 * hypot(e, n));
    rows[i].brg = (float)fmod(atan2(e, n) / D2R + 360.0, 360.0);
  }
  for (int i = 1; i < rows_n; i++)                         /* by distance, or by name (the objects after) */
    for (int j = i; j > 0; j--) {
      Row *p = &rows[j - 1], *q = &rows[j];
      int sw;
      if (sortn) {
        const char *np = p->k >= 0 ? W[p->k].name : obj_get(-p->k - 1)->name, *nq = q->k >= 0 ? W[q->k].name : obj_get(-q->k - 1)->name;
        sw = (p->k < 0) != (q->k < 0) ? p->k < 0 : strcmp(np, nq) > 0;
      } else sw = p->nm > q->nm;
      if (!sw) break;
      Row t = *p; *p = *q; *q = t;
    }
  if (cur >= rows_n) cur = rows_n - 1;
  if (cur < 0) cur = 0;
}

void wpdb_open(int pk) {
  wpdb_init();
  pick = pk; edit = -1; group = -1; msg[0] = 0;
  build();
}

static void row_pos(const Row *r, double *la, double *lo, const char **name) {
  if (r->k >= 0) { *la = W[r->k].lat; *lo = W[r->k].lon; *name = W[r->k].name; }
  else { const Obj *o = obj_get(-r->k - 1); *la = o->lat; *lo = o->lon; *name = o->name; }
}

static void fmt_ll(char *s, double v, int lat) {
  double a = fabs(v);
  int d = (int)a;
  double m = (a - d) * 60;
  if (m >= 59.95) { d++; m = 0; }
  sprintf(s, lat ? "%02d" NT_DEG "%04.1f%c" : "%03d" NT_DEG "%04.1f%c", d, m, lat ? (v < 0 ? 'S' : 'N') : (v < 0 ? 'W' : 'E'));
}

void wpdb_draw(void) {
  char s[64], a[24], b[24];
  sprintf(s, pick ? "WPTS: PICK" : "WPTS %d", NW);
  nav_header(s);
  nt(4, 17, "NAME"); nt_r(190, 17, "LAT"); nt_r(290, 17, "LON"); nt_r(396, 17, "BRG   NM");
  gfx_rect(0, 32, LCD_X, 1, 1);
  if (!rows_n) {
    nt(6, 48, "No waypoints yet.");
    nt(6, 68, "ADD: the position now (GPS or SET),");
    nt(6, 84, "then its name: F1-F6 a group of letters,");
    nt(6, 100, "F1-F5 the letter; the digits as they are;");
    nt(6, 116, "ENTER, then LAT and LON (ENTER: as they are).");
    nt(6, 140, "Or /NAV47/WAYPTS.CSV: name, lat, lon.");
    return;
  }
  if (cur < top) top = cur;
  if (cur >= top + VIS) top = cur - VIS + 1;
  for (int i = top; i < rows_n && i < top + VIS; i++) {
    int y = ROW0 + (i - top) * 16;
    double la, lo;
    const char *nm;
    row_pos(&rows[i], &la, &lo, &nm);
    int editing = edit >= 0 && i == cur;
    if (editing && edit == 0) sprintf(s, "%s_", ebuf); else snprintf(s, sizeof s, "%s%s", nm, rows[i].k < 0 ? "*" : "");
    if (i == cur) nt_inv(4, y, s); else nt(4, y, s);
    if (editing && edit == 1) { sprintf(a, "%s_", ebuf); nt_r(190, y, a); }
    else { fmt_ll(a, editing && edit == 2 ? elat : la, 1); nt_r(190, y, a); }
    if (editing && edit == 2) { sprintf(b, "%s_", ebuf); nt_r(290, y, b); }
    else { fmt_ll(b, lo, 0); nt_r(290, y, b); }
    sprintf(s, "%03.0f" NT_DEG " %6.1f", rows[i].brg, rows[i].nm);
    nt_r(396, y, s);
  }
  const char *h;
  if (edit == 0) h = group < 0 ? "NAME: F1-F6 letters  digits  <- del  ENTER" : "the letter: F1-F5  F6 the groups";
  else if (edit == 1) h = "LAT DD.MMm  +/- SOUTH  ENTER (empty: as is)";
  else if (edit == 2) h = "LON DDD.MMm  +/- WEST  ENTER";
  else if (msg[0]) h = msg;
  else h = pick ? "ENTER: into the ROUTE  0 sort  1 objects  EXIT" : "ENTER edit  0 sort  1 objects*  ROUTE: insert";
  draw_text(&TINY, 2, 199, h, 0);
  sprintf(s, "%s%s", sortn ? "by name" : "nearest", objs ? " +obj" : "");
  draw_text_r(&TINY, LCD_X - 2, 199, s, 0);
}

const char *const *wpdb_menu(void) {
  if (edit == 0) {
    for (int i = 0; i < 6; i++) {
      if (group < 0) EMENU[i] = GROUPS[i];
      else if (i < 5) { char c = GROUPS[group][i]; emenu_txt[i][0] = c == ' ' ? '_' : c; emenu_txt[i][1] = 0; EMENU[i] = emenu_txt[i]; }
      else EMENU[i] = "ABC..";
    }
    return EMENU;
  }
  if (edit > 0) { static const char *E[6] = { "", "", "", "", "", "CANCEL" }; return E; }
  MENU[4] = pick ? "PICK" : "ROUTE";
  return MENU;
}

static void start_edit(int fresh) {
  if (!rows_n || rows[cur].k < 0) { strcpy(msg, "an object: edit it in OBJECTS.CSV"); return; }
  eidx = rows[cur].k; edit = 0; group = -1; enew = fresh;
  if (fresh) ebuf[0] = 0; else strcpy(ebuf, W[eidx].name);   /* new: typed from empty (ENTER keeps WPnnn) */
  elen = (int)strlen(ebuf);
}

static void del(int k) {
  memmove(&W[k], &W[k + 1], sizeof *W * (size_t)(NW - k - 1));
  NW--;
}

static int num_key(int key) {
  static const int dk[10] = { KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9 };
  for (int i = 0; i < 10; i++) if (key == dk[i]) return i;
  return -1;
}

static double dm(const char *s) {                         /* DD.MMm -> degrees */
  double v = atof(s), a = fabs(v);
  double d = floor(a), m = (a - d) * 100;
  return (v < 0 ? -1 : 1) * (d + m / 60);
}

static int edit_key(int key) {
  int d = num_key(key);
  if (key == KEY_EXIT || (edit > 0 && key == KEY_F6)) {
    if (enew) { del(eidx); save(); }
    edit = -1; build(); return 1;
  }
  if (edit == 0) {
    if (key >= KEY_F1 && key <= KEY_F6) {
      int f = key - KEY_F1;
      if (group < 0) group = f;
      else if (f == 5) group = -1;
      else { if (elen < WP_NAME - 1) { ebuf[elen++] = GROUPS[group][f]; ebuf[elen] = 0; } group = -1; }
      return 1;
    }
    if (d >= 0 && elen < WP_NAME - 1) { ebuf[elen++] = (char)('0' + d); ebuf[elen] = 0; return 1; }
    if (key == KEY_DOT && elen < WP_NAME - 1) { ebuf[elen++] = '.'; ebuf[elen] = 0; return 1; }
    if (key == KEY_BSP) { if (elen) ebuf[--elen] = 0; return 1; }
    if (key == KEY_ENTER) {
      if (elen) clean_name(W[eidx].name, ebuf);
      edit = 1; elen = 0; ebuf[0] = 0; elat = W[eidx].lat;
      return 1;
    }
    return 1;
  }
  if (key == KEY_ENTER) {
    double v = dm(ebuf);
    if (edit == 1) { if (elen && fabs(v) <= 90) elat = v; edit = 2; elen = 0; ebuf[0] = 0; return 1; }
    W[eidx].lat = elat;
    if (elen && fabs(v) <= 180) W[eidx].lon = v;
    edit = -1; enew = 0; save(); build();
    for (int i = 0; i < rows_n; i++) if (rows[i].k == eidx) cur = i;
    return 1;
  }
  if (key == KEY_BSP) { if (elen) ebuf[--elen] = 0; return 1; }
  if (key == KEY_CHS) { if (ebuf[0] == '-') { memmove(ebuf, ebuf + 1, (size_t)elen); elen--; } else if (elen < 12) { memmove(ebuf + 1, ebuf, (size_t)elen + 1); ebuf[0] = '-'; elen++; } return 1; }
  if (key == KEY_DOT && elen < 12 && !strchr(ebuf, '.')) { ebuf[elen++] = '.'; ebuf[elen] = 0; return 1; }
  if (d >= 0 && elen < 12) { ebuf[elen++] = (char)('0' + d); ebuf[elen] = 0; }
  return 1;
}

static void to_route(void) {
  if (!rows_n) return;
  double la, lo;
  const char *nm;
  row_pos(&rows[cur], &la, &lo, &nm);
  nav_route_insert(la, lo, nm);
  snprintf(msg, sizeof msg, "%s: into the ROUTE", nm);
}

int wpdb_key(int key) {
  if (!W) return 0;
  if (edit >= 0) return edit_key(key);
  msg[0] = 0;
  if (key >= KEY_F1 && key <= KEY_F6) {
    switch (key - KEY_F1) {
    case 0: {                                             /* ADD: the position now */
      double la, lo;
      int g = nav_here(&la, &lo);
      int k = wpdb_add(NULL, la, lo);
      if (k < 0) { strcpy(msg, "the list is full"); return 1; }
      sortn = 1; build();
      for (int i = 0; i < rows_n; i++) if (rows[i].k == k) cur = i;
      start_edit(1);
      if (!g) strcpy(msg, "SET's position");
      return 1;
    }
    case 1: start_edit(0); return 1;
    case 2:
      if (rows_n && rows[cur].k >= 0) { del(rows[cur].k); save(); build(); }
      else strcpy(msg, "an object: in OBJECTS.CSV");
      return 1;
    case 3: sortn ^= 1; build(); return 1;
    case 4: to_route(); return pick ? 2 : 1;
    case 5: return 2;
    }
  }
  if (key == KEY_8 || key == KEY_UP) { if (cur > 0) cur--; return 1; }
  if (key == KEY_2 || key == KEY_DOWN) { if (cur < rows_n - 1) cur++; return 1; }
  if (key == KEY_4) { cur -= VIS - 1; if (cur < 0) cur = 0; return 1; }
  if (key == KEY_6) { cur += VIS - 1; if (cur > rows_n - 1) cur = rows_n - 1; return 1; }
  if (key == KEY_0) { sortn ^= 1; build(); return 1; }
  if (key == KEY_1) { objs ^= 1; cur = 0; build(); return 1; }
  if (key == KEY_ENTER) { if (pick) { to_route(); return 2; } start_edit(0); return 1; }
  if (key == KEY_EXIT) return 2;
  return 0;
}
