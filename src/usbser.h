/* usbser.h - NAV47: the main loop's wait while the USB is on (src/sercon.c, both builds). GPL-3.0-or-later. */
#ifndef NAV47_USBSER_H
#define NAV47_USBSER_H
#include <stdint.h>
void usbser_idle(uint32_t ms);   /* instead of sys_sleep() while the USB is on: until a key, or ms milliseconds */
#endif
