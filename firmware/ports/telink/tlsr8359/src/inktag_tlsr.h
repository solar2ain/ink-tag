#ifndef INKTAG_TLSR_H
#define INKTAG_TLSR_H

#include <stdbool.h>
#include <stdint.h>
#include "tl_common.h"
#include "drivers.h"
#include "app_config.h"

#include "inktag_tlsr_flash_layout.h"
#include "inktag_pin_config.h"
#include "inktag_epd_screen.h"

typedef enum {
    INKTAG_BOARD_UNKNOWN = 0,
    INKTAG_BOARD_NOWA_154R_N = 1,
    INKTAG_BOARD_NEBULAR_420R_N = 2,
    INKTAG_BOARD_NOWA_213R_N = 3,
    INKTAG_BOARD_NEBULAR_213R_N = 4,
    INKTAG_BOARD_NEBULAR_PRO_154Q_N = 5,
    INKTAG_BOARD_HSEL3_266Q = 6,
    INKTAG_BOARD_HSEL5101_200BWR = 7,
    INKTAG_BOARD_STELLAR_154R_N = 8,
    INKTAG_BOARD_STELLAR_PRO_154Q_N = 9,
    INKTAG_BOARD_HSEL5_154M02 = 10,
} inktag_board_id_t;

typedef struct __attribute__((packed)) {
    uint8_t magic[4];
    uint8_t version;
    uint8_t board_id;
    uint16_t reserved;
    uint32_t checksum;
} inktag_board_selector_t;

/* Explicit physical qualification, not panel/date/UID auto-detection. */
#define INKTAG_TLSR_SELECTOR_BASE_IMAGE_STORE_ONLY 1u
#define INKTAG_TLSR_SELECTOR_PIN_RECORD_REQUIRED 2u
#define INKTAG_TLSR_SELECTOR_TYPED_ADDRESS_REQUIRED 4u
bool inktag_tlsr_board_high_image_extent_allowed(void);
bool inktag_tlsr_board_pin_record_required(void);
/* Validated ITBD flags, including explicit unconfigured0+4; 0xFF refuses.
 * This does not qualify or select an optional peripheral backend. */
uint8_t inktag_tlsr_board_selector_flags(void);

/* Exact-board capability/qualification facts are flattened into the runtime
 * profile once.  Vendor-common code must query these bits instead of growing
 * a second board-name switch for every feature. */
#define INKTAG_TLSR_BOARD_NFC_PRESENT_QUALIFIED       (1u << 0)
#define INKTAG_TLSR_BOARD_NFC_FIELD_ACTION_AVAILABLE  (1u << 1)
#define INKTAG_TLSR_BOARD_NFC_FIELD_ACTION_QUALIFIED  (1u << 2)
#define INKTAG_TLSR_BOARD_NFC_TYPE4_AVAILABLE         (1u << 3)
#define INKTAG_TLSR_BOARD_NFC_TYPE4_QUALIFIED         (1u << 4)
#define INKTAG_TLSR_BOARD_INDICATOR_QUALIFIED         (1u << 5)
#define INKTAG_TLSR_BOARD_LED_DIAGNOSTIC_AVAILABLE    (1u << 6)
/* Feature bit 7 is retired; exact available status channels are represented
 * by board_profile.indicator_colors instead of a second result-scene gate. */
#define INKTAG_TLSR_BOARD_AUX_IO_QUALIFIED            (1u << 8)
#define INKTAG_TLSR_BOARD_LIFECYCLE_QUALIFIED         (1u << 9)
#define INKTAG_TLSR_BOARD_ACTIVATION_QUALIFIED        (1u << 10)
#define INKTAG_TLSR_BOARD_FIND_QUALIFIED              (1u << 11)
#define INKTAG_TLSR_BOARD_FIND_EXPERIMENT_AVAILABLE   (1u << 12)
#define INKTAG_TLSR_BOARD_OTA_RECOVERY_QUALIFIED      (1u << 13)
#define INKTAG_TLSR_BOARD_POWER_THRESHOLDS_QUALIFIED  (1u << 14)
#define INKTAG_TLSR_BOARD_GUI_ASSET_UPDATE_QUALIFIED  (1u << 15)
#define INKTAG_TLSR_BOARD_PACKBITS_RLE_QUALIFIED      (1u << 16)
/* Contact format fact, not RF type or release qualification. */
#define INKTAG_TLSR_BOARD_NFC_STORAGE55               (1u << 17)
/* Same recovered contact wiring; stock chooses NC00/NC55 from the chip's
 * read-only manufacturer/type header, not from a panel/board-name guess. */
#define INKTAG_TLSR_BOARD_NFC_DETECT_CONTACT_FORMAT    (1u << 18)
/* Exact native contact-init transcript; requires positive NC55 readback. */
#define INKTAG_TLSR_BOARD_NFC_NATIVE55_INIT            (1u << 19)
/* Native A4/03 contact base init, without HSEL3's FFE6=55 mode write. */
#define INKTAG_TLSR_BOARD_NFC_NATIVE_BASE_INIT         (1u << 20)

/* Values0..3 keep the SDK's uniform pull encoding. Value4 is an exact-PCB
 * signal-release experiment, not an inferred controller-wide sleep policy. */
