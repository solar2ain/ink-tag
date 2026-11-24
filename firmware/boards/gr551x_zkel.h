#ifndef INKTAG_BOARD_H
#define INKTAG_BOARD_H

#if !defined(INKTAG_GOODIX_GR5515)
#error "Requires the Goodix GR551x port"
#endif

#if defined(INKTAG_GR5513_INTERNAL_IMAGES)
#define INKTAG_FIRMWARE_FAMILY_ID "gr5513_zkel_internal"
#else
#define INKTAG_FIRMWARE_FAMILY_ID "gr551x_zkel"
#endif
#define INKTAG_BOARD_ARTIFACT "InkTag-gr551x_zkc42v"

/* UTC now uses BLE half-slots. Do not carry the old AON-specific +3180 ppm
 * trial into this source. Both boards default to zero pending exact-board
 * long-interval calibration; runtime overrides remain RAM-only. */
#define INKTAG_BOARD_CLOCK_RATE_PPM 0
#define INKTAG_BOARD_ZKC42V_CLOCK_RATE_PPM 0

/* GR5513 ZKC42V P4/P3 hardware SPIM: 4 MHz (64 MHz core / BAUD 16) passed
 * exact-board 2557 optical clear/calendar on UC8276, Huawei A0/A1, Hanshow
 * Nebular and SSD1677 gray4; the latter three also passed registered colour
 * or grayscale image fixtures. The earlier 1 MHz failure was not reproduced
 * on a controlled retry. GR5515 ZKC42VM P4/P16 uses GPIO SPI and is
 * unaffected by this prescaler. */
#define INKTAG_BOARD_EPD_SPI_HZ 4000000u

/* Product capability qualification is separate from code packaging. This
 * shared artifact contains both ZKC42VM and ZKC42V, so runtime board identity
 * still gates NFC and runtime RGB access. */
#define INKTAG_BOARD_ACTIVATION_QUALIFIED 1
#define INKTAG_BOARD_LIFECYCLE_QUALIFIED 1
#define INKTAG_BOARD_AUX_IO_QUALIFIED 1
#ifndef INKTAG_BOARD_PIN_CONFIG_QUALIFIED
#if defined(INKTAG_GR5513_INTERNAL_IMAGES)
/* Exact 21V routes; persisted whole-tuple writes reuse the shared validator. */
#define INKTAG_BOARD_PIN_CONFIG_QUALIFIED 1
#else
#define INKTAG_BOARD_PIN_CONFIG_QUALIFIED 0
#endif
#endif
#if defined(INKTAG_GR5513_INTERNAL_IMAGES)
#define INKTAG_BOARD_NFC_VARIANT_QUALIFIED 0
#else
#define INKTAG_BOARD_NFC_VARIANT_QUALIFIED 1
#endif
#define INKTAG_BOARD_FIND_QUALIFIED 0
#define INKTAG_BOARD_GUI_ASSET_UPDATE_QUALIFIED 1
#define INKTAG_BOARD_TRANSFER_PACKBITS_RLE_QUALIFIED 1
/* Public-only P-224 Find v2 remains an opt-in test capability, not a
 * production-qualified Find My accessory. Exact-board report/power/soak
 * qualification is tracked separately; never enable it by default. */
#define INKTAG_BOARD_FIND_EXPERIMENTAL_AVAILABLE 1

/* ZKC42VM expansion pads. Wire values are the public GPIO numbers; pin masks
 * are the Goodix SDK representation consumed only by the vendor adapter. */
#define INKTAG_BOARD_AUX_BUTTON_GPIO 26u
#define INKTAG_BOARD_AUX_BUTTON_PIN_MASK 0x04000000u

/* ZKC42V-only contact route, top mark GT / N692 / 226 on 2557. Reviewed
 * transactions match a first-generation GT23SC6699/NTAG-I2C-shaped map;
 * the complete die revision is not proven. Do not label this GT23NT081D.
 * The common image uses these pins only after the ZKC42V selector. */
#define INKTAG_BOARD_NFC_I2C_ADDRESS 0x55u
#define INKTAG_BOARD_NFC_SCL_PIN 0x00000001u
#define INKTAG_BOARD_NFC_SDA_PIN 0x00000002u
#define INKTAG_BOARD_NFC_ENABLE_PIN 0x00000400u
#define INKTAG_BOARD_NFC_FIELD_PIN 0x00000008u

/* GR5515 ZKC42VM (FC34) uses the SSD1619-compatible70-byte path; the
 * GR5513 ZKC42VM assembly retains its SSD1683 profile. ZKC42V shares this application but
 * has a UC8276-compatible HE042AD7A2 panel selected by the Full-image
 * descriptor. Existing saved UC8176/recipe-1 records remain valid and are
 * not silently rewritten: a profile change must account for stored images. */
#define INKTAG_BOARD_GR5513_SCREEN_CONFIG \
    {EPD_SCREEN_SCHEMA_VERSION, EPD_SCREEN_DRIVER_SSD1683, INKTAG_RECIPE_SSD1683_ZKC42VM_FACTORY, EPD_SCREEN_COLOR_BWR, 0u, 400u, 300u, 0u, 0u, 400u, 300u}
