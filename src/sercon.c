/* sercon.c - NAV47: the screen on the PC through DMCP5's serial console, in both builds:
 *   make                  (USB=sercon)  DMCP5 3.58's own console (IFC 3.18): DMCP runs the USB
 *   make USB=tinyusb      the same console played by src/usb/usbcon.c over our own TinyUSB port (any DMCP5)
 * The PC sees the same stream either way, so tools/dm42view.py (screen, --keys, --send, --c47) works with both.
 *
 * With SERIO_SEND_LCD on, every changed row goes out on channel 0 at each refresh ("L <yy>" + the 400 pixels); when
 * the PC sends L the program gets key 77: it redraws and calls lcd_forced_refresh() so every row goes out. So the
 * program only switches the console and the LCD stream on (>> >> USB), handles key 77 (main.c), and gives back the
 * user's DMCP settings when it ends or the USB goes off. In the TinyUSB build usbcon.c sends the rows at
 * usbview_send() (after each redraw) and runs the USB in usbser_idle() (the CPU must not sleep).
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#include <main.h>
#include <dmcp.h>
#include <stdio.h>
#include <string.h>
#include "usbview.h"
#include "usbser.h"
#include "sercon_ifc.h"

#define RD SER_IOCTL_READ
static int on, was_en = -1, was_lcd = -1, redraws;

static int sw(int op) { return usb_serial_ioctl(op, RD); }
static void put_back(int op, int v) { if (v == 0 || v == 1) usb_serial_ioctl(op, v); }   /* only a real 0 / 1 */

void usbview_on(void) {
  if (on) return;
#ifdef SERCON_TINYUSB
  usbcon_ident("NAV47 screen", PROGRAM_VERSION);
#endif
  was_en = sw(SERIO_IS_ENABLED);
  was_lcd = sw(SERIO_SEND_LCD);
  usb_serial_ioctl(SERIO_IS_ENABLED, 1);
#ifdef SERCON_TINYUSB
  if (sw(SERIO_IS_ENABLED) != 1) return;           /* no USB power or no memory: usbview_state says which */
#endif
  usb_serial_ioctl(SERIO_SEND_LCD, 1);
  on = 1;
}

void usbview_off(void) {
  if (!on) return;
  put_back(SERIO_SEND_LCD, was_lcd);              /* the user's DMCP settings, as they were */
  put_back(SERIO_IS_ENABLED, was_en);
  on = 0;
}

int usbview_is_on(void) { return on; }

/* after each redraw: DMCP sends the rows itself at lcd_refresh(); the TinyUSB build sends them now */
void usbview_send(void) {
#ifdef SERCON_TINYUSB
  if (on) { usbcon_lcd_refreshed(); usbcon_poll(); }
#endif
}

/* key 77 (the PC sent L): redraw, then every row goes out */
void usbview_redraw_request(void) {
  redraws++;
  lcd_forced_refresh();
#ifdef SERCON_TINYUSB
  usbcon_lcd_refreshed();
#endif
}

void usbview_state(char *a, char *b, int n) {
#ifdef SERCON_TINYUSB
  int e = usbcon_error();
  snprintf(a, (size_t)n, "USB: TinyUSB serial console %s", on ? "ON, the LCD streamed" :
           e == -1 ? "OFF: NO USB POWER" : e == -2 ? "OFF: no memory" : "OFF");
  if (!on) { snprintf(b, (size_t)n, ">> >> USB: on (connect the cable)"); return; }
  snprintf(b, (size_t)n, "PC %s  port %s  layout %s  redraws %d", sw(SERIO_IS_ACTIVE) > 0 ? "sees it" : "-",
           sw(SERIO_IS_CONNECTED) > 0 ? "open" : "closed", usbcon_layout_ok() ? "ok" : "NOT ok", redraws);
#else
  snprintf(a, (size_t)n, "USB: DMCP serial console %s", on ? "ON, the LCD streamed" : "OFF");
  if (!on) { snprintf(b, (size_t)n, ">> >> USB: on (needs DMCP5 3.58)"); return; }
  snprintf(b, (size_t)n, "cable %s  PC port %s  redraws %d", sw(SERIO_IS_ACTIVE) > 0 ? "in" : "OUT",
           sw(SERIO_IS_CONNECTED) > 0 ? "open" : "closed", redraws);
#endif
}

/* the main loop's wait while the USB is on */
void usbser_idle(uint32_t ms) {
#ifdef SERCON_TINYUSB
  usbcon_idle(ms);                                /* the CPU runs the USB: no sleep (it would stop the USB) */
#else
  sys_timer_start(0, ms);                         /* DMCP runs the USB: sleep; a timer, a key or key 77 wakes */
  CLR_ST(STAT_RUNNING);
  sys_sleep();
#endif
}
