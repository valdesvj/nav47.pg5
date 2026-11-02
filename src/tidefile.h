/* tidefile.h - NAV47 tide station (.TID) and FLOW atlas (.FLW) files. GPL-3.0-or-later. */
#ifndef NAV47_TIDEFILE_H
#define NAV47_TIDEFILE_H
#include "tide.h"
int tid_parse(char *text, TRegion **regs, int *nregs, char *err, int errlen);
int flw_parse(char *text, FlowAtlas *fl, char *err, int errlen);
#endif
