#include <stdio.h>
#include "../src/route.h"
int main(void) {
  double c, d;
  /* Bowditch: MP(30N) = 1876.67 (Table 6), MP(45N) = 3013.38 */
  printf("MP30 %.2f MP45 %.2f MP60 %.2f\n", mer_parts(30), mer_parts(45), mer_parts(60));
  leg_rl(26.5, 50.1667, 25.3333, 55.2, &c, &d); printf("RL Ras Tanura->Dubai %.1f %.1f\n", c, d);
  leg_gc(26.5, 50.1667, 25.3333, 55.2, &c, &d); printf("GC %.1f %.1f\n", c, d);
  leg_rl(40, -74, 50, -5, &c, &d); printf("RL NY->Lizard %.1f %.1f\n", c, d);
  leg_gc(40, -74, 50, -5, &c, &d); printf("GC NY->Lizard %.1f %.1f\n", c, d);
  leg_rl(10, 179, 10, -179, &c, &d); printf("RL dateline %.1f %.2f\n", c, d);
  return 0;
}
