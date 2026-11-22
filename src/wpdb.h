/* wpdb.h - NAV47: the waypoints' database, /NAV47/WAYPTS.CSV (name, lat, lon), and its page WPTS: add (the position
 * now), edit (the name HP-42S style: F1-F6 a letter group, then the letter), delete, sorted by name or the nearest,
 * your objects listed too; a waypoint into the ROUTE after its cursor. GPL-3.0-or-later. */
#ifndef NAV47_WPDB_H
#define NAV47_WPDB_H

#define WPDB_FILE "/NAV47/WAYPTS.CSV"
#define WPDB_MAX  250
#define WP_NAME   13                          /* 12 characters */

int  wpdb_init(void);                         /* the list on the heap, the file read: 1 ok */
int  wpdb_count(void);
int  wpdb_add(const char *name, double lat, double lon);   /* the index, -1 full (saved) */
void wpdb_open(int pick);                     /* the page; pick: ENTER puts the waypoint into the ROUTE and goes back */
void wpdb_draw(void);
int  wpdb_key(int key);                       /* 0 not used, 1 used, 2 back (to where it was opened from) */
const char *const *wpdb_menu(void);           /* the soft keys now */

/* from src/main.c */
void nav_route_insert(double lat, double lon, const char *name);   /* after the ROUTE's cursor (saved) */
int  nav_here(double *lat, double *lon);      /* the position now: the GPS's (or DR), else SET's; 1 = from the GPS */

#endif
