/* route.c - passage planner for NAV47 (see route.h) */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "geo_route.h"

#define DEG (3.14159265358979323846 / 180.0)
#define CAPS (GEOD_LATITUDE | GEOD_LONGITUDE | GEOD_AZIMUTH | \
              GEOD_DISTANCE | GEOD_DISTANCE_IN)

typedef struct {
  char kind;                     /* SEG_GEOD / SEG_RHUMB / SEG_PARAL */
  double lat1, lon1;             /* start, lon1 unrolled */
  double azi1;                   /* initial course */
  double s;                      /* length (m) */
  struct geod_geodesicline l;    /* geodesic segments only */
} seg_t;

/* ~1.6 KB kept off the stack */
static seg_t segs[3];
static struct geod_geodesicline lwork;

static double norm360(double a)
{
  a = fmod(a, 360.0);
  return a < 0 ? a + 360.0 : a;
}

static double par_radius(const route_t *r, double lat)
{
  double sp = sin(lat * DEG);
  return r->g->a * cos(lat * DEG) / sqrt(1.0 - r->rh.e2 * sp * sp);
}

static void seg_pos(const route_t *r, const seg_t *sg, double s,
                    double *lat, double *lon, double *azi)
{
  if (sg->kind == SEG_GEOD) {
    geod_genposition(&sg->l, GEOD_LONG_UNROLL, s, lat, lon, azi,
                     0, 0, 0, 0, 0);
  } else {
    rhumb_direct(&r->rh, sg->lat1, sg->lon1, sg->azi1, s, lat, lon);
    *azi = sg->azi1;
  }
}

void route_init(route_t *r, const struct geod_geodesic *g)
{
  memset(r, 0, sizeof *r);
  r->g = g;
  rhumb_init(&r->rh, g);
}

static int emit(route_t *r, double lat, double lonu, double azi,
                char kind, uint8_t flags, double leg)
{
  wpt_t *w;
  if (r->n >= ROUTE_MAX_WPT) { r->full = 1; return ROUTE_EFULL; }
  w = &r->w[r->n];
  w->lat = lat;
  w->lon = remainder(lonu, 360.0);
  if (w->lon == 180.0) w->lon = -180.0;
  w->leg_m = leg;
  w->cum_m = r->n ? r->w[r->n - 1].cum_m + leg : 0.0;
  w->crs_in = isnan(azi) ? (double)NAN : norm360(azi);
  w->crs_out = w->crs_in;
  w->rl_crs = w->rl_m = NAN;
  w->kind = kind;
  w->flags = flags;
  r->n++;
  r->lon_u = lonu;
  return ROUTE_OK;
}

/* arc (deg, >= 0) from the line's start to its northern/southern vertex */
static double arc_to_vertex(const struct geod_geodesicline *l, int north)
{
  double sig1 = atan2(l->ssig1, l->csig1) / DEG;
  return norm360((north ? 90.0 : -90.0) - sig1);
}

int geod_vertex(const struct geod_geodesic *g, double lat1, double lon1,
                double lat2, double lon2, double *latv, double *lonv,
                double *sv)
{
  int k, inside = 0;
  double best = -1, a, la, lo, s;
  geod_inverseline(&lwork, g, lat1, lon1, lat2, lon2, CAPS);
  for (k = 0; k < 2; k++) {
    a = arc_to_vertex(&lwork, k == 0);
    geod_genposition(&lwork, GEOD_ARCMODE | GEOD_LONG_UNROLL, a,
                     &la, &lo, 0, &s, 0, 0, 0, 0);
    if (a <= lwork.a13) {
      if (!inside || fabs(la) > best) { best = fabs(la); *latv = la; *lonv = lo; *sv = s; }
      inside = 1;
    } else if (!inside && (la > 0) == (lat1 + lat2 > 0)) {
      *latv = la; *lonv = lo; *sv = s;
    }
  }
  *lonv = remainder(*lonv, 360.0);
  return inside;
}

/* tangent geodesic from (lat,lon) to the parallel latmax*sgn, heading
 * east (sgnE=+1) or west.  Fills l, returns the vertex point/distance. */
