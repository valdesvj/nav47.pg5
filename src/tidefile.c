/* tidefile.c - NAV47: tide station files (.TID) and FLOW atlas files (.FLW) for the TIDE FILES menu.
 * The files are written by tools/gen_tides.py files (from Tide 47's public or private data) and copied to
 * /NAV47/TIDES/ on the calculator disk. Plain text, one keyword a line:
 *
 *   NAV47 TIDES 1                 NAV47 FLOW 1
 *   REGION AR ARAMCO: private     REF Ras Tanura
 *   STATION Ras Tanura            SOURCE Arabian Gulf Tidal Flow
 *   KIND TIDE | RECT | ROT        GRID lat0 lon0 step_min nlat nlon q
 *   POS lat lon                   CELL index u1 v1 ... u13 v13   (sea cells only)
 *   TZ hours US|-                 END
 *   DATUM LAT   SRC text
 *   Z0 m   V0 kn   FLOOD deg   EBB deg
 *   HC name H g   (HV name H g for the north component of a rotary stream)
 *   END
 *
 * The parser works on a text buffer (no file I/O here) and allocates with malloc: the same code runs on the DM42n
 * and in the PC tests. Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tide.h"
#include "tidefile.h"

static void snprintf_line(char *err, int n, const char *msg, int line) { snprintf(err, (size_t)n, "line %d: %s", line, msg); }

static char *dupstr(const char *s) {
  size_t n = strlen(s) + 1;
  char *d = (char *)malloc(n);
  if (d) memcpy(d, s, n);
  return d;
}

static char *next_line(char **p) {
  char *s = *p;
  if (!s || !*s) return NULL;
  char *e = s;
  while (*e && *e != '\n') e++;
  if (*e) { *e = 0; *p = e + 1; } else *p = e;
  size_t n = strlen(s);
  while (n && (s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
  while (*s == ' ' || *s == '\t') s++;
  return s;
}

static const char *after(const char *line, const char *kw) {
  size_t n = strlen(kw);
  if (strncmp(line, kw, n) || (line[n] != ' ' && line[n] != 0)) return NULL;
  line += n;
  while (*line == ' ') line++;
  return line;
}

static int con_index(const char *name) {
  for (int k = 0; k < NCONS; k++) if (!strcmp(CON_NAME[k], name)) return k;
  return -1;
}

static int cmp_tcon(const void *a, const void *b) { return ((const TCon *)a)->k - ((const TCon *)b)->k; }

/* Parses a .TID text (modified in place). Appends one TRegion to *regs (realloc'd). Returns stations read, -1 error */
int tid_parse(char *text, TRegion **regs, int *nregs, char *err, int errlen) {
  char *p = text, *l;
  int nst = 0, cap = 0;
  TStation *st = NULL, *cur = NULL;
  TCon hc[NCONS], hv[NCONS];
  int nhc = 0, nhv = 0, line = 0;
  char *code = NULL, *title = NULL;
#define FAIL(msg) do { if (err) { snprintf_line(err, errlen, msg, line); } goto fail; } while (0)
  while ((l = next_line(&p)) != NULL) {
    line++;
    const char *v;
    if (!*l || *l == '#') continue;
    if (line == 1 || !strncmp(l, "NAV47 ", 6)) {
      if (strncmp(l, "NAV47 TIDES", 11)) FAIL("not a NAV47 TIDES file");
      continue;
    }
    if ((v = after(l, "REGION"))) {
      const char *sp = strchr(v, ' ');
      if (sp) { code = (char *)malloc((size_t)(sp - v) + 1); memcpy(code, v, (size_t)(sp - v)); code[sp - v] = 0; title = dupstr(sp + 1); }
      else { code = dupstr(v); title = dupstr(v); }
    } else if ((v = after(l, "STATION"))) {
      if (nst == cap) {
        cap = cap ? cap * 2 : 8;
        TStation *ns = (TStation *)realloc(st, (size_t)cap * sizeof(TStation));
        if (!ns) FAIL("out of memory");
        st = ns;
      }
      cur = &st[nst];
      memset(cur, 0, sizeof *cur);
      cur->name = dupstr(v);
      cur->datum = "";
      cur->src = "";
      nhc = nhv = 0;
    } else if (!cur) {
      FAIL("data before STATION");
    } else if ((v = after(l, "KIND"))) {
      cur->kind = !strcmp(v, "RECT") ? TK_RECT : !strcmp(v, "ROT") ? TK_ROT : TK_TIDE;
    } else if ((v = after(l, "POS"))) {
      char *e;
      cur->lat = strtod(v, &e);
      cur->lon = strtod(e, NULL);
    } else if ((v = after(l, "TZ"))) {
      char *e;
      cur->tz = strtod(v, &e);
      while (*e == ' ') e++;
      cur->dst_us = !strncmp(e, "US", 2);
    } else if ((v = after(l, "DATUM"))) cur->datum = dupstr(strcmp(v, "-") ? v : "");
    else if ((v = after(l, "SRC"))) cur->src = dupstr(strcmp(v, "-") ? v : "");
    else if ((v = after(l, "Z0"))) cur->z0 = strtod(v, NULL);
    else if ((v = after(l, "V0"))) cur->v0 = strtod(v, NULL);
    else if ((v = after(l, "FLOOD"))) cur->flood = strtod(v, NULL);
    else if ((v = after(l, "EBB"))) cur->ebb = strtod(v, NULL);
    else if ((v = after(l, "HC")) || (v = after(l, "HV"))) {
      int isv = l[1] == 'V';
      char name[12];
      int i = 0;
      while (*v && *v != ' ' && i < 11) name[i++] = *v++;
      name[i] = 0;
      int k = con_index(name);
      if (k < 0) FAIL("unknown constituent");
      char *e;
      double H = strtod(v, &e), g = strtod(e, NULL);
      TCon *t = isv ? &hv[nhv++] : &hc[nhc++];
      if ((isv ? nhv : nhc) > NCONS) FAIL("too many constituents");
      t->k = (unsigned char)k; t->H = H; t->g = g;
    } else if (!strcmp(l, "END")) {
      qsort(hc, (size_t)nhc, sizeof(TCon), cmp_tcon);
      qsort(hv, (size_t)nhv, sizeof(TCon), cmp_tcon);
      TCon *a = nhc ? (TCon *)malloc((size_t)nhc * sizeof(TCon)) : NULL;
      TCon *b = nhv ? (TCon *)malloc((size_t)nhv * sizeof(TCon)) : NULL;
      if ((nhc && !a) || (nhv && !b)) FAIL("out of memory");
      if (a) memcpy(a, hc, (size_t)nhc * sizeof(TCon));
      if (b) memcpy(b, hv, (size_t)nhv * sizeof(TCon));
      cur->hc = a; cur->nhc = nhc; cur->hv = b; cur->nhv = nhv;
      nst++;
      cur = NULL;
    }
  }
  if (!nst) FAIL("no stations");
  {
    TRegion *nr = (TRegion *)realloc(*regs, (size_t)(*nregs + 1) * sizeof(TRegion));
    if (!nr) FAIL("out of memory");
    *regs = nr;
    nr[*nregs].code = code ? code : "FILE";
    nr[*nregs].title = title ? title : "FILE";
    nr[*nregs].n = nst;
    nr[*nregs].st = st;
    (*nregs)++;
  }
  return nst;
fail:
  return -1;            /* (a failed parse leaks a little heap; the calculator reloads rarely) */
#undef FAIL
}

