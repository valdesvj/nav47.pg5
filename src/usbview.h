/* usbview.h - NAV47: the screen to the PC over USB, for tools/dm42view.py. Two builds of the same interface:
 * src/sercon.c (make, USB=sercon: DMCP5 3.58's own serial console streams the LCD) and src/usbview.c
 * (make USB=tinyusb: our own TinyUSB serial port and text frames).
 * GPL-3.0-or-later. */
#ifndef NAV47_USBVIEW_H
#define NAV47_USBVIEW_H

void usbview_on(void);       /* the USB port as a serial port; learns the LCD buffer's layout */
void usbview_off(void);
int  usbview_is_on(void);
void usbview_send(void);
void usbview_redraw_request(void);   /* DMCP's key 77 (serial console: the PC asked for the whole screen) */
#define USBVIEW_KEY_REDRAW 77
void usbview_state(char *a, char *b, int n);   /* a line for INFO: the USB state, for checking on the calculator */     /* the whole LCD (as shown, menu bar too) as one text frame */

#endif
