/* usbview.h - NAV47: the screen to the PC over the USB serial port (acm_puts), for tools/dm42view.py.
 * GPL-3.0-or-later. */
#ifndef NAV47_USBVIEW_H
#define NAV47_USBVIEW_H

void usbview_on(void);       /* the USB port as a serial port; learns the LCD buffer's layout */
void usbview_off(void);
int  usbview_is_on(void);
void usbview_send(void);
void usbview_state(char *a, char *b, int n);   /* a line for INFO: the USB state, for checking on the calculator */     /* the whole LCD (as shown, menu bar too) as one text frame */

#endif