#define INKTAG_TLSR_EPD_PARK_RESET_CS_UP_FLOAT_OTHER 4u

/* Internal descriptors, not Factory/wire headers. SDK GPIO encodings and the
 * optional AUX index/sentinel fit16bit. Keep wide facts first so their word
 * offsets remain aligned even under the vendor packed ABI. */
typedef struct __attribute__((aligned(4))) {
    /* Historical name: qualified accessible prefix used for bounded storage,
     * not a JEDEC/address-window inference of total physical die capacity. */
    uint32_t physical_flash_bytes;
    uint32_t feature_flags;
    uint32_t lfclk_source_frequency_hz;
    uint32_t time_counter_ticks_per_second;
    uint32_t time_counter_mask;
    uint32_t lfclk_accuracy_ppm;
    uint16_t epd_mosi;
    uint16_t epd_clk;
    uint16_t epd_cs;
    uint16_t epd_dc;
    uint16_t epd_reset;
    uint16_t epd_busy;
    uint16_t epd_enable;
    uint16_t led_red;
    uint16_t led_green;
    uint16_t led_blue;
    uint16_t supply_adc;
    uint16_t aux_button_gpio;
    uint16_t aux_reserved_0;
    /* Screen geometry/recipe/transform live only in the current screen
     * configuration; do not retain an unused second copy per PCB. */
    uint8_t epd_park_pull;
    uint8_t epd_power_settle_ms;
    uint8_t epd_bus_settle_ms;
    uint8_t indicator_colors;
    uint8_t aux_reserved_1;
    uint8_t aux_reserved_2;
    uint8_t lfclk_source;
    uint8_t lfclk_quality;
    bool epd_busy_park_float;
    bool epd_hardware_spi;
    bool epd_reset_high_low_high;
    bool high_image_extent;
    bool lfclk_qualified;
} inktag_tlsr_board_profile_t;

/* Compact mutable GPIO view. Immutable panel/PM/NFC/ADC facts are referenced,
 * not copied into another full board descriptor. SDK pin encodings fit16bit. */
typedef struct __attribute__((aligned(4))) {
    const inktag_tlsr_board_profile_t *base;
    uint16_t epd_mosi, epd_clk, epd_cs, epd_dc, epd_reset, epd_busy, epd_enable;
    uint16_t led_red, led_green, led_blue;
    bool epd_hardware_spi, epd_busy_active_high, epd_enable_active_low, epd_busy_known;
    uint8_t indicator_colors;
} inktag_tlsr_gpio_profile_t;

/* Stack-only preflight result. No new persistent record or global cache. */
typedef struct {
    inktag_pin_config_t defaults, effective;
    bool overridden, busy_known, busy_active_high;
} inktag_tlsr_pin_candidate_t;

/* Recovery consumes only indicator routes and the safety ADC route. Keeping
 * this view separate lets its linker discard panel/NFC/App profile tables.
 * GPIO encodings fit 16 bits; this is not a new persistent or wire format. */
typedef struct __attribute__((aligned(4))) {
    uint16_t led_red;
    uint16_t led_green;
    uint16_t supply_adc;
    uint8_t indicator_colors;
} inktag_tlsr_recovery_profile_t;
const inktag_tlsr_recovery_profile_t *inktag_tlsr_board_recovery_profile(
    inktag_board_id_t board);

enum {
    INKTAG_TLSR_DEEP_RETENTION_ADV = 1u << 0,
    INKTAG_TLSR_DEEP_RETENTION_CONN = 1u << 1,
};

inktag_board_id_t inktag_board_detect(void);
int16_t inktag_tlsr_board_clock_rate_default(inktag_board_id_t board);
const char *inktag_board_name(void);
uint32_t inktag_fnv1a(const uint8_t *data, uint16_t length);
const inktag_tlsr_board_profile_t *inktag_tlsr_board_profile(
    inktag_board_id_t board);
/* Immutable board facts stay separate from the validated GPIO view. Supply,
 * NFC, Flash, clock and PM policy must continue to use board_profile(). */
void inktag_tlsr_pin_routes_init(inktag_board_id_t board);
const inktag_tlsr_gpio_profile_t *inktag_tlsr_gpio_profile(void);
bool inktag_tlsr_pin_routes_available(void);
void inktag_tlsr_pin_routes_invalidate(void);
bool inktag_tlsr_factory_screen(uint8_t wire[17]);
bool inktag_tlsr_pin_routes_validate(const uint8_t wire[16]);
bool inktag_tlsr_pin_routes_prepare(const epd_screen_config_t *screen,
                                    inktag_tlsr_pin_candidate_t *candidate);
bool inktag_tlsr_pin_routes_prepare_override(const uint8_t wire[16], bool override,
                                             inktag_tlsr_pin_candidate_t *candidate);
void inktag_tlsr_pin_routes_apply(const inktag_tlsr_pin_candidate_t *candidate);
bool inktag_tlsr_pin_routes_refresh(void);
bool inktag_tlsr_pin_routes_status(uint8_t wire[36]);
uint8_t inktag_tlsr_pin_routes_led_active_low(void);
bool inktag_tlsr_board_device_info_qr_qualified(inktag_board_id_t board);
bool inktag_tlsr_board_first_display_requested(void);
bool inktag_tlsr_board_consume_first_display_request(void);

#endif
