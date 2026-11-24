#ifndef INKTAG_BUILD_PRO
#define INKTAG_BUILD_PRO 1
#endif
#if INKTAG_BUILD_PRO
#include "inktag_gr_aux_io_backend_pro.inc"
#else
#include "inktag_gr_aux_io.h"
/* The maintained exact GR defaults enable no stock software key. Optional
 * UART-pad buttons are user customization; never load their record
 * or touch GPIO in base. Wire/storage records remain intact elsewhere. */
bool inktag_gr_aux_io_decode(const uint8_t*w,uint16_t n,inktag_gr_aux_io_config_t*out){(void)w;(void)n;(void)out;return false;}
bool inktag_gr_aux_io_encode(const inktag_gr_aux_io_config_t*c,uint8_t w[INKTAG_AUX_IO_WIRE_SIZE]){(void)c;(void)w;return false;}
void inktag_gr_aux_io_init(bool enabled){(void)enabled;}
bool inktag_gr_aux_io_supported(void){return false;}
bool inktag_gr_aux_io_busy(void){return false;}
bool inktag_gr_aux_io_reset(void){return false;}
void inktag_gr_aux_io_process(uint32_t now){(void)now;}
void inktag_gr_aux_io_get(inktag_gr_aux_io_config_t*c){if(c)*c=(inktag_gr_aux_io_config_t){.button_gpio=255u,.button_flags=3u};}
bool inktag_gr_aux_io_set(const inktag_gr_aux_io_config_t*c,const char**e){(void)c;if(e)*e="unsupported";return false;}
bool inktag_gr_aux_io_take_button(inktag_button_gesture_t*g){(void)g;return false;}
bool inktag_gr_aux_io_simulate_button(inktag_button_gesture_t g){(void)g;return false;}
#endif
