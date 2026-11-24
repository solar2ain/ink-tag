#ifndef INKTAG_GR_PANEL_DIAGNOSTIC_H
#define INKTAG_GR_PANEL_DIAGNOSTIC_H
#include "inktag_panel_diagnostic.h"
#if INKTAG_PANEL_DIAGNOSTIC_ENABLED
/* Internal bus API, never a public arbitrary-register interface. */
bool inktag_gr_panel_read_start(uint8_t command, uint16_t skip, uint8_t length);
bool inktag_gr_panel_read_poll(uint8_t *output, uint8_t capacity,
                              uint8_t *written, bool *done);
bool inktag_gr_panel_read_power_off(void);
bool inktag_gr_panel_diag_write(uint8_t command, const uint8_t *data, uint8_t length);
#endif
#endif
