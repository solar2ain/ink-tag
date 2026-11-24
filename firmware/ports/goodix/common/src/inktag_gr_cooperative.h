#ifndef INKTAG_GR_COOPERATIVE_H
#define INKTAG_GR_COOPERATIVE_H

/* Main-context, bus-released yield; never call inside a Flash critical section. */
void inktag_gr_cooperative_maintenance(void);

#endif
