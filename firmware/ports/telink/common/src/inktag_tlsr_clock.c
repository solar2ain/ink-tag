#include "inktag_tlsr_clock.h"

#include "tlsr8359_hsel.h"
#include "stack/ble/ble.h"

#if INKTAG_NOWA_LFCLK_SOURCE != INKTAG_NEBULAR_LFCLK_SOURCE
#error "Shared TLSR application needs an early board selector for mixed LFCLK sources"
#endif

void inktag_tlsr_clock_boot_select(void)
{
#if INKTAG_NOWA_LFCLK_SOURCE == INKTAG_LFCLK_SOURCE_INTERNAL_RC
    blc_pm_select_internal_32k_crystal();
#elif INKTAG_NOWA_LFCLK_SOURCE == INKTAG_LFCLK_SOURCE_LFXO
    blc_pm_select_external_32k_crystal();
#else
#error "TLSR boards must select a qualified internal-RC or LFXO source"
#endif
}

uint32_t inktag_tlsr_clock_now(void)
{
    return clock_time();
}

bool inktag_tlsr_clock_profile(inktag_board_id_t board,
                               inktag_timebase_profile_t *profile)
{
    const inktag_tlsr_board_profile_t *board_profile;
    if (profile == NULL) return false;
    board_profile = inktag_tlsr_board_profile(board);
    if (board_profile == NULL) return false;
    profile->source = board_profile->lfclk_source;
    profile->quality = board_profile->lfclk_quality;
    profile->qualified = board_profile->lfclk_qualified;
    profile->source_frequency_hz = board_profile->lfclk_source_frequency_hz;
    profile->counter_ticks_per_second =
        board_profile->time_counter_ticks_per_second;
    profile->counter_mask = board_profile->time_counter_mask;
    profile->accuracy_ppm = board_profile->lfclk_accuracy_ppm;
    return inktag_timebase_profile_valid(profile);
}
