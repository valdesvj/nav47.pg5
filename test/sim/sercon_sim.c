/* sercon_sim.c - the PC simulator's DMCP5 3.58 serial console, as the data sheet describes it: with SERIO_SEND_LCD
 * on, each refresh sends the rows that differ from the ones last sent, on channel 0 into ./acm.out:
 *   0x10 "L <yy>" + the 50 bytes of the row packed 7 bits a byte (LSB first, top bit set) + '\n'
 * yy: two upper-case hex digits, the top screen row is 239; pixel x in byte 49 - x/8, bit 7 - x%8, dark = 0 (the
 * line buffer's own bytes 2..51, lcd_line_addr). HOSTL in the key script: every row unsent, the program gets
 * key 77, its lcd_forced_refresh() then sends every non-blank row. tools/dm42view.py acm.out shows it. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "dmcp.h"
#include "sercon_ifc.h"

static int en, lcd, sent_ok[LCD_Y];
static uint8_t sent[LCD_Y][50];
static FILE *f;

int usb_serial_ioctl(int op, int val) {
  int *p = op == SERIO_IS_ENABLED ? &en : op == SERIO_SEND_LCD ? &lcd : NULL;
  if (op == SERIO_IS_ACTIVE || op == SERIO_IS_CONNECTED) return val == SER_IOCTL_READ ? en : SER_IOCTL_INVALID;
  if (!p) return val == SER_IOCTL_READ ? 0 : SER_IOCTL_INVALID;
  if (val == SER_IOCTL_READ) return *p;
  *p = val ? 1 : 0;
  return *p;
}
void usb_serial_write(uint8_t chan_nr, const char *buf, int len) {
  if (!en) return;
  if (!f) f = fopen("acm.out", "wb");
  fputc(US_CHAN0_CHAR + chan_nr, f); fwrite(buf, 1, (size_t)(len > 255 ? 255 : len), f); fflush(f);
}
/* channel 2 in: NMEA. NAV47_NMEA=file replays a log (test/nmea/make_log.py: a blank line ends each second), its
   second k due k seconds after the GPS went on; the web page pushes its lines (sim_nmea_push). */
#include <stdlib.h>
uint32_t sys_current_ms(void);
#define QN 64
static char q[QN][128];
static int qh, qt;
void sim_nmea_push(const char *line) {
  if ((qt + 1) % QN == qh) return;                /* full: the line lost, as on the calculator */
  snprintf(q[qt], sizeof q[qt], "%s\n", line);
  qt = (qt + 1) % QN;
}
static FILE *nm;
static int nm_done, nm_sec;
static uint32_t nm_t0;
static char nm_line[256];
static int nm_have;
int usb_serial_read_line(uint8_t chan_nr, uint8_t *buf, int buflen, uint8_t *is_overflow) {
  if (is_overflow) *is_overflow = 0;
  if (!en || chan_nr != 2) return 0;
  const char *l = NULL;
  if (qh != qt) l = q[qh];
  else {
    if (!nm && !nm_done) {
      const char *p = getenv("NAV47_NMEA");
      nm = p ? fopen(p, "r") : NULL;
      if (!nm) nm_done = 1;
      nm_t0 = sys_current_ms();
    }
    if (!nm) return 0;
    for (;;) {
      if (!nm_have) {
        if (!fgets(nm_line, sizeof nm_line, nm)) { fclose(nm); nm = NULL; nm_done = 1; return 0; }
        nm_have = 1;
      }
      if (nm_line[0] == '\n' || nm_line[0] == '\r') {     /* the end of a second: the next one when it is due */
        if (sys_current_ms() - nm_t0 < (uint32_t)(nm_sec + 1) * 1000u) return 0;
        nm_sec++; nm_have = 0; continue;
      }
      l = nm_line;
      break;
    }
  }
  int n = (int)strlen(l);
  if (buf) {
    if (n > buflen) return -1;
    memcpy(buf, l, (size_t)n + 1);
  }
  if (qh != qt) qh = (qh + 1) % QN; else nm_have = 0;
  return n;
}

void sim_host_L(void) { memset(sent_ok, 0, sizeof sent_ok); }

void sim_lcd_out(int forced) {
  (void)forced;                               /* DMCP: a row goes out when it differs from the one last sent */
  if (!en || !lcd) return;
  for (int y = 0; y < LCD_Y; y++) {
    const uint8_t *d = lcd_line_addr(y) + 2;
    int blank = 1;
    for (int i = 0; i < 50; i++) if (d[i] != 0xFF) blank = 0;
    if (sent_ok[y] && !memcmp(sent[y], d, 50)) continue;
    if (!sent_ok[y] && blank) { memcpy(sent[y], d, 50); sent_ok[y] = 1; continue; }   /* "every non-blank row" */
    memcpy(sent[y], d, 50); sent_ok[y] = 1;
    char line[4 + 58 + 1];
    int n = sprintf(line, "L %02X", LCD_Y - 1 - y);
    for (int j = 0; j < 58; j++) {             /* 400 bits, 7 a byte, least significant first */
      int v = 0;
      for (int k = 0; k < 7; k++) {
        int i = j * 7 + k;
        if (i < 400 && (d[i / 8] >> (i % 8) & 1)) v |= 1 << k;
      }
      line[n++] = (char)(0x80 | v);
    }
    line[n++] = '\n';
    usb_serial_write(0, line, n);
  }
}
