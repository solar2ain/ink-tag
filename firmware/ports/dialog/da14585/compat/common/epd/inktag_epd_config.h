/* InkTag configuration persistence interface. Public field order and names
 * below are retained ABI constraints; backend transaction details stay in C.
 * Storing policy bits is not authorization to execute a Pro-only operation. */
#ifndef INKTAG_EPD_CONFIG_H
#define INKTAG_EPD_CONFIG_H
#include <stdbool.h>
#include <stdint.h>

#include "inktag_ble_identity.h"

#include "inktag_epd_screen.h"
#include "inktag_input.h"
#if defined(INKTAG_NRF_PROTOCOL_V2)
#include "inktag_display_settings.h"
#endif

/* Thirteen single-byte fields, including the reserved screen byte. Keep
 * native storage/board consumers compatible without introducing a new format. */
typedef struct {
    uint8_t mosi_pin;
    uint8_t sclk_pin;
    uint8_t cs_pin;
    uint8_t dc_pin;
    uint8_t rst_pin;
    uint8_t busy_pin;
    uint8_t bs_pin;
    uint8_t reserved_screen_legacy; /* Kept only to preserve the board-config record layout. */
    uint8_t wakeup_pin;
    uint8_t led_pin;
    uint8_t en_pin;
    uint8_t display_mode;
    uint8_t week_start;
#if defined(INKTAG_DIALOG)
    uint8_t enable_active_low; /* RAM only; the common pin16 owns persisted polarity. */
#endif
} epd_config_t;

/* Display state is a separate persistence domain from board wiring and panel
 * configuration. The argument is mode-specific: clear color for CLEAR, stored
 * record ID for PICTURE/SLIDESHOW, and 0xFF when no replay source exists. */
typedef enum {
    EPD_DISPLAY_STATE_PICTURE = 0,
    EPD_DISPLAY_STATE_CALENDAR = 1,
    EPD_DISPLAY_STATE_CLOCK = 2,
    EPD_DISPLAY_STATE_CLEAR = 3,
    EPD_DISPLAY_STATE_SLIDESHOW = 4,
    EPD_DISPLAY_STATE_DEVICE_INFO = 5,
} epd_display_state_mode_t;

#define EPD_DISPLAY_STATE_NO_SOURCE 0xFFu

/* Display policy reuses the existing reserved field so every platform keeps
 * the same persistent-record layout. Boot replay is opt-in. The historical
 * suspended bit stays reserved/readable but has no scheduling effect; an
 * operation failure must not permanently disable automatic refresh. */
#define EPD_DISPLAY_STATE_FLAG_BOOT_REFRESH           0x0001u
#define EPD_DISPLAY_STATE_WEEK_START_SHIFT             1u
#define EPD_DISPLAY_STATE_WEEK_START_MASK              0x000Eu
#define EPD_DISPLAY_STATE_FLAG_BRAND_HIDDEN            0x0010u
#define EPD_DISPLAY_STATE_FLAG_AUTO_REFRESH_SUSPENDED 0x8000u /* Retired; never reuse. */
#define EPD_DISPLAY_STATE_USER_FLAGS_MASK \
    EPD_DISPLAY_STATE_FLAG_BOOT_REFRESH
#define EPD_DISPLAY_STATE_PRO_FLAGS_MASK \
    EPD_DISPLAY_STATE_FLAG_BRAND_HIDDEN
#define EPD_DISPLAY_STATE_KNOWN_FLAGS_MASK \
    (EPD_DISPLAY_STATE_USER_FLAGS_MASK | \
     EPD_DISPLAY_STATE_PRO_FLAGS_MASK | \
     EPD_DISPLAY_STATE_WEEK_START_MASK | \
     EPD_DISPLAY_STATE_FLAG_AUTO_REFRESH_SUSPENDED)

/* The low-level driver config only needs picture/calendar/clock. Keep the
 * richer clear/slideshow/device-info state in its dedicated current record and
 * project non-native states to the driver's picture mode. */
static inline uint8_t epd_display_state_driver_mode(uint8_t mode) {
    return (mode == EPD_DISPLAY_STATE_CALENDAR || mode == EPD_DISPLAY_STATE_CLOCK) ?
           mode : EPD_DISPLAY_STATE_PICTURE;
}

typedef struct {
    uint8_t mode;
    uint8_t argument;
    uint16_t reserved;
    uint32_t interval_seconds;
} epd_display_state_t;

#define EPD_DEVICE_INFO_QR_SLOT_COUNT 1u
#define EPD_DEVICE_INFO_QR_MAX_MODULES 57u
#define EPD_DEVICE_INFO_QR_MAX_BYTES 407u

typedef struct {
    uint8_t modules;
    uint8_t version;
    uint16_t length;
    uint8_t data[EPD_DEVICE_INFO_QR_MAX_BYTES];
} epd_device_info_qr_t;

