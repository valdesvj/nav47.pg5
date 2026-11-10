/* dmcon.h - one serial-console API for DMCP5 3.58 and for an own TinyUSB port.
 *
 * DMCON_NATIVE=1  DMCP5 3.58+ (IFC 3.18). The calls map straight to DMCP's
 *                 usb_serial_ioctl / usb_serial_read_line / usb_serial_write,
 *                 and DMCP itself serves channel 0 (V, K.., L, LCD rows, keys).
 * DMCON_NATIVE=0  (default) older DMCP5: dmcon.c speaks the same wire format
 *                 over TinyUSB CDC, channel 0 included.
 *
 * Program code uses only the dmcon_* names, so it is the same in both builds.
 * Wire format and values follow "DMCP5 Serial Console API", preliminary
 * data sheet of 2026-11-06 (DMCP5 3.58 / IFC 3.18).
 */
#ifndef DMCON_H
#define DMCON_H

#include <stdint.h>

#ifndef DMCON_NATIVE
#define DMCON_NATIVE 0
#endif

#define DMCON_KEY_REDRAW 77   /* key code DMCP passes after a host's "L" */

#if DMCON_NATIVE

#include <dmcp.h>
#define dmcon_ioctl      usb_serial_ioctl
#define dmcon_read_line  usb_serial_read_line
#define dmcon_write      usb_serial_write
#define dmcon_printf     printf          /* reaches channel 1 while SERIO_SEND_PRINTF is on */
static inline void dmcon_task(void) {}
static inline void dmcon_lcd_refreshed(void) {}

#else

/* Same names and values as the DMCP5 3.58 dmcp.h (data sheet sections 4-6). */
#ifndef SER_IOCTL_READ
#define SER_IOCTL_READ      ((int)0x80000000u)
#endif
#ifndef SER_IOCTL_INVALID
#define SER_IOCTL_INVALID   ((int)0x80000000u)
#endif
#ifndef SERIO_IS_ENABLED
#define SERIO_IS_ENABLED    0x01
#define SERIO_IS_ACTIVE     0x02
#define SERIO_IS_CONNECTED  0x03
#define SERIO_LINE_READY    0x08
#define SERIO_LINE_LENGTH   0x09
#define SERIO_SEND_PRINTF   0x10
#define SERIO_SEND_LCD      0x11
#define SERIO_SEND_KEYS     0x12
#define SERIO_SEND_IR       0x13
#endif
#ifndef US_CHAN0_CHAR
#define US_CHAN0_CHAR       0x10
#define US_LINEEND_CHAR     '\n'
#define US_OVERFLOW_CHAR    0x0f
#endif
#ifndef USIN_CHAN_CNT
#define USIN_CHAN_CNT       4
#define USIN_CHAN_SIZE      256
#define USIN_MAX_READLINES  16
#endif

int  dmcon_ioctl(int op, int val);
int  dmcon_read_line(uint8_t chan_nr, uint8_t *buf, int buflen, uint8_t *is_overflow);
void dmcon_write(uint8_t chan_nr, const char *buf, int len);
int  dmcon_printf(const char *fmt, ...);

/* Compat only (empty in the native build):
 * dmcon_task()          call from the main loop, every wake-up (runs tud_task too)
 * dmcon_lcd_refreshed() call right after each lcd_refresh() / lcd_forced_refresh() */
void dmcon_task(void);
void dmcon_lcd_refreshed(void);

#endif /* DMCON_NATIVE */
#endif /* DMCON_H */