#if defined(INKTAG_GR5513_INTERNAL_IMAGES)
#define INKTAG_BOARD_DEFAULT_SCREEN_CONFIG INKTAG_BOARD_GR5513_SCREEN_CONFIG
#else
#define INKTAG_BOARD_DEFAULT_SCREEN_CONFIG \
    {EPD_SCREEN_SCHEMA_VERSION, EPD_SCREEN_DRIVER_SSD1619, INKTAG_RECIPE_SSD1619_STANDARD, EPD_SCREEN_COLOR_BWR, 0u, 400u, 300u, 0u, 0u, 400u, 300u}
#endif
#define INKTAG_BOARD_ZKC42V_SCREEN_CONFIG \
    {EPD_SCREEN_SCHEMA_VERSION, EPD_SCREEN_DRIVER_UC8276, INKTAG_RECIPE_UC8276_ZKC42V_HE042AD7A2, EPD_SCREEN_COLOR_BWR, 0u, 400u, 300u, 0u, 0u, 400u, 300u}
#define INKTAG_BOARD_GR5513_SCREEN_DRIVER EPD_SCREEN_DRIVER_SSD1683

/* Portable order: MOSI, CLK, CS, DC, RST, BUSY, BS, reserved, WAKEUP, LED, EN. */
#define INKTAG_BOARD_DEFAULT_EPD_CONFIG \
    {0x04, 0x10, 0x19, 0x05, 0x07, 0x06, 0xFF, 0xFF, 0xFF, 0xFF, 0x18}
/* Recovered from PP5513_4.2.bin: P4 data, P3 clock, P2 chip select, P5 DC,
 * P7 reset, P6 BUSY and P24 panel enable. */
#define INKTAG_BOARD_GR5513_EPD_CONFIG \
    {0x04, 0x03, 0x02, 0x05, 0x07, 0x06, 0xFF, 0xFF, 0xFF, 0xFF, 0x18}

/* Default-off build pilot, only the new GR5513 ZKC21V selector1 and the
 * recovered SSD1680 profile/wiring. Does not qualify any 4.2-inch assembly.
 * The shared backend retains controller/B1/temperature/OLD-NEW admission. */
/* Standard GR5515 factory-wiring candidate. Runtime owns the configured
 * SSD1683 recipe/geometry plus defined status/native calibration checks;
 * version/User-ID metadata is diagnostic, not a fixed identity whitelist.
 * Optical qualification remains per actual panel. */
#define INKTAG_BOARD_SSD1683_CLOCK_EPD_CONFIG INKTAG_BOARD_DEFAULT_EPD_CONFIG
#define INKTAG_BOARD_CLOCK_PARTIAL_AVAILABLE 1
#define INKTAG_BOARD_CLOCK_PARTIAL_EPD_CONFIG INKTAG_BOARD_GR5513_EPD_CONFIG

/* EN is active high on both registered GR551x PCB variants.  Leaving P24
 * high impedance after a refresh allows the board-level bias network to
 * repower the panel rail.  Keep the exact-board supply gate actively low
 * while the display is parked. */
#define INKTAG_BOARD_EPD_IDLE_HOLD_ENABLE_LOW 1

#define INKTAG_BOARD_HAS_STATUS_LED 0
#define INKTAG_BOARD_HAS_LED_DIAG 0
#define INKTAG_BOARD_HAS_RUNTIME_RGB_LED 1
#define INKTAG_BOARD_STATUS_LED_PIN 0xFF
/* Recovered from the ZKC42V stock one-by-one R/B/G test and optically
 * confirmed on the v1.2 bench board: MSIO0=red, MSIO1=green and GPIO31=blue.
 * All three channels are active low. Encoded pin types use bit 7 for MSIO and
 * plain values for normal GPIO. */
#define INKTAG_BOARD_LED_RED_PIN 0x80
#define INKTAG_BOARD_LED_BLUE_PIN 0x1F
#define INKTAG_BOARD_LED_GREEN_PIN 0x81
#define INKTAG_BOARD_LED_ACTIVE_LOW 1

/* No exact-board exception to the common 0.2 product policy is qualified. */
#define INKTAG_BOARD_POWER_WARN_MV INKTAG_POWER_POLICY_WARNING_MV
#define INKTAG_BOARD_POWER_DISPLAY_MIN_MV INKTAG_POWER_POLICY_DISPLAY_MIN_MV
#define INKTAG_BOARD_POWER_FLASH_MIN_MV INKTAG_POWER_POLICY_FLASH_MIN_MV
#define INKTAG_BOARD_POWER_OTA_MIN_MV INKTAG_POWER_POLICY_OTA_MIN_MV
#define INKTAG_BOARD_POWER_RECOVERY_HYSTERESIS_MV \
    INKTAG_POWER_POLICY_RECOVERY_HYSTERESIS_MV
#define INKTAG_BOARD_POWER_THRESHOLDS_QUALIFIED 0

#endif
