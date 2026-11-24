#ifndef INKTAG_TLSR_APP_H
#define INKTAG_TLSR_APP_H

#include <stdbool.h>
#include <stdint.h>

#include "inktag_brand_overlay.h"
#include "inktag_epd_screen.h"
#if INKTAG_BUILD_PRO
#include "inktag_app_advertising_pro.h"
#endif

void inktag_app_init(void);
void inktag_app_resume(void);
bool inktag_app_requires_awake(void);
bool inktag_app_debug_grace_active(void);
void inktag_app_start(void);
void inktag_app_process(void);
void inktag_app_cooperative_snapshot(void);
void inktag_app_packet(const uint8_t *data, uint16_t length);
void inktag_app_set_notify(bool enabled);
void inktag_app_set_connected(bool connected);
void inktag_app_disconnected(void);
void inktag_app_set_ota_active(bool active, bool success);
void inktag_app_status_service(void);
void inktag_app_gui_refresh_phase(bool partial);
void inktag_app_gui_prepared(uint32_t gap_bytes, uint32_t prepare_ms,
                             uint32_t stream_ms, uint32_t stream_bytes,
                             uint32_t max_gap_ms);
bool inktag_app_activated(void);
bool inktag_app_clock_partial_allowed(void);
#if defined(INKTAG_TLSR_CLOCK_SSD1683_PILOT) && INKTAG_TLSR_CLOCK_SSD1683_PILOT
/* Private fixed-transaction diagnostics, not a register/LUT command API. */
void inktag_app_clock_pilot_text(const char *text);
#endif
inktag_status_v2_t inktag_app_screen_access(
    const epd_screen_config_t *screen, bool backend_available);
bool inktag_app_nfc_commands_allowed(void);
#if INKTAG_TLSR_NFC_FMSC_PILOT && INKTAG_BUILD_PRO
#include "inktag_nfc_fmsc.h"
bool inktag_app_nfc_image_write(inktag_nfc_fmsc_t *,uint32_t,uint8_t *,uint16_t);
void inktag_app_nfc_image_prepare(void);
bool inktag_app_nfc_image_allowed(void);
bool inktag_app_nfc_image_foreground_busy(void);
#endif
inktag_brand_edition_t inktag_app_brand_edition(void);
bool inktag_app_find_runtime_allowed(bool experiment_enabled);
uint32_t inktag_app_monotonic_ms(void);

#endif
