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
int usb_serial_read_line(uint8_t chan_nr, uint8_t *buf, int buflen, uint8_t *is_overflow) {
  (void)chan_nr; (void)buf; (void)buflen; if (is_overflow) *is_overflow = 0; return 0;
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
