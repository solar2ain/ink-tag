#ifndef INKTAG_GOODIX_PIN_BACKEND_FACTS_H
#define INKTAG_GOODIX_PIN_BACKEND_FACTS_H
#include "inktag_gr_pin_config.h"

/* Trusted executable backend facts, NOT supplied by a host PCB config.
 * Host JSON parity is tested; editing a per-unit file cannot expand masks.
 * The qualified rail pad retains its ownership even when disabled. */
static const inktag_gr_pin_facts_t inktag_gr_pin_backends[] = {
    {0x33u, 1u, 0x31u, {0x00003F00u, 0x40u, 7u}, 255u, 255u, 255u, 70u},
#if !defined(INKTAG_GOODIX_GR5331)
    /* Stock PWM A/B/A masks are NOT pad numbers: MSIO0/MSIO1/GPIO31. */
    {0x55u, 1u, 0x13u, {0x810000FCu, 0u, 3u}, 255u, 255u, 255u, 24u},
#endif
    {0x55u, 2u, 0x13u, {0x810000FCu, 0u, 3u}, 255u, 255u, 255u, 24u},
    {0x55u, 3u, 0x13u, {0x010000FCu, 0u, 0u}, 26u, 30u, 255u, 24u},
    {0x55u, 3u, 0x15u, {0x030100F0u, 0u, 0u}, 26u, 30u, 255u, 24u},
};

static inline const inktag_gr_pin_facts_t *inktag_gr_pin_backend(
    uint8_t family, uint8_t board, uint8_t mcu) {
    for (unsigned i = 0u; i < sizeof(inktag_gr_pin_backends)/sizeof(inktag_gr_pin_backends[0]); ++i) {
        const inktag_gr_pin_facts_t *facts = &inktag_gr_pin_backends[i];
        if (facts->family == family && facts->board == board && facts->mcu == mcu)
            return facts;
    }
    return NULL;
}
#endif