static void tangent(const route_t *r, struct geod_geodesicline *l,
                    double lat, double lon, double latmax, int sgn, int sgnE,
                    double *latv, double *lonv, double *sv)
{
  double f1 = 1.0 - r->g->f;
  double cbm = cos(atan(f1 * tan(latmax * DEG)));
  double cb = cos(atan(f1 * tan(lat * DEG)));
  double sa = cbm / cb, az, a;
  if (sa > 1) sa = 1;
  az = atan2(sgnE * sa, sgn * sqrt(1.0 - sa * sa)) / DEG;
  geod_lineinit(l, r->g, lat, lon, az, CAPS);
  a = arc_to_vertex(l, sgn > 0);
  geod_genposition(l, GEOD_ARCMODE | GEOD_LONG_UNROLL, a,
                   latv, lonv, 0, sv, 0, 0, 0, 0);
}

/* build the 1..3 segments of one user leg; returns count or error */
static int build(route_t *r, double lat1, double lon1, double lat2,
                 double lon2, sail_t sail, double latmax)
{
  double latv, lonv, sv, lon2u, la, lo, s;
  int sgn, sgnE;

  if (sail == SAIL_RHUMB) {
    seg_t *sg = &segs[0];
    sg->kind = SEG_RHUMB; sg->lat1 = lat1; sg->lon1 = lon1;
    if (rhumb_inverse(&r->rh, lat1, lon1, lat2, lon2, &sg->s, &sg->azi1))
      return ROUTE_EARG;
    return 1;
  }

  segs[0].kind = SEG_GEOD;
  geod_inverseline(&segs[0].l, r->g, lat1, lon1, lat2, lon2, CAPS);
  segs[0].lat1 = lat1; segs[0].lon1 = lon1;
  segs[0].azi1 = segs[0].l.azi1; segs[0].s = segs[0].l.s13;
  if (sail == SAIL_GEOD) return 1;

  /* composite */
  if (latmax <= 0 || latmax >= 90) return ROUTE_EARG;
  if (!geod_vertex(r->g, lat1, lon1, lat2, lon2, &latv, &lonv, &sv) ||
      fabs(latv) <= latmax)
    return 1;                                   /* limit not reached */
  if (fabs(lat1) > latmax || fabs(lat2) > latmax) return ROUTE_ELIMIT;

  sgn = latv > 0 ? 1 : -1;
  sgnE = segs[0].l.salp1 >= 0 ? 1 : -1;
  geod_genposition(&segs[0].l, GEOD_LONG_UNROLL, segs[0].l.s13,
                   0, &lon2u, 0, 0, 0, 0, 0, 0);

  /* leg A: P1 -> V1 */
  tangent(r, &segs[0].l, lat1, lon1, latmax, sgn, sgnE, &la, &lo, &s);
  segs[0].azi1 = segs[0].l.azi1; segs[0].s = s;
  /* leg C: V2 -> P2 (found backwards from P2) */
  tangent(r, &lwork, lat2, lon2u, latmax, sgn, -sgnE, &la, &lonv, &sv);
  if (sgnE * (lonv - lo) <= 0) return ROUTE_ECOMP;

  /* leg B: along the parallel */
  segs[1].kind = SEG_PARAL;
  segs[1].lat1 = sgn * latmax; segs[1].lon1 = lo;
  segs[1].azi1 = sgnE > 0 ? 90.0 : -90.0;
  segs[1].s = par_radius(r, latmax) * fabs(lonv - lo) * DEG;

  segs[2].kind = SEG_GEOD;
  geod_inverseline(&segs[2].l, r->g, sgn * latmax, lonv, lat2, lon2u, CAPS);
  segs[2].lat1 = sgn * latmax; segs[2].lon1 = lonv;
  segs[2].azi1 = segs[2].l.azi1; segs[2].s = segs[2].l.s13;
  return 3;
}

/* distance along sg where longitude == t (Newton on dlon/ds) */
static double find_lon(const route_t *r, const seg_t *sg, double t,
                       double lonA, double lonB)
{
  double s = sg->s * (t - lonA) / (lonB - lonA), lat, lon, azi, d;
  int i;
  for (i = 0; i < 12; i++) {
    seg_pos(r, sg, s, &lat, &lon, &azi);
    if (fabs(lon - t) < 1e-12) break;
    d = sin(azi * DEG) / par_radius(r, lat) / DEG;   /* deg per metre */
    if (d == 0) break;
    s -= (lon - t) / d;
    if (s < 0) s = 0;
    if (s > sg->s) s = sg->s;
  }
  return s;
}

