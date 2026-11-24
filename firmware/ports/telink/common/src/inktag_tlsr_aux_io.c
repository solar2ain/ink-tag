#include "inktag_tlsr_aux_io.h"

#include <string.h>

#include "tl_common.h"
#include "drivers.h"
#include "inktag_status.h"

#if INKTAG_BUILD_PRO
#include "inktag_tlsr_aux_state_pro.inc"
#endif

void inktag_tlsr_aux_io_defaults(inktag_tlsr_aux_io_config_t *config) {
    if (!config) return;
    memset(config, 0, sizeof(*config));
    for (unsigned i=0u;i<INKTAG_AUX_BUTTON_COUNT;i++)
        config->buttons[i].gpio = INKTAG_TLSR_AUX_GPIO_UNUSED;
    config->reserved[1] = INKTAG_TLSR_AUX_GPIO_UNUSED;
    config->reserved[2] = INKTAG_TLSR_AUX_GPIO_UNUSED;
    config->reserved_word = 2000u;
}

#if INKTAG_BUILD_PRO
#include "inktag_tlsr_aux_io_pro.inc"
#endif
