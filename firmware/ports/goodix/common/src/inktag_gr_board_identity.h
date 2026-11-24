#ifndef INKTAG_GR_BOARD_IDENTITY_H
#define INKTAG_GR_BOARD_IDENTITY_H

#include <stdbool.h>
#include <stdint.h>

/* IDs are private to this family, not screen/controller IDs. */
enum {
    INKTAG_GR_BOARD_UNKNOWN = 0,
    INKTAG_GR_BOARD_ZKC21V = 1,
    INKTAG_GR_BOARD_ZKC42V = 2,
    INKTAG_GR_BOARD_ZKC42VM = 3,
};

/* Configured IBD1/GPF1 + matching offline-seeded NVDS are the only authority.
 * No legacy selector, guessed backend, ordinary boot seed or repair writer. */
uint8_t inktag_gr_board_identity(void);
void inktag_gr_board_identity_invalidate(void);
#if defined(INKTAG_GR_PIN_CONFIG_PORT)
#include "inktag_gr_pin_storage.h"
uint8_t inktag_gr_board_identity_configured(inktag_gr_pin_storage_t *state,
                                           uint8_t family, uint8_t allowed_boards);
#endif

#endif
