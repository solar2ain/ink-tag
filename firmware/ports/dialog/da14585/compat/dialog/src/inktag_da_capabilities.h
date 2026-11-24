#ifndef INKTAG_DA_CAPABILITIES_H
#define INKTAG_DA_CAPABILITIES_H

#include "inktag_epd_screen.h"

#ifndef INKTAG_BOARD_TRANSFER_PACKBITS_RLE_QUALIFIED
#define INKTAG_BOARD_TRANSFER_PACKBITS_RLE_QUALIFIED 0
#endif

#ifndef INKTAG_DA_PACKBITS_CANDIDATE
#define INKTAG_DA_PACKBITS_CANDIDATE 0
#endif
static inline bool inktag_da_transfer_packbits_available(void)
{
    return INKTAG_BOARD_TRANSFER_PACKBITS_RLE_QUALIFIED != 0 ||
           INKTAG_DA_PACKBITS_CANDIDATE != 0;
}

/* IDs remain in the shared registry. Omission is explicit, not normalization
 * to another panel recipe or a successful no-op. */
static inline bool inktag_da_screen_profile_omitted(const uint8_t *wire, uint16_t length)
{
    return wire != NULL && length == EPD_SCREEN_WIRE_SIZE &&
        (wire[1] == EPD_SCREEN_DRIVER_SSD1675 ||
         wire[1] == EPD_SCREEN_DRIVER_UC8151 ||
         wire[1] == EPD_SCREEN_DRIVER_JD79665CA ||
         (wire[1] == EPD_SCREEN_DRIVER_SSD1677 && wire[3] == EPD_SCREEN_COLOR_GRAY4));
}

static inline bool inktag_da_transfer_object_supported(uint8_t object)
{
    return object == INKTAG_TRANSFER_OBJECT_DIRECT_FRAME ||
           object == INKTAG_TRANSFER_OBJECT_STORED_IMAGE;
}

#endif
