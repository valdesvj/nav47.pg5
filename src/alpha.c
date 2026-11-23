/* alpha.c - NAV47: a name typed HP-42S style. See alpha.h. Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#include <main.h>
#include <dmcp.h>
#include <string.h>
#include "alpha.h"

static const char *const GROUPS[6] = { "ABCDE", "FGHIJ", "KLMNO", "PQRST", "UVWXY", "Z-/. " };
static const char *LAB[6];
static char lab[6][4];

void alpha_start(Alpha *a, const char *init, int max, int fname) {
  if (max > (int)sizeof a->buf - 1) max = (int)sizeof a->buf - 1;
  a->max = max; a->group = -1; a->fname = fname;
  strncpy(a->buf, init ? init : "", (size_t)max); a->buf[max] = 0;
  a->len = (int)strlen(a->buf);
}

static void put(Alpha *a, char c) {
  if (a->fname && !((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-')) return;   /* 8.3: no / . space */
  if (a->len < a->max) { a->buf[a->len++] = c; a->buf[a->len] = 0; }
}

int alpha_key(Alpha *a, int key) {
  static const int dk[10] = { KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9 };
  if (key >= KEY_F1 && key <= KEY_F6) {
    int f = key - KEY_F1;
    if (a->group < 0) a->group = f;
    else if (f == 5) a->group = -1;
    else { put(a, GROUPS[a->group][f]); a->group = -1; }
    return 0;
  }
  for (int i = 0; i < 10; i++) if (key == dk[i]) { put(a, (char)('0' + i)); return 0; }
  if (key == KEY_DOT) { put(a, '.'); return 0; }
  if (key == KEY_BSP) { if (a->len) a->buf[--a->len] = 0; return 0; }
  if (key == KEY_ENTER) return 1;
  if (key == KEY_EXIT) return -1;
  return 2;
}

const char *const *alpha_menu(const Alpha *a) {
  for (int i = 0; i < 6; i++) {
    if (a->group < 0) LAB[i] = GROUPS[i];
    else if (i < 5) { char c = GROUPS[a->group][i]; lab[i][0] = c == ' ' ? '_' : c; lab[i][1] = 0; LAB[i] = lab[i]; }
    else LAB[i] = "ABC..";
  }
  return LAB;
}

const char *alpha_help(const Alpha *a) {
  return a->group < 0 ? "F1-F6 letters  digits  <- del  ENTER  EXIT" : "the letter: F1-F5  F6 the groups";
}