int route_add_leg(route_t *r, double lat1, double lon1,
                  double lat2, double lon2, sail_t sail, double latmax,
                  split_t split, double param)
{
  int ns, i, rc = ROUTE_OK;
  double S = 0, off = 0, last = 0, step = 0, lat, lon, azi, t;

  if (fabs(lat2) > 90 || (r->n == 0 && fabs(lat1) > 90)) return ROUTE_EARG;
  if (r->n == 0) {
    emit(r, lat1, lon1, NAN, ' ', WPT_USER, 0);
  } else {
    lat1 = r->w[r->n - 1].lat;
    lon1 = r->lon_u;
  }
  ns = build(r, lat1, lon1, lat2, lon2, sail, latmax);
  if (ns < 0) return ns;
  for (i = 0; i < ns; i++) S += segs[i].s;

  if (split == SPLIT_DIST && param > 0) step = param;
  else if (split == SPLIT_LEGS && param >= 1) step = S / floor(param);

  for (i = 0; i < ns && rc == ROUTE_OK; i++) {
    seg_t *sg = &segs[i];
    double lonA, lonB, la2;
    r->w[r->n - 1].crs_out = norm360(sg->azi1);

    if (step > 0) {                         /* by distance / by count */
      double k = floor(off / step) + 1;
      for (t = k * step; t < off + sg->s - 1e-3 && rc == ROUTE_OK; t += step) {
        if (t <= off + 1e-3) continue;
        seg_pos(r, sg, t - off, &lat, &lon, &azi);
        rc = emit(r, lat, lon, azi, sg->kind, WPT_SPLIT, t - last);
        last = t;
      }
    } else if (split == SPLIT_DLON && param > 0) {   /* on whole meridians */
      lonA = sg->lon1;
      seg_pos(r, sg, sg->s, &la2, &lonB, &azi);
      if (fabs(lonB - lonA) > 1e-9) {
        double dir = lonB > lonA ? 1 : -1;
        double m = dir > 0 ? floor(lonA / param + 1e-9) + 1
                           : ceil(lonA / param - 1e-9) - 1;
        for (t = m * param; dir * (lonB - t) > 1e-9 && rc == ROUTE_OK;
             t += dir * param) {
          double s = find_lon(r, sg, t, lonA, lonB);
          seg_pos(r, sg, s, &lat, &lon, &azi);
          rc = emit(r, lat, lon, azi, sg->kind, WPT_SPLIT, off + s - last);
          last = off + s;
        }
      }
    }
    if (rc != ROUTE_OK) break;
    seg_pos(r, sg, sg->s, &lat, &lon, &azi);
    if (i == ns - 1) lat = lat2;            /* land exactly on the user point */
    rc = emit(r, lat, lon, azi, sg->kind,
              i == ns - 1 ? WPT_USER : WPT_VERTEX, off + sg->s - last);
    off += sg->s;
    last = off;
  }
  return rc;
}

void route_finish(route_t *r)
{
  int i;
  for (i = 0; i + 1 < r->n; i++) {
    double s, a;
    if (rhumb_inverse(&r->rh, r->w[i].lat, r->w[i].lon,
                      r->w[i + 1].lat, r->w[i + 1].lon, &s, &a) == RHUMB_OK) {
      r->w[i].rl_crs = norm360(a);
      r->w[i].rl_m = s;
    }
  }
  if (r->n) r->w[r->n - 1].crs_out = NAN;
}

/* ---------- formatting without printf("%f") (newlib-nano safe) ---------- */

static char *fx(char *p, double v, int width, int dec)
{
  long sc = 1, q, ip, fp;
  int i, n;
  char tmp[16];
  if (isnan(v)) { *p = 0; return p; }
  for (i = 0; i < dec; i++) sc *= 10;
  if (v < 0) { *p++ = '-'; v = -v; }
  q = (long)(v * sc + 0.5);
  ip = q / sc; fp = q % sc;
  n = sprintf(tmp, "%ld", ip);
  for (i = n; i < width; i++) *p++ = '0';
  memcpy(p, tmp, n); p += n;
  if (dec) {
    *p++ = '.';
    for (i = dec - 1; i >= 0; i--) { p[i] = (char)('0' + fp % 10); fp /= 10; }
    p += dec;
  }
  *p = 0;
  return p;
}

