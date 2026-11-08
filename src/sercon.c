/* sercon.c - NAV47: the screen on the PC through DMCP5's own serial console (DMCP5 3.58, IFC 3.18), no TinyUSB.
 *
 * DMCP itself sends the LCD: with SERIO_SEND_LCD on it streams every changed row on channel 0 at each refresh
 * ("L <yy>" + the 400 pixels), and when the PC sends L it passes us the key code 77: we redraw and call
 * lcd_forced_refresh() so every row goes out. So the program only switches the console and the LCD stream on
 * (>> >> USB), handles key 77 (main.c), and gives back the user's DMCP settings when it ends or the USB goes off.
 * tools/dm42view.py shows the stream (Braille in the terminal, or PNG).
 *
 * The same interface as usbview.c (the TinyUSB build, make USB=tinyusb), so main.c does not change.
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
  was_en = sw(SERIO_IS_ENABLED);
  was_lcd = sw(SERIO_SEND_LCD);
  usb_serial_ioctl(SERIO_IS_ENABLED, 1);
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

/* DMCP sends the rows itself at lcd_refresh(); nothing to do here */
void usbview_send(void) {}

/* key 77 (the PC sent L): redraw, then every row goes out */
void usbview_redraw_request(void) { redraws++; lcd_forced_refresh(); }

void usbview_state(char *a, char *b, int n) {
  snprintf(a, (size_t)n, "USB: DMCP serial console %s", on ? "ON, the LCD streamed" : "OFF");
  if (!on) { snprintf(b, (size_t)n, ">> >> USB: on (needs DMCP5 3.58)"); return; }
  snprintf(b, (size_t)n, "cable %s  PC port %s  redraws %d", sw(SERIO_IS_ACTIVE) > 0 ? "in" : "OUT",
           sw(SERIO_IS_CONNECTED) > 0 ? "open" : "closed", redraws);
}

/* the main loop's wait while the USB is on: DMCP runs the USB itself, so the program sleeps as usual; a timer
   wakes it (the clock pages) and a key, or key 77, wakes it at once */
void usbser_idle(uint32_t ms) {
  sys_timer_start(0, ms);
  CLR_ST(STAT_RUNNING);
  sys_sleep();
}
