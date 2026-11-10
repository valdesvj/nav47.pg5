/* dmcon.c - DMCP5 3.58 serial-console wire format over TinyUSB CDC.
 * Compiled only when DMCON_NATIVE is 0. See dmcon.h.
 *
 * Rules:
 *  - Call everything from the main loop only, never from a TinyUSB callback.
 *  - Your existing code still does the USB hardware set-up (clock 48 MHz,
 *    VDDUSB, pins, tusb_init). dmcon only uses the tud_* device API.
 *  - Set CFG_TUD_CDC_TX_BUFSIZE to 512 or more in tusb_config.h, so a
 *    255-byte write and LCD rows fit in the FIFO.
 *  - Keep DMCP's own serial console OFF while this build runs: only one
 *    USB stack can own the port.
 */
#include "dmcon.h"

#if !DMCON_NATIVE

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <dmcp.h>
#include "tusb.h"

#ifndef DMCON_HW_NAME
# ifdef PROGRAM_NAME
#  define DMCON_HW_NAME PROGRAM_NAME
# else
#  define DMCON_HW_NAME "PGM"
# endif
#endif
#ifndef DMCON_VERSION
# ifdef PROGRAM_VERSION
#  define DMCON_VERSION PROGRAM_VERSION
# else
#  define DMCON_VERSION "?"
# endif
#endif

#define OUT_CHAN_CNT   11            /* channels 0..10 */
#define LCD_ROWS       240
#define LCD_BYTES      50            /* 400 pixels */
#define LCD_PACKED     58            /* 400 bits, 7 per byte */
#define TX_WAIT_LOOPS  20000         /* bound when waiting for TX room */

/* ---------- hooks: weak defaults use DMCP calls; override if your SDK differs ---------- */

__attribute__((weak)) void dmcon_key_push(int k) { key_push(k); }

__attribute__((weak)) int dmcon_keys_held(int *k1, int *k2) { return read_key(k1, k2); }

__attribute__((weak)) uint32_t dmcon_cs_in_hour(void)
{
    tm_t t; dt_t d;
    rtc_read(&t, &d);
    return (uint32_t)t.min * 6000u + (uint32_t)t.sec * 100u + t.csec;
}

/* Wire row w (0 = bottom, 239 = top screen row) in wire bit order:
 * pixel x in byte 49 - x/8, bit 7 - x%8, dark pixel = 0.
 * DMCP's frame buffer is expected to hold exactly this layout; verify with
 * a capture against the native build before trusting it. */
__attribute__((weak)) const uint8_t *dmcon_lcd_wire_row(int w)
{
    return lcd_line_addr(LCD_ROWS - 1 - w);
}

/* ---------- state ---------- */

typedef struct {
    uint8_t  data[USIN_CHAN_SIZE];
    uint16_t len;                         /* bytes held: whole lines + partial line */
    uint16_t plen;                        /* bytes of the partial line */
    uint8_t  lines;                       /* whole lines held */
    uint8_t  drop;                        /* discarding until the next '\n' */
    uint16_t llen[USIN_MAX_READLINES];    /* length of each line, terminator included */
    uint8_t  lovf[USIN_MAX_READLINES];    /* line was cut by overflow */
} chan_in_t;

static chan_in_t cin[USIN_CHAN_CNT];
static uint8_t   in_ch;
static uint8_t   sw_enabled, sw_printf, sw_lcd, sw_keys;
static uint8_t   was_connected;
static int       last_k1 = -1, last_k2 = -1;
static uint32_t  row_hash[LCD_ROWS];
static uint8_t   row_valid[LCD_ROWS / 8];
static int       lcd_scan = -1;           /* next wire row to check, -1 = idle */

/* ---------- input ---------- */

static void in_byte(chan_in_t *c, uint8_t b)
{
    if (c->drop) {
        if (b == US_LINEEND_CHAR) c->drop = 0;
        return;
    }
    if (c->plen == 0 && (c->lines >= USIN_MAX_READLINES || c->len >= USIN_CHAN_SIZE)) {
        if (b != US_LINEEND_CHAR) c->drop = 1;   /* no room for a new line: lose it */
        return;
    }
    if (b == US_LINEEND_CHAR || c->len >= USIN_CHAN_SIZE - 1) {
        uint8_t ovf = (b != US_LINEEND_CHAR);    /* last free byte: end the line with 0x0f */
        c->data[c->len++] = ovf ? US_OVERFLOW_CHAR : US_LINEEND_CHAR;
        c->llen[c->lines] = c->plen + 1;
        c->lovf[c->lines] = ovf;
        c->lines++;
        c->plen = 0;
        c->drop = ovf;
        return;
    }
    c->data[c->len++] = b;
    c->plen++;
}

