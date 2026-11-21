/* test_dpops.c - NAV47 DP OPS checked on the PC (make test-dp).
 *  1. no lee, no limits, wind, current and waves together: the same answer as DPCAP47's own dp_held()
 *  2. the hull STANDOFF m off the face (no closer), every side, face and heading
 *  3. the lee: the wind from the platform over the vessel is lighter; from the other side, all of it
 *  4. the working limits: held inside them, no thruster above its limit
 *  5. the example: the best heading */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "dpops.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static float seg_dist(float px, float py, float ax, float ay, float bx, float by) {
  float dx = bx - ax, dy = by - ay, t = ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy);
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  return hypotf(px - ax - t * dx, py - ay - t * dy);
}
/* the least distance from the hull's outline to the platform's rectangle (corners to edges, both ways) */
static float clearance(const DpPlat *p, float h[7][2]) {
  float o = p->orient * 0.017453292f, a0x = sinf(o), a0y = cosf(o), a1x = cosf(o), a1y = -sinf(o), r[4][2];
  float sx[4] = { 1, 1, -1, -1 }, sy[4] = { 1, -1, -1, 1 };
  for (int k = 0; k < 4; k++) {
    r[k][0] = sx[k] * p->len / 2 * a0x + sy[k] * p->wid / 2 * a1x;
    r[k][1] = sx[k] * p->len / 2 * a0y + sy[k] * p->wid / 2 * a1y;
  }
  float m = 1e9f;
  for (int i = 0; i < 7; i++) for (int k = 0; k < 4; k++) {
    float d = seg_dist(h[i][0], h[i][1], r[k][0], r[k][1], r[(k + 1) % 4][0], r[(k + 1) % 4][1]);
    if (d < m) m = d;
    d = seg_dist(r[k][0], r[k][1], h[i][0], h[i][1], h[(i + 1) % 7][0], h[(i + 1) % 7][1]);
    if (d < m) m = d;
  }
  return m;
}

