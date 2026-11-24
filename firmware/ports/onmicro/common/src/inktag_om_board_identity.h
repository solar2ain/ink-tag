#ifndef INKTAG_OM_BOARD_IDENTITY_H
#define INKTAG_OM_BOARD_IDENTITY_H

#include <stdbool.h>
#include <stdint.h>

#include "inktag_epd_screen.h"

typedef enum {
    INKTAG_OM_BOARD_UNKNOWN = 0,
    INKTAG_OM_BOARD_ZKC21B = 1,
    INKTAG_OM_BOARD_ZKC42B = 2,
    INKTAG_OM_BOARD_ZKC21V = 3,
    /* Selector 4 is reserved for the legacy board family. */
} inktag_om_board_t;

/* Read-only after NVDS: Full seeds identity offline. Selected native markers
 * must match it; absence/corruption never causes boot provisioning or repair. */
void inktag_om_board_identity_init(void);
inktag_om_board_t inktag_om_board_identity(void);
const char *inktag_om_board_id(void);
/* Retained API returns false: factory seeding is never ordinary boot work. */
bool inktag_om_board_factory_provisioning_active(void);
bool inktag_om_board_default_screen(epd_screen_config_t *screen);
/* Retained API returns false: boot never reseeds a profile or board record. */
bool inktag_om_board_requires_default_screen(void);

#endif
