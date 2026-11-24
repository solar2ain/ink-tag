#ifndef INKTAG_TLSR_CLOCK_H
#define INKTAG_TLSR_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

#include "inktag_timebase.h"
#include "inktag_tlsr.h"

/* Select the 32 kHz source before cpu_wakeup_init(), as required by the B85
 * SDK. All boards sharing one application must currently agree on this early
 * source; an incompatible board needs an early selector adapter. */
void inktag_tlsr_clock_boot_select(void);

/* Read the SDK's recovered 16 MHz monotonic system counter, independent of
 * the CPU clock. It pauses in suspend, but the selected LFCLK recovery
 * handler advances the counter before application code resumes. */
uint32_t inktag_tlsr_clock_now(void);

/* Resolve exact-board clock facts without encoding them in product logic. */
bool inktag_tlsr_clock_profile(inktag_board_id_t board,
                               inktag_timebase_profile_t *profile);

#endif
