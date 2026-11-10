/* tport_sim.c - the PC simulator's stand-in for src/usb/tport.c (make sim USB=tinyusb): the port is always open and
 * what src/usb/usbcon.c sends goes to ./acm.out; HOSTL in the key script is the PC sending L on channel 0.
 * So the simulator checks usbcon.c's stream against test/sim/sercon_sim.c's (DMCP's): tools/dm42view.py acm.out. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "tport.h"

static FILE *f;
static uint8_t rx[64];
static int nrx;

int tport_start(const char *product) { (void)product; if (!f) f = fopen("acm.out", "wb"); return f ? 0 : -2; }
void tport_stop(void) { if (f) fclose(f); f = NULL; }
int tport_mounted(void) { return f != NULL; }
int tport_connected(void) { return f != NULL; }
void tport_task(void) {}
int tport_rx(uint8_t *p, int max) {
  int n = nrx < max ? nrx : max;
  memcpy(p, rx, (size_t)n);
  memmove(rx, rx + n, (size_t)(nrx - n));
  nrx -= n;
  return n;
}
int tport_tx_room(void) { return f ? 1024 : 0; }
int tport_tx(const void *p, int n) { if (!f) return 0; fwrite(p, 1, (size_t)n, f); fflush(f); return n; }
void sys_sleep(void);
void tport_wait(uint32_t ms, void (*poll)(void)) { (void)ms; poll(); sys_sleep(); }   /* the next scripted key */
const char *tport_product(void) { return "sim"; }

/* the PC sends L (the sim delivers key 77 itself) */
void sim_host_L(void) { static const char L[] = "\x10L\n"; memcpy(rx + nrx, L, 3); nrx += 3; }
int key_push(int k1) { (void)k1; return 0; }
