#ifndef INKTAG_BOARD_H
#define INKTAG_BOARD_H

#if !defined(INKTAG_GOODIX_GR5331)
#error "Requires the Goodix GR5331 port"
#endif

#define INKTAG_FIRMWARE_FAMILY_ID "gr5331_zkel"
#define INKTAG_BOARD_ARTIFACT "InkTag-gr5331_zkc21v"

/* No exact ZKC21V frequency calibration is qualified yet. */
#define INKTAG_BOARD_CLOCK_RATE_PPM 0

/* Only the measured 128x250 / 122x250 SSD1680-compatible factory recipe.
 * Runtime also checks wiring, geometry, controller metadata and temperature.
 * This does not qualify arbitrary replacement panels or other GR families. */
#define INKTAG_BOARD_CLOCK_PARTIAL_AVAILABLE 1
#define INKTAG_BOARD_CLOCK_PARTIAL_EPD_CONFIG INKTAG_BOARD_DEFAULT_EPD_CONFIG

/* The installed SSD1680 permits 20 MHz writes. Use 4 MHz for the exact
 * ZKC21V hardware-SPIM signal-path qualification; physical SCLK and optical
 * refresh evidence must be recorded before this is release-qualified. */
#define INKTAG_BOARD_EPD_SPI_HZ 4000000u

/* Offline activation uses the shared Goodix UID/RNG/ECC/NVDS adapter and the
 * same frozen IKAC1 protocol as GR551x. Lifecycle, optional NFC and auxiliary
 * GPIO remain separately gated until their exact-board tests pass. */
#define INKTAG_BOARD_ACTIVATION_QUALIFIED 1
/* 42D8 exact-board two-action matrix passed 2026-10-09: images-only clear,
 * password-admitted/fresh-W settings reset, OFF reset and Pro/Find retention.
 * Physical interrupted-write qualification remains separately tracked. */
#ifndef INKTAG_BOARD_LIFECYCLE_QUALIFIED
#define INKTAG_BOARD_LIFECYCLE_QUALIFIED 1
#endif
#define INKTAG_BOARD_AUX_IO_QUALIFIED 0
#ifndef INKTAG_BOARD_PIN_CONFIG_QUALIFIED
#define INKTAG_BOARD_PIN_CONFIG_QUALIFIED 0
#endif
#define INKTAG_BOARD_NFC_VARIANT_QUALIFIED 0
#define INKTAG_BOARD_FIND_QUALIFIED 0
#define INKTAG_BOARD_GUI_ASSET_UPDATE_QUALIFIED 1
#define INKTAG_BOARD_TRANSFER_PACKBITS_RLE_QUALIFIED 1
/* Package the same public-key/time-multiplex control path as GR551x for
 * explicit bench use. No GR5331 Find RF/report qualification exists yet. */
#define INKTAG_BOARD_FIND_EXPERIMENTAL_AVAILABLE 1

/* This describes the shipped panel only. Pins, SPI wiring, BUSY polarity and
 * board power paths remain in the board configuration below. */
#define INKTAG_BOARD_DEFAULT_SCREEN_CONFIG \
    {EPD_SCREEN_SCHEMA_VERSION, EPD_SCREEN_DRIVER_SSD1680, INKTAG_RECIPE_SSD1680_ZKC21V_FACTORY, EPD_SCREEN_COLOR_BWR, 9u, 128u, 250u, 0u, 0u, 122u, 250u}

/* Portable order: MOSI, CLK, CS, DC, RST, BUSY, BS, reserved, WAKEUP, LED, EN.
 * Goodix pin encoding uses bits 0..5 for the pin and bits 6..7 for the GPIO
 * group. ZKC21V's panel rail is AON6 (0x46). */
#define INKTAG_BOARD_DEFAULT_EPD_CONFIG \
    {0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0xFF, 0xFF, 0xFF, 0xFF, 0x46}

#define INKTAG_BOARD_HAS_STATUS_LED 1
#define INKTAG_BOARD_HAS_LED_DIAG 1
#define INKTAG_BOARD_STATUS_LED_PIN 0x80
#define INKTAG_BOARD_LED_BLUE_PIN 0x80
#define INKTAG_BOARD_LED_GREEN_PIN 0x81
#define INKTAG_BOARD_LED_RED_PIN 0x82
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