/* degrees + minutes with dec decimals; returns hemisphere letter */
static char dm_split(double v, int is_lat, int dec, long *deg, double *min)
{
  long sc = 1, q;
  int i;
  char h = is_lat ? (v < 0 ? 'S' : 'N') : (v < 0 ? 'W' : 'E');
  for (i = 0; i < dec; i++) sc *= 10;
  v = fabs(v);
  q = (long)(v * 60.0 * sc + 0.5);            /* total minutes * sc */
  *deg = q / (60 * sc);
  *min = (double)(q % (60 * sc)) / sc;
  return h;
}

void geo_fmt_dm(char *buf, double v, int is_lat, char degch)
{
  long d; double m;
  char h = dm_split(v, is_lat, 1, &d, &m);
  char *p = buf + sprintf(buf, is_lat ? "%02ld" : "%03ld", d);
  *p++ = degch ? degch : ' ';
  p = fx(p, m, 2, 1);
  *p++ = h; *p = 0;
}

static void nmea_put(route_put_fn put, void *ctx, char *s)
{
  unsigned char ck = 0;
  char *p;
  for (p = s + 1; *p; p++) ck ^= (unsigned char)*p;
  sprintf(p, "*%02X", ck);
  put(s, ctx);
}

static char *nmea_pos(char *p, double v, int is_lat)
{
  long d; double m;
  char h = dm_split(v, is_lat, 4, &d, &m);
  p += sprintf(p, is_lat ? "%02ld" : "%03ld", d);
  p = fx(p, m, 2, 4);
  *p++ = ','; *p++ = h; *p = 0;
  return p;
}

void route_nmea(const route_t *r, const char *id, route_put_fn put, void *ctx)
{
  char s[96], *p;
  int i, j, nsent, k, len;

  for (i = 0; i < r->n; i++) {                       /* $GPWPL */
    p = s + sprintf(s, "$GPWPL,");
    p = nmea_pos(p, r->w[i].lat, 1); *p++ = ',';
    p = nmea_pos(p, r->w[i].lon, 0);
    sprintf(p, ",WP%03d", i + 1);
    nmea_put(put, ctx, s);
  }
  /* $GPRTE: a sentence is at most 82 chars including "*hh\r\n", so the
   * body before '*' stays <= 77.  Capacity is fixed from a worst-case
   * header ("$GPRTE,99,99,c,ID") so the sentence count is known up front. */
  k = (77 - (int)(15 + strlen(id))) / 6;            /* names per sentence */
  if (k < 1) return;
  nsent = (r->n + k - 1) / k;
  for (i = 0, j = 1; i < r->n; j++) {
    int m;
    len = sprintf(s, "$GPRTE,%d,%d,c,%s", nsent, j, id);
    for (m = 0; m < k && i < r->n; m++, i++)
      len += sprintf(s + len, ",WP%03d", i + 1);
    nmea_put(put, ctx, s);
  }
}

void route_csv(const route_t *r, route_put_fn put, void *ctx)
{
  char s[160], *p;
  int i;
  put("WPT,LAT,LON,LAT_DM,LON_DM,LEG_NM,CUM_NM,CRS_IN,CRS_OUT,"
      "RL_CRS,RL_NM,SEG,FLAGS", ctx);
  for (i = 0; i < r->n; i++) {
    const wpt_t *w = &r->w[i];
    p = s + sprintf(s, "WP%03d,", i + 1);
    p = fx(p, w->lat, 1, 6); *p++ = ',';
    p = fx(p, w->lon, 1, 6); *p++ = ',';
    geo_fmt_dm(p, w->lat, 1, ' '); p += strlen(p); *p++ = ',';
    geo_fmt_dm(p, w->lon, 0, ' '); p += strlen(p); *p++ = ',';
    p = fx(p, w->leg_m / NM, 1, 1); *p++ = ',';
    p = fx(p, w->cum_m / NM, 1, 1); *p++ = ',';
    p = fx(p, w->crs_in, 3, 1); *p++ = ',';
    p = fx(p, w->crs_out, 3, 1); *p++ = ',';
    p = fx(p, w->rl_crs, 3, 1); *p++ = ',';
    p = fx(p, w->rl_m / NM, 1, 1); *p++ = ',';
    p += sprintf(p, "%c,%u", w->kind, w->flags);
    put(s, ctx);
  }
}