int main(void) {
  static Vessel v;
  char err[64];
  if (dp_parse(&v, DPO_EXAMPLE_VESSEL, err, sizeof err)) { printf("FAIL vessel: %s\n", err); return 1; }
  dp_prepare(&v);
  DpPlat p;
  dpo_default(&p);
  dpo_limits(&v, &p);
  printf("vessel %s: %d thrusters; platform %s %.0f x %.0f m, face 0 to %03.0f\n", v.name, v.nthr, p.name, p.len, p.wid, p.orient);
  for (int i = 0; i < v.nthr; i++) printf("  %-4s %s  full %.0f kN  limit %.0f %%\n", v.thr[i].name, DP_TYPE[v.thr[i].type], v.thr[i].tf, p.lim[i] * 100);
  CHECK(p.lim[0] == 0.3f && p.lim[1] == 0.3f && p.lim[2] == 0.3f && p.lim[3] == 1 && p.lim[4] == 1, "LIM TUN 30 on the three tunnels only");

  /* 1. against dp_held */
  DpPlat q = p;
  q.slen = 0;
  for (int i = 0; i < MAX_THR; i++) q.lim[i] = 1;
  int n1 = 0, same = 0;
  for (int bf = 2; bf <= 8; bf++)
    for (int d = 0; d < 360; d += 30) {
      const Env *e = &DP_TABLE[bf];
      DpWx w = { 0, e->wind / 0.514444f, 0, e->cur / 0.514444f, 0, e->hs, e->tp };
      float hdg = 17;                                  /* the weather from d relative to the bow, all together */
      w.wfrom = fmodf(hdg + d, 360); w.vfrom = w.wfrom; w.cset = fmodf(w.wfrom + 180, 360);
      DpRes r;
      dpo_eval(&v, &q, &w, 0, hdg, &r);
      Alloc a;
      memset(&a, 0, sizeof a);
      int h = dp_held(&v, dp_case_mask(&v, 0), e, (float)d, &a);
      n1++; same += h == r.held_lim;
      float t[3];
      dp_force(&v, e, (float)d, t);
      CHECK(fabsf(t[0] - r.env[0]) < 0.01f * (1 + fabsf(t[0])) && fabsf(t[1] - r.env[1]) < 0.01f * (1 + fabsf(t[1])) &&
            fabsf(t[2] - r.env[2]) < 0.01f * (1 + fabsf(t[2])), "forces bf %d from %d: %.1f %.1f %.0f vs %.1f %.1f %.0f",
            bf, d, r.env[0], r.env[1], r.env[2], t[0], t[1], t[2]);
    }
  CHECK(same == n1, "held as dp_held: %d of %d", same, n1);
  printf("1. weather together, no lee, no limits: %d of %d cases as DPCAP47's dp_held\n", same, n1);

  /* 2. the working point (the side's middle, the stern, the bow) on the face's centre line, STANDOFF m off or more; no
        corner closer than MINCLEAR; one of the two exactly (moved out no further than needed); square: no part closer */
  float mfar = 0;
  for (int sd = 0; sd < DPS_N; sd++) for (int f = 0; f < 4; f++) for (int dh = -30; dh <= 30; dh += 5) {
    DpPlat t = p;
    t.side = sd;
    static const float add[4] = { 0, 90, -90, 180 };
    float hdg = dpo_face_normal(&t, f) + add[sd] + (float)dh, px, py, hull[7][2];
    dpo_place(&v, &t, f, hdg, &px, &py);
    dpo_hull(&v, &t, hdg, px, py, hull);
    float L = dpo_loa(&v, &t), wx = sd == DPS_STERN ? -L / 2 : sd == DPS_BOW ? L / 2 : 0, wy = sd == DPS_STBD ? v.b / 2 : sd == DPS_PORT ? -v.b / 2 : 0;
    float h = hdg * 0.017453292f, ax = px + wx * sinf(h) + wy * cosf(h), ay = py + wx * cosf(h) - wy * sinf(h);
    float nb = dpo_face_normal(&t, f) * 0.017453292f, along = ax * cosf(nb) - ay * sinf(nb);
    float mid = dpo_mid(&v, &t, f, hdg, px, py), cl = dpo_clear(&v, &t, f, hdg, px, py), c = clearance(&t, hull);
    CHECK(fabsf(along) < 0.01f, "side %s face %d %+d: %.2f m along the face", DPO_SIDE[sd], f, dh, along);
    CHECK(mid >= t.standoff - 0.01f && cl >= t.mincl - 0.01f && (fabsf(mid - t.standoff) < 0.01f || fabsf(cl - t.mincl) < 0.01f),
          "side %s face %d %+d: the working point %.2f m, the nearest corner %.2f m", DPO_SIDE[sd], f, dh, mid, cl);
    CHECK(c >= fminf(t.mincl, t.standoff) - 0.05f, "side %s face %d %+d: the hull %.2f m off the platform", DPO_SIDE[sd], f, dh, c);
    if (dh == 0) CHECK(fabsf(cl - t.standoff) < 0.05f, "side %s face %d square: the nearest %.2f m", DPO_SIDE[sd], f, cl);
    if (sd == DPS_STBD && dh == 30 && mid > mfar) mfar = mid;
  }
  printf("2. the working point on the face's centre line, %.0f m off or more, no corner within %.0f m, every side, face and\n"
         "   heading; starboard side to turned 30 degrees: moved out to %.1f m (the corner at %.0f m)\n", p.standoff, p.mincl, mfar, p.mincl);

  /* 3. the lee: stern-to face 0 (to 030), the vessel on the platform's 030 side; the wind from 210 crosses it first */
  DpWx w = { 210, 30, 0, 0, 210, 0, 0 };
  DpRes r;
  DpPlat ps = p;
  ps.side = DPS_STERN;
  dpo_eval(&v, &ps, &w, 0, 30, &r);
  float lee_in = r.wleft;
  w.wfrom = 30; w.vfrom = 30;
  dpo_eval(&v, &ps, &w, 0, 30, &r);
  CHECK(lee_in < 0.8f && r.wleft == 1, "the lee: from the platform %.2f, from the open sea %.2f", lee_in, r.wleft);
  printf("3. stern-to, the wind across the platform: %.0f %% of its force; from the open sea: %.0f %%\n", lee_in * 100, r.wleft * 100);

  /* 4, 5. the example: 25 kn from 330, 1 kn setting 135, waves from 330 */
  DpWx ex = { 330, 25, 135, 1.0f, 330, 0, 0 };
  dpo_waves_auto(&ex);
  DpRes b;
  int n = dpo_best(&v, &p, &ex, 5, &b, NULL);
  printf("4. wind %03.0f %.0f kn, current to %03.0f %.1f kn, waves %.1f m %.1f s: %d headings tried\n",
         ex.wfrom, ex.wkn, ex.cset, ex.ckn, ex.hs, ex.tp, n);
  printf("   best: face %d (%03.0f) heading %03.0f, %s; wind left %.0f %%; force %.0f kN to %03.0f, %s %.0f kN\n",
         b.face, dpo_face_normal(&p, b.face), b.hdg, b.rank == 3 ? "HOLDS, and after the worst failure" : b.rank == 2 ? "HOLDS (not after a failure)" :
         b.rank == 1 ? "only beyond the limits" : "NO HOLD", b.wleft * 100, b.fmag, b.fto, b.blow >= 0 ? "blow-off" : "blow-on", fabsf(b.blow));
  for (int i = 0; i < v.nthr; i++) {
    printf("   %-4s %3.0f %% (limit %3.0f %%)  after group %d lost %3.0f %%\n", v.thr[i].name, b.use[i] * 100, p.lim[i] * 100,
           b.fail_grp, b.fail_wuse[i] * 100);
    if (b.held_lim) CHECK(b.use[i] <= p.lim[i] * 1.02f, "%s %.2f over its limit %.2f", v.thr[i].name, b.use[i], p.lim[i]);
  }
  { float sx = 0, sy = 0;                        /* the thrusters' forces balance the weather's (x 1.25) */
    for (int i = 0; i < v.nthr; i++) { sx += b.f[i][0]; sy += b.f[i][1]; }
    CHECK(fabsf(sx + b.env[0]) < 0.02f * b.fmag + 0.5f && fabsf(sy + b.env[1]) < 0.02f * b.fmag + 0.5f,
          "the thrust %.1f %.1f against the weather %.1f %.1f", sx, sy, b.env[0], b.env[1]);
    printf("   the thrusters: %.1f t ahead, %.1f t to starboard; the weather %.1f t to %03.0f\n", sx / 9.80665f, sy / 9.80665f, b.fmag / 9.80665f, b.fto); }
  float lw = dpo_limit_wind(&v, &p, &ex, b.face, b.hdg);
  printf("   holds inside the limits up to %.1f kn of wind from %03.0f\n", lw, ex.wfrom);
  CHECK(lw >= ex.wkn && lw <= DPO_WIND_TOP, "limit wind %.1f", lw);
  dpo_eval(&v, &p, &ex, 1, 120 + 90, &b);
  printf("   the same weather, face 1 STBD side to (heading 210): %s, the worst %s %.0f %% of its limit\n", b.rank >= 2 ? "holds" : b.rank == 1 ? "only beyond the limits" : "NO HOLD", v.thr[b.worst_thr].name, b.worst * 100);

  /* parallel: starboard or port side to, the heading along the face */
  for (int sd = DPS_STBD; sd <= DPS_PORT; sd++) {
    DpPlat t = p;
    t.side = sd; t.par = 1; t.face = 1;
    DpRes q;
    dpo_best(&v, &t, &ex, 5, &q, NULL);
    float want = fmodf(dpo_face_normal(&t, 1) + (sd == DPS_STBD ? 90 : 270), 360);
    CHECK(fabsf(q.hdg - want) < 0.01f, "parallel %s: heading %.1f, not %.1f", DPO_SIDE[sd], q.hdg, want);
    float hull[7][2];
    dpo_hull(&v, &t, q.hdg, q.px, q.py, hull);
    float nb = dpo_face_normal(&t, 1) * 0.017453292f, d0 = hull[0][0] * sinf(nb) + hull[0][1] * cosf(nb), d6 = hull[6][0] * sinf(nb) + hull[6][1] * cosf(nb);
    printf("5. %s side parallel to face 1: heading %03.0f, %s; the stern corners %.1f and %.1f m off the face's line\n", DPO_SIDE[sd], q.hdg,
           q.rank >= 2 ? "holds" : q.rank == 1 ? "only beyond the limits" : "no hold", d0 - t.wid / 2, d6 - t.wid / 2);
  }
  /* the file */
  char buf[600];
  dpo_write(&p, buf, sizeof buf);
  DpPlat p2;
  CHECK(dpo_parse(buf, &p2, err, sizeof err) == 0 && p2.len == p.len && p2.side == p.side && p2.nlim == p.nlim, "DPOPS.TXT round trip: %s", err);
  printf("%s\n", fails ? "FAILED" : "all passed");
  return fails != 0;
}
