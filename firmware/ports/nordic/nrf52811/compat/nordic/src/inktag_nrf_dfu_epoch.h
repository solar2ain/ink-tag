#ifndef INKTAG_NRF_DFU_EPOCH_H
#define INKTAG_NRF_DFU_EPOCH_H

#include <stdbool.h>
#include <stdint.h>

/* Nordic's signed init command has one 32-bit firmware-version field. Keep
 * product release identity in its low half and the cross-platform compatible
 * epoch in its high half. The Bootloader version field is otherwise unused by
 * InkTag's application-only Recovery and temporarily holds a candidate until
 * the new application reaches service/advertising readiness. */
#define INKTAG_NRF_DFU_COMPATIBILITY_EPOCH_SHIFT 16u
#define INKTAG_NRF_DFU_NO_PENDING_VERSION        0u
#define INKTAG_NRF_DFU_CONFIRM_GPREGRET           0xB4u
#define INKTAG_NRF_DFU_PENDING_VERSION_WORD       3u

static inline uint16_t inktag_nrf_dfu_compatibility_epoch(uint32_t version)
{
    return (uint16_t)(version >> INKTAG_NRF_DFU_COMPATIBILITY_EPOCH_SHIFT);
}

static inline bool inktag_nrf_dfu_version_allowed(uint32_t candidate,
                                                  uint32_t confirmed)
{
    return inktag_nrf_dfu_compatibility_epoch(candidate) >=
           inktag_nrf_dfu_compatibility_epoch(confirmed);
}

#endif
