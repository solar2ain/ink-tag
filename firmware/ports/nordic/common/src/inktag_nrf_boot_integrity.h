#ifndef INKTAG_NRF_BOOT_INTEGRITY_H
#define INKTAG_NRF_BOOT_INTEGRITY_H
#include <stdbool.h>
#include <stdint.h>
/* Native settings carry the complete stored length/CRC and boot-validation
 * CRC. Read/check them; never recalculate an expected value from the App. */
bool inktag_nrf_boot_integrity_valid(uint32_t image_size, uint32_t expected_crc);
bool inktag_nrf_dfu_settings_pair_matches(void);
#if defined(INKTAG_CODE_GUARDS) && INKTAG_CODE_GUARDS
/* One-shot Recovery-owned admission consumed by the native handoff. */
extern uint32_t inktag_nrf_verified_app_size;
#endif
#endif
