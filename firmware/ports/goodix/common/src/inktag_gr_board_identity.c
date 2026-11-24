#include "inktag_gr_board_identity.h"

#include <string.h>


static uint8_t s_board;

static bool board_allowed(uint8_t board, uint8_t mask) {
    return board > INKTAG_GR_BOARD_UNKNOWN && board < 8u &&
           (mask & (uint8_t)(1u << board)) != 0u;
}

uint8_t inktag_gr_board_identity(void) { return s_board; }

void inktag_gr_board_identity_invalidate(void) { s_board = INKTAG_GR_BOARD_UNKNOWN; }

#if defined(INKTAG_GR_PIN_CONFIG_PORT)
uint8_t inktag_gr_board_identity_configured(inktag_gr_pin_storage_t *state,
                                           uint8_t family, uint8_t allowed_boards) {
    inktag_gr_pin_factory_t factory;
    inktag_gr_pin_user_t user;
    inktag_gr_board_identity_invalidate();
    if (state && state->facts && state->facts->family == family &&
        board_allowed(state->facts->board, allowed_boards) &&
        inktag_gr_pin_storage_read(state, &factory, &user))
        s_board = state->facts->board;
    return s_board;
}
#endif
