#ifndef INKTAG_OM_PANEL_IDLE_H
#define INKTAG_OM_PANEL_IDLE_H

#include <stdint.h>
#include "inktag_board.h"
#include "inktag_om_board_identity.h"

/* Exact-board electrical fact only; an absent pin must never be driven.
 * Keep boot handoff and completed/cancelled refresh teardown consistent. */
static inline uint8_t inktag_om_panel_idle_en_pin(void) {
    inktag_om_board_t board = inktag_om_board_identity();
    return board == INKTAG_OM_BOARD_ZKC21B ||
           board == INKTAG_OM_BOARD_ZKC42B ||
           board == INKTAG_OM_BOARD_ZKC21V ?
        INKTAG_BOARD_ZKCXXB_PANEL_EN_PIN : UINT8_MAX;
}

#endif