static void rx_byte(uint8_t b)
{
    if (b >= US_CHAN0_CHAR && b <= US_CHAN0_CHAR + OUT_CHAN_CNT - 1) {
        in_ch = b - US_CHAN0_CHAR;               /* id byte: never data */
        return;
    }
    if (in_ch < USIN_CHAN_CNT) in_byte(&cin[in_ch], b);   /* 4..10: no input FIFO, dropped */
}

int dmcon_read_line(uint8_t chan_nr, uint8_t *buf, int buflen, uint8_t *is_overflow)
{
    if (chan_nr >= USIN_CHAN_CNT) return 0;
    chan_in_t *c = &cin[chan_nr];
    if (c->lines == 0) return 0;
    int n = c->llen[0];
    if (buf) {
        if (n > buflen) return -1;               /* stays in the FIFO */
        memcpy(buf, c->data, n);
        buf[n] = 0;                              /* caller allocated buflen + 1 */
        if (is_overflow) *is_overflow = c->lovf[0];
    }
    memmove(c->data, c->data + n, c->len - n);
    c->len -= n;
    c->lines--;
    memmove(c->llen, c->llen + 1, c->lines * sizeof c->llen[0]);
    memmove(c->lovf, c->lovf + 1, c->lines);
    return n;
}

/* ---------- output ---------- */

static int tx_room(uint32_t need)
{
    for (int i = 0; i < TX_WAIT_LOOPS; i++) {
        if (!tud_cdc_connected()) return 0;
        if (tud_cdc_write_available() >= need) return 1;
        tud_cdc_write_flush();
        tud_task();
    }
    return 0;
}

/* One call = one id byte, then the data (as DMCP: id only at the start of a call). */
static void tx(uint8_t chan, const void *buf, uint32_t len, int wait)
{
    uint8_t id = US_CHAN0_CHAR + chan;
    if (wait ? !tx_room(len + 1) : tud_cdc_write_available() < len + 1) return;
    tud_cdc_write(&id, 1);
    tud_cdc_write(buf, len);
    tud_cdc_write_flush();
}

void dmcon_write(uint8_t chan_nr, const char *buf, int len)
{
    if (!sw_enabled || chan_nr >= OUT_CHAN_CNT || len <= 0 || !tud_cdc_connected()) return;
    if (len > 255) len = 255;                    /* DMCP sends at most 255 bytes per call */
    tx(chan_nr, buf, (uint32_t)len, 1);
}

int dmcon_printf(const char *fmt, ...)
{
    char b[128];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (n < 0) return n;
    if (n > (int)sizeof b - 1) n = (int)sizeof b - 1;
    if (sw_printf) dmcon_write(1, b, n);
    return n;
}

/* ---------- channel 0: LCD rows ---------- */

