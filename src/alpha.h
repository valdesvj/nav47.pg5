/* alpha.h - NAV47: a name typed on the DM42n, HP-42S style (WPTS's, and a route's name): F1-F6 a group of letters,
 * then F1-F5 the letter (F6 the groups again); the digits and . as they are; <- deletes; ENTER done; EXIT cancels.
 * GPL-3.0-or-later. */
#ifndef NAV47_ALPHA_H
#define NAV47_ALPHA_H

typedef struct { char buf[16]; int len, max, group, fname; } Alpha;   /* fname: a file name (letters, digits, - only) */

void alpha_start(Alpha *a, const char *init, int max, int fname);
int  alpha_key(Alpha *a, int key);            /* 0 typing (used), 1 ENTER, -1 EXIT, 2 not a key of its */
const char *const *alpha_menu(const Alpha *a);   /* the soft keys' labels now */
const char *alpha_help(const Alpha *a);
#endif