#define EPD_DEVICE_NAME_MAX_BYTES INKTAG_BLE_DEVICE_NAME_MAX_BYTES
#define EPD_DEVICE_LABEL_MAX_BYTES 24u
#define EPD_DEVICE_SETTINGS_VERSION 1u

/* Device identity and input bindings are persistent device settings. They are
 * independent from panel profiles, board wiring and optional LED hardware. */
typedef struct {
    uint8_t version;
    uint8_t name_length;
    uint8_t label_length;
    uint8_t name[EPD_DEVICE_NAME_MAX_BYTES];
    uint8_t label[EPD_DEVICE_LABEL_MAX_BYTES];
    inktag_button_bindings_t button;
} epd_device_settings_t;

void epd_device_settings_default(epd_device_settings_t* settings);
bool epd_device_name_valid(const uint8_t* name, uint8_t length);
bool epd_device_label_valid(const uint8_t* label, uint8_t length);
bool epd_device_settings_read(epd_device_settings_t* settings);
bool epd_device_settings_write(const epd_device_settings_t* settings);
void epd_device_settings_clear(void);

#define EPD_CONFIG_SIZE (sizeof(epd_config_t) / sizeof(uint8_t))

/* Reads project the selected backend into the wiring ABI. An all-255 buffer
 * denotes no usable projection. A void write/clear is not a durable receipt;
 * callers needing cleanup evidence use epd_user_settings_clear_and_verify. */
void epd_config_init(epd_config_t* cfg);
void epd_config_read(epd_config_t* cfg);
void epd_config_write(epd_config_t* cfg);
void epd_config_clear(epd_config_t* cfg);
bool epd_config_empty(epd_config_t* cfg);
bool epd_time_read(uint32_t* timestamp);
bool epd_time_write(uint32_t timestamp);
#if defined(INKTAG_DIALOG)
/* Existing DA local-time record bounds; shared by admission and persistence.
 * This does not extend the record format or enable diagnostic time tests. */
#define DIALOG_TIME_MIN_VALID 1704067200UL
#define DIALOG_TIME_MAX_VALID 4102444800UL
#endif
#if defined(INKTAG_GOODIX) || defined(INKTAG_ONMICRO) || \
    defined(INKTAG_DIALOG) || defined(INKTAG_NRF_PROTOCOL_V2)
/* V2 keeps timezone in the same persistent state as the local display
 * timestamp. Persistence alone does not prove wall-clock continuity after an
 * uncontrolled restart; a port may return false until explicit time sync. */
bool epd_time_state_read(uint32_t* timestamp, int16_t* timezone_minutes);
bool epd_time_state_write(uint32_t timestamp, int16_t timezone_minutes);
#if defined(INKTAG_ONMICRO)
/* OM's always-on PMU counter survives the controlled application -> Recovery
 * -> application reset chain. This variant records that counter and arms a
 * power-loss-cleared marker so boot can account for time spent in Recovery. */
bool epd_time_state_write_reboot_anchor(uint32_t timestamp,
                                        int16_t timezone_minutes);
#endif
#endif
bool epd_screen_config_read(epd_screen_config_t* screen);
bool epd_screen_config_write(const epd_screen_config_t* screen);
#if FDS_ENABLED || defined(INKTAG_NRF_COMPACT_STORE)
uint32_t epd_screen_config_last_write_error(void);
#endif
void epd_screen_config_clear(void);
bool epd_display_state_read(epd_display_state_t* state);
bool epd_display_state_write(const epd_display_state_t* state);
void epd_display_state_clear(void);
#if defined(INKTAG_NRF_PROTOCOL_V2)
/* Nordic stores ordinary Display v3 preferences with the existing ID7 device
 * settings, separate from ID4 replay references. Sibling identity/input and
 * display writes preserve each other via the existing atomic journal put. */
bool epd_display_settings_read(inktag_display_settings_t* settings);
bool epd_display_settings_write(const epd_display_state_t* state,
                                const inktag_display_settings_t* settings);
/* After mutation admission only; boot does not write. Preserve installed ID4
 * preferences in ID7 and retire ID4 v1 before a cleanup can remove authority. */
bool epd_display_settings_prepare(void);
#endif
bool epd_device_info_qr_read(uint8_t slot, epd_device_info_qr_t* qr);
#if defined(INKTAG_DIALOG)
/* Dialog lifecycle adapters use the common record classification and an
 * atomic Settings-Journal generation switch. Image records remain a separate
 * storage domain and are cleared by the protocol adapter. */
bool epd_settings_reset_and_verify(void);
/* Preserve ordinary native preferences before clearing image replay records. */
bool epd_user_data_settings_prepare_and_verify(void);
bool epd_user_data_settings_clear_and_verify(void);
#endif

/* Clears only user-owned configuration and display records, then verifies
 * that they are absent. Activation, factory selection, calibration, recovery
 * journals and image records belong to separate persistence domains. */
bool epd_user_settings_clear_and_verify(epd_config_t* current_config);

#endif
