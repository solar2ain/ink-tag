#ifndef INKTAG_TIMEBASE_H
#define INKTAG_TIMEBASE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Product-visible clock facts. The source is an exact-board property while
 * counter access and oscillator selection belong to the vendor port. */
#define INKTAG_LFCLK_SOURCE_UNKNOWN     0u
#define INKTAG_LFCLK_SOURCE_INTERNAL_RC 1u
#define INKTAG_LFCLK_SOURCE_LFXO        2u

#define INKTAG_TIMEBASE_QUALITY_UNKNOWN       0u
#define INKTAG_TIMEBASE_QUALITY_NON_PRECISION 1u
#define INKTAG_TIMEBASE_QUALITY_CRYSTAL       2u

typedef struct {
    uint8_t source;
    uint8_t quality;
    bool qualified;
    uint32_t source_frequency_hz;
    uint32_t counter_ticks_per_second;
    uint32_t counter_mask;
    uint32_t accuracy_ppm;
} inktag_timebase_profile_t;

static inline bool inktag_timebase_profile_valid(
    const inktag_timebase_profile_t *profile)
{
    return profile != NULL &&
           profile->source >= INKTAG_LFCLK_SOURCE_INTERNAL_RC &&
           profile->source <= INKTAG_LFCLK_SOURCE_LFXO &&
           profile->quality >= INKTAG_TIMEBASE_QUALITY_NON_PRECISION &&
           profile->quality <= INKTAG_TIMEBASE_QUALITY_CRYSTAL &&
           profile->source_frequency_hz != 0u &&
           profile->counter_ticks_per_second != 0u &&
           profile->counter_mask != 0u &&
           (profile->counter_mask & (profile->counter_mask + 1u)) == 0u;
}

#endif
