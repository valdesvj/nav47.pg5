/* tide_cli.c - PC driver for test/test_tide.py.
 *   tide_cli FILE.TID [FILE.FLW]      then stdin commands:
 *   DAY src region station n stream   src P = compiled public data, F = the loaded file
 *   NOW src region station n hour
 *   FLOW latdm londm wdir wkn n target (reference: the station named by the atlas, in the file's regions) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/tide.h"
#include "../src/tidefile.h"

static char *slurp(const char *fn) {
  FILE *f = fopen(fn, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *b = malloc((size_t)n + 1);
  fread(b, 1, (size_t)n, f);
  b[n] = 0;
  fclose(f);
  return b;
}

int main(int argc, char **argv) {
  TRegion *fr = NULL;
  int nfr = 0;
  char err[80];
  if (argc > 1) {
    char *t = slurp(argv[1]);
    if (!t || tid_parse(t, &fr, &nfr, err, sizeof err) < 0) { printf("ERR %s\n", err); return 1; }
  }
  FlowAtlas fl;
  int have_fl = 0;
  if (argc > 2) {
    char *t = slurp(argv[2]);
    if (!t || flw_parse(t, &fl, err, sizeof err) < 0) { printf("ERR %s\n", err); return 1; }
    have_fl = 1;
  }
  char cmd[16], src[4];
  while (scanf("%15s", cmd) == 1) {
    if (!strcmp(cmd, "DAY") || !strcmp(cmd, "NOW")) {
      int r, s;
      long n;
      double x;
      scanf("%3s %d %d %ld %lf", src, &r, &s, &n, &x);
      const TStation *st = (src[0] == 'P' ? &PUB_REGIONS[r].st[s] : &fr[r].st[s]);
      if (cmd[0] == 'D') {
        TDay D;
        tide_day(st, n, (int)x, &D);
        printf("TZ %.17g\n", D.tz);
        printf("EV %d", D.nev);
        for (int i = 0; i < D.nev; i++) printf(" %.17g %.17g %.17g %d", D.ev[i].t, D.ev[i].val, D.ev[i].set, D.ev[i].kind);
        printf("\nCV");
        for (int i = 0; i < 73; i++) printf(" %.17g", D.curve[i]);
        printf("\n");
      } else {
        double v, d;
        stream_now(st, n, x, &v, &d);
        printf("NOW %.17g %.17g\n", v, d);
      }
    } else if (!strcmp(cmd, "FLOW")) {
      double la, lo, wd, wk, target;
      long n;
      scanf("%lf %lf %lf %lf %ld %lf", &la, &lo, &wd, &wk, &n, &target);
      const TStation *ref = NULL;
      for (int r = 0; r < nfr && !ref; r++)
        for (int s = 0; s < fr[r].n; s++)
          if (have_fl && strstr(fr[r].st[s].name, fl.ref)) { ref = &fr[r].st[s]; break; }
      double FU[13], FV[13];
      if (!have_fl || !ref || !flow_cell(&fl, flow_dm(la), flow_dm(lo), FU, FV)) { printf("FLOW none\n"); continue; }
      long on;
      int om;
      flow_pick(ref, n, target, &on, &om);
      double t, h, rng, fac;
      flow_cycle(ref, on, om, &t, &h, &rng, &fac);
      double U[13], V[13];
      flow_rows(FU, FV, fac, wd, wk, U, V);
      printf("FLOW %ld %d %.17g %.17g %.17g %.17g", on, om, t, h, rng, fac);
      for (int i = 0; i < 13; i++) printf(" %.17g %.17g", U[i], V[i]);
      long n2;
      int m2;
      flow_step(ref, on, om, 1, &n2, &m2);
      printf(" NEXT %ld %d", n2, m2);
      flow_step(ref, on, om, -1, &n2, &m2);
      printf(" PREV %ld %d\n", n2, m2);
    }
    fflush(stdout);
  }
  return 0;
}
