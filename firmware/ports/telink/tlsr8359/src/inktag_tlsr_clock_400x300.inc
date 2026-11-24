#if INKTAG_BUILD_PRO
#include "inktag_tlsr_clock_400x300_pro.inc"
#else
/* Unsigned feature-cost oracle: ordinary full-clock rendering stays intact. */
static const bool s_clock_transfer_started=false;
bool inktag_epd_clock_partial_supported(void) {return false;}
static void tlsr_clock_invalidate(void) {}
static bool tlsr_clock_draw(gui_data_t *data) {(void)data;return false;}
#endif
