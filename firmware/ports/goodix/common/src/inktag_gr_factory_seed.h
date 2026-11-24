#ifndef INKTAG_GR_FACTORY_SEED_H
#define INKTAG_GR_FACTORY_SEED_H

#include "inktag_gr_flash_layout.h"
#if defined(INKTAG_GR_PIN_CONFIG_PORT)
#include "inktag_gr_pin_config.h"
static inline void inktag_gr_factory_config_copy(uint8_t out[76]) {
    const volatile uint8_t *selector = (const volatile uint8_t *)(uintptr_t)
        (INKTAG_GR_FLASH_BASE + INKTAG_GR_PIN_SELECTOR_OFFSET);
    const volatile uint8_t *profile = (const volatile uint8_t *)(uintptr_t)
        (INKTAG_GR_FLASH_BASE + INKTAG_GR_PIN_FACTORY_OFFSET);
    for (unsigned i = 0u; i < 12u; ++i) out[i] = selector[i];
    for (unsigned i = 0u; i < 64u; ++i) out[12u+i] = profile[i];
}
_Static_assert(INKTAG_GR_FACTORY_SEED_OFFSET + INKTAG_GR_PIN_SELECTOR_BYTES <=
                   INKTAG_GR_FLASH_SECTOR_SIZE,
               "board seed must stay in existing boot metadata sector zero");
#endif

#endif
