#ifndef INKTAG_NRF_DFU_HASH_H
#define INKTAG_NRF_DFU_HASH_H
#include <stdbool.h>
#include <stdint.h>
/* Integrity only: the expected digest is supplied by the update package. */
bool inktag_nrf_dfu_sha256_matches_le(uint8_t const expected[32],
                                     uint8_t const *data, uint32_t length);
#endif
