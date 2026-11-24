#ifndef INKTAG_NRF_FINDMY_H
#define INKTAG_NRF_FINDMY_H
#include <stdbool.h>
#include "inktag_findmy.h"
#include "inktag_findmy_public_store.h"

void inktag_nrf_findmy_init(inktag_findmy_config_t *config);
bool inktag_nrf_findmy_write(const inktag_findmy_config_t *config);
/* CLEAR_CONFIG disables Find but preserves the existing public owner. */
bool inktag_nrf_findmy_clear(void);
bool inktag_nrf_findmy_unregister_valid(const uint8_t payload[41],
                                       const uint8_t activation_id[8]);
bool inktag_nrf_findmy_unregister(bool (*admit)(void *), void *context);
inktag_find_public_store_result_t inktag_nrf_findmy_public_state(void);
bool inktag_nrf_findmy_public_safe(bool first_provision);
bool inktag_nrf_findmy_public_material(uint8_t fingerprint[16], uint8_t key[16]);
bool inktag_nrf_findmy_select(uint64_t unix_seconds, bool time_valid,
    uint64_t *slot, uint8_t public_x[28]);
bool inktag_nrf_findmy_public_install(const uint8_t old_fingerprint[16],
    const uint8_t master_xy[56], const uint8_t key[16], uint64_t unix_seconds,
    uint64_t *slot, uint8_t public_x[28], uint8_t fingerprint[16],
    bool (*admit)(void *), void *admit_context);
#endif
