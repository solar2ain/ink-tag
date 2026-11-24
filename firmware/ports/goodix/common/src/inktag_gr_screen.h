#ifndef INKTAG_GR_SCREEN_H
#define INKTAG_GR_SCREEN_H
#include "inktag_epd_screen.h"
#include "inktag_protocol_v2.h"
#include "inktag_gr_settings_store.h"

/* Exact authoritative ID3 read. Missing alone may use factory RAM defaults;
 * malformed/IO errors never authorize a boot repair. No writes or GPIO IO. */
inktag_gr_settings_read_result_t inktag_gr_screen_read(epd_screen_config_t *out);
/* One durable ID3 transaction with fresh power and exact readback. A failed
 * attempted commit is unconfirmed, even if readback observes the new value. */
inktag_status_v2_t inktag_gr_screen_commit(const epd_screen_config_t *screen);
#endif
