#ifndef INKTAG_TLSR_EPD_H
#define INKTAG_TLSR_EPD_H

#include <stdbool.h>
#include <stdint.h>
#include "inktag_tlsr.h"
#include "inktag_epd_screen.h"
#include "inktag_protocol_v2.h"
#include "inktag_gui.h"
#include "inktag_brand_overlay.h"

typedef enum {
    INKTAG_EPD_RESULT_OK = 0,
    INKTAG_EPD_RESULT_NOT_READY,
    INKTAG_EPD_RESULT_NO_BUSY_EDGE,
    INKTAG_EPD_RESULT_TIMEOUT,
    INKTAG_EPD_RESULT_STOPPED,
    INKTAG_EPD_RESULT_IO_ERROR,
    INKTAG_EPD_RESULT_PON_TIMEOUT,
    INKTAG_EPD_RESULT_DRF_TIMEOUT,
    INKTAG_EPD_RESULT_POF_TIMEOUT,
    INKTAG_EPD_RESULT_DENIED,
    INKTAG_EPD_RESULT_UNSUPPORTED,
} inktag_epd_result_t;

void inktag_epd_configure(inktag_board_id_t board);
/* Cold boot only; validated current-screen/routes gated, never draws pixels. */
void inktag_epd_cold_boot_sleep(inktag_board_id_t board);
void inktag_epd_restore_gpio(void);
void inktag_epd_resume(void);
const epd_screen_config_t *inktag_epd_screen(void);
inktag_status_v2_t inktag_epd_control_access(const epd_screen_config_t *screen);
inktag_status_v2_t inktag_epd_set_screen(const epd_screen_config_t *screen);
void inktag_epd_init(void);
#if INKTAG_EPD_TEMPERATURE_OPTION
/* Fixed for this flow; normal entry reads, NFC image entry skips. */
void inktag_epd_init_with_temperature(bool read_temperature);
#endif
bool inktag_epd_clear(uint8_t color);
/* Keep the initialized panel active after one full clear/refresh pass. This is
 * reserved for bounded foreground sequences such as Ghost Clean; the caller
 * must finish with inktag_epd_sleep(). */
bool inktag_epd_clear_pass(uint8_t color);
void inktag_epd_command(uint8_t command);
void inktag_epd_data(const uint8_t *data, uint16_t length);
void inktag_epd_write_ram(bool begin, bool black, const uint8_t *data, uint16_t length);
bool inktag_epd_refresh(void);
void inktag_epd_sleep(void);
/* Foreground-only bounded wake/read/sleep; never writes pixels or refreshes. */
bool inktag_epd_sample_temperature(void);
void inktag_epd_stop(void);
#if INKTAG_TRANSFER_LZO1X_ENABLED || INKTAG_TLSR_NFC_FMSC_PILOT
uint8_t *inktag_epd_transfer_workspace(uint32_t *capacity);
#endif
#if INKTAG_TLSR_NFC_FMSC_PILOT && INKTAG_BUILD_PRO
#define inktag_epd_nfc_workspace inktag_epd_transfer_workspace
#include "inktag_nfc_fmsc.h"
bool inktag_epd_write_nfc_chunk(inktag_nfc_fmsc_t *image,uint32_t offset,uint8_t *pixels,uint16_t length);
#endif
bool inktag_epd_show_gui(display_mode_t mode, uint32_t timestamp, uint8_t week_start,
                         const gui_countdown_t *countdown,
                         const char *device_name);
bool inktag_epd_show_stored(uint8_t record_id,
                            inktag_brand_edition_t edition);
bool inktag_epd_clock_partial_supported(void);
bool inktag_epd_busy(void);
bool inktag_epd_busy_raw(void);
uint8_t inktag_epd_active_driver_id(void);
uint8_t inktag_epd_controller_temperature(void);
uint8_t inktag_epd_controller_temperature_fraction(void);
bool inktag_epd_controller_temperature_valid(void);
/* Completed, bounded-age sample only; never performs peripheral IO. */
bool inktag_epd_panel_temperature_sixteenths(int16_t *value);
bool inktag_epd_effective_temperature(int8_t *celsius);
inktag_epd_result_t inktag_epd_last_result(void);
/* First failed BUSY wait's command, captured before shutdown/parking. */
uint8_t inktag_epd_failed_command(void);
uint16_t inktag_epd_voltage(void);

#endif
