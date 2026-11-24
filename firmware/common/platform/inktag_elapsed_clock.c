#include "inktag_elapsed_clock.h"

inktag_elapsed_time_t inktag_elapsed_clock_sample_time_shared(
    inktag_elapsed_clock_t *clock, uint32_t now_tick)
{
    return inktag_elapsed_clock_sample_time(clock, now_tick);
}