static uint32_t hash_row(const uint8_t *p)
{
    uint32_t h = 2166136261u;                    /* FNV-1a */
    for (int i = 0; i < LCD_BYTES; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}

/* "L yy" + 400 pixels packed 7 bits per byte, LSB first, top bit set, + '\n' */
static int lcd_encode(uint8_t *out, int w, const uint8_t *row)
{
    static const char hx[] = "0123456789ABCDEF";
    int n = 0;
    out[n++] = 'L'; out[n++] = ' ';
    out[n++] = hx[(w >> 4) & 15]; out[n++] = hx[w & 15];
    uint32_t acc = 0; int bits = 0;
    for (int i = 0; i < LCD_BYTES; i++) {
        acc |= (uint32_t)row[i] << bits;
        bits += 8;
        while (bits >= 7) { out[n++] = 0x80 | (acc & 0x7f); acc >>= 7; bits -= 7; }
    }
    if (bits) out[n++] = 0x80 | (acc & 0x7f);
    out[n++] = '\n';
    return n;                                    /* 63 bytes */
}

static void lcd_step(void)
{
    int connected = tud_cdc_connected();
    while (lcd_scan >= 0 && lcd_scan < LCD_ROWS) {
        int w = lcd_scan;
        const uint8_t *row = dmcon_lcd_wire_row(w);
        uint32_t h = hash_row(row);
        int valid = row_valid[w >> 3] & (1 << (w & 7));
        if (!valid || h != row_hash[w]) {
            if (connected) {
                uint8_t b[4 + LCD_PACKED + 1];
                int n = lcd_encode(b, w, row);
                if (tud_cdc_write_available() < (uint32_t)n + 1) return;   /* resume next call */
                tx(0, b, (uint32_t)n, 0);
            }
            /* As DMCP: a row counts as sent even with no host, so a new host
             * gets nothing until a row changes or it asks with "L". */
            row_hash[w] = h;
            row_valid[w >> 3] |= (uint8_t)(1 << (w & 7));
        }
        lcd_scan++;
    }
    lcd_scan = -1;
}

void dmcon_lcd_refreshed(void)
{
    if (sw_enabled && sw_lcd) lcd_scan = 0;
}

/* ---------- channel 0: key reports ---------- */

static void keys_step(void)
{
    int k1 = 0, k2 = 0;
    dmcon_keys_held(&k1, &k2);
    if (k1 <= 0) k1 = 0xff;                      /* ff = empty slot */
    if (k2 <= 0) k2 = 0xff;
    if (k1 == last_k1 && k2 == last_k2) return;
    last_k1 = k1; last_k2 = k2;
    char b[32];
    int n = snprintf(b, sizeof b, "K %02x %02x %06lx\n",
                     k1 & 0xff, k2 & 0xff, (unsigned long)dmcon_cs_in_hour());
    tx(0, b, (uint32_t)n, 1);
}

/* ---------- channel 0: host commands ---------- */

static void send_identity(void)
{
    char b[128];
    int n = snprintf(b, sizeof b, "V|HW %s|VER %s|DMCP5 compat|IFC -|SN -\n",
                     DMCON_HW_NAME, DMCON_VERSION);
    if (n > (int)sizeof b - 1) { n = (int)sizeof b - 1; b[n - 1] = '\n'; }
    tx(0, b, (uint32_t)n, 1);
}

static void ch0_line(char *s, int n)
{
    if (n == 1 && s[0] == 'V') { send_identity(); return; }
    if (n == 1 && s[0] == 'L') {                 /* full display: rows go out at the redraw */
        memset(row_valid, 0, sizeof row_valid);
        dmcon_key_push(DMCON_KEY_REDRAW);
        dmcon_key_push(0);
        return;
    }
    if (n == 1 && s[0] == 'B') return;           /* DFU: not offered by this port */
    if (s[0] == 'K') {
        if (s[1] == 'R') { dmcon_key_push(0); return; }
        int hold = (s[1] == 'P');
        const char *p = s + 1 + hold;
        while (*p == ' ') p++;
        long v = strtol(p, NULL, 16);
        int k1 = (int)(v & 0xff);                /* KPhhhh: k1 is the low byte; k2 not injected */
        if (k1 <= 0) return;
        dmcon_key_push(k1);
        if (!hold) dmcon_key_push(0);
    }
}

/* ---------- switches ---------- */

int dmcon_ioctl(int op, int val)
{
    int rd = (val == SER_IOCTL_READ);
    uint8_t *sw;
    switch (op) {
    case SERIO_IS_ENABLED:
        if (!rd) {
            sw_enabled = (val != 0);
            if (sw_enabled) tud_connect(); else tud_disconnect();
        }
        return sw_enabled;
    case SERIO_IS_ACTIVE:    return sw_enabled && tud_mounted();
    case SERIO_IS_CONNECTED: return sw_enabled && tud_cdc_connected();
    case SERIO_LINE_READY:   return (val >= 0 && val < USIN_CHAN_CNT) ? cin[val].lines : 0;
    case SERIO_LINE_LENGTH:
        return (val >= 0 && val < USIN_CHAN_CNT && cin[val].lines) ? cin[val].llen[0] : 0;
    case SERIO_SEND_PRINTF:  sw = &sw_printf; break;
    case SERIO_SEND_LCD:     sw = &sw_lcd;    break;
    case SERIO_SEND_KEYS:    sw = &sw_keys;   break;
    case SERIO_SEND_IR:      return 0;        /* no IR printer over this port */
    default:                 return SER_IOCTL_INVALID;
    }
    if (!rd) *sw = (val != 0);
    return *sw;
}

/* ---------- main-loop pump ---------- */

void dmcon_task(void)
{
    if (!sw_enabled) return;
    tud_task();

    int conn = tud_cdc_connected();
    if (conn && !was_connected) in_ch = 0;
    was_connected = (uint8_t)conn;

    uint8_t tmp[64];
    while (tud_cdc_available()) {
        uint32_t n = tud_cdc_read(tmp, sizeof tmp);
        for (uint32_t i = 0; i < n; i++) rx_byte(tmp[i]);
    }

    /* Channel 0 is served here, as DMCP does; channel 1 (system input) is unused. */
    uint8_t line[USIN_CHAN_SIZE + 1];
    uint8_t ovf;
    int n;
    while ((n = dmcon_read_line(0, line, USIN_CHAN_SIZE, &ovf)) != 0) {
        if (n < 0) { dmcon_read_line(0, NULL, USIN_CHAN_SIZE, NULL); continue; }
        if (ovf || line[n - 1] != US_LINEEND_CHAR) continue;
        line[n - 1] = 0;
        ch0_line((char *)line, n - 1);
    }
    while (dmcon_read_line(1, NULL, USIN_CHAN_SIZE, NULL) != 0) {}

    if (sw_keys && conn) keys_step();
    if (sw_lcd && lcd_scan >= 0) lcd_step();
}

#endif /* !DMCON_NATIVE */
