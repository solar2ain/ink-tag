#ifndef INKTAG_NRF_INTEGRITY_HASH_H
#define INKTAG_NRF_INTEGRITY_HASH_H
#include <stdbool.h>
#include <stdint.h>
#if defined(INKTAG_CODE_GUARDS) && INKTAG_CODE_GUARDS
bool inktag_nrf_code_sha256(const uint8_t *data, uint32_t length, uint8_t output[32]);
#endif
bool inktag_nrf_dfu_sha256_matches_le(uint8_t const expected[32],
                                      uint8_t const *data,
                                      uint32_t length);
#endif