/* Parses a .FLW text into *fl (cells malloc'd). Returns sea cells, -1 error */
int flw_parse(char *text, FlowAtlas *fl, char *err, int errlen) {
  char *p = text, *l;
  int line = 0, sea = 0;
  unsigned char *cells = NULL;
  memset(fl, 0, sizeof *fl);
#define FAIL(msg) do { if (err) { snprintf_line(err, errlen, msg, line); } goto fail; } while (0)
  while ((l = next_line(&p)) != NULL) {
    line++;
    const char *v;
    if (!*l || *l == '#') continue;
    if (line == 1) { if (strncmp(l, "NAV47 FLOW", 10)) FAIL("not a NAV47 FLOW file"); continue; }
    if ((v = after(l, "REF"))) fl->ref = dupstr(v);
    else if ((v = after(l, "SOURCE"))) fl->source = dupstr(v);
    else if ((v = after(l, "GRID"))) {
      char *e;
      fl->lat0 = strtod(v, &e);
      fl->lon0 = strtod(e, &e);
      fl->step_min = (int)strtol(e, &e, 10);
      fl->nlat = (int)strtol(e, &e, 10);
      fl->nlon = (int)strtol(e, &e, 10);
      fl->q = strtod(e, NULL);
      if (fl->nlat < 2 || fl->nlon < 2 || fl->nlat * fl->nlon > 20000 || fl->step_min <= 0) FAIL("bad GRID");
      cells = (unsigned char *)calloc((size_t)(fl->nlat * fl->nlon) * 26, 1);
      if (!cells) FAIL("out of memory");
    } else if ((v = after(l, "CELL"))) {
      if (!cells) FAIL("CELL before GRID");
      char *e;
      long k = strtol(v, &e, 10);
      if (k < 0 || k >= fl->nlat * fl->nlon) FAIL("bad CELL index");
      for (int i = 0; i < 26; i++) {
        long c = strtol(e, &e, 10);
        if (c < 1 || c > 99) FAIL("bad CELL code");
        cells[26 * k + i] = (unsigned char)c;
      }
      sea++;
    } else if (!strcmp(l, "END")) break;
  }
  if (!cells) FAIL("no GRID");
  if (!fl->ref) fl->ref = "";
  if (!fl->source) fl->source = "";
  fl->cells = cells;
  return sea;
fail:
  free(cells);
  fl->cells = NULL;
  return -1;
#undef FAIL
}
