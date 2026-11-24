#ifndef INKTAG_BOARD_H
#define INKTAG_BOARD_H

/* Reserved AUX bytes retain their persisted/wire positions without a backend. */
#ifndef INKTAG_AUX_RESERVED_ENABLED
#define INKTAG_AUX_RESERVED_ENABLED 0
#endif
#if INKTAG_AUX_RESERVED_ENABLED != 0
#error "Reserved AUX output has no implementation"
#endif

#if !defined(NRF52811_XXAA)
#error "nrf52811_aesl requires the nRF52811 port"
#endif

#define INKTAG_BOARD_ID "aesl22_nrf"
#define INKTAG_BOARD_DISPLAY_NAME "nrf52811_asel22"
#define INKTAG_BOARD_ARTIFACT "InkTag-nrf52811_aesl"

/* The current EL022H2 fixture uses the original Alibaba/Hema 2.13-inch BWR
 * panel path (HINK-E0213A67 / IL3897-family command stream). The controller
 * has a 128-pixel RAM stride around a 122x250 visible area. Other replaceable
 * panels remain selectable through the persisted runtime screen record. */
#define INKTAG_BOARD_DEFAULT_SCREEN_CONFIG \
    {EPD_SCREEN_SCHEMA_VERSION, EPD_SCREEN_DRIVER_SSD1619, INKTAG_RECIPE_SSD1619_HEMA_213_BWR, EPD_SCREEN_COLOR_BWR, 3u, 128u, 250u, 0u, 0u, 122u, 250u}

/* The application keeps the complete common controller/profile catalog.
 * Compiled recipes are selectable through the persisted screen record; that
 * does not qualify a particular fitted panel on this exact board. */
#define INKTAG_COMPACT_BUSY_DIAG 1
#define INKTAG_BOARD_SETTINGS_SIZE 0x2000UL
#define INKTAG_BOARD_DFU_SETTINGS_START 0x2F000UL
#define INKTAG_BOARD_RAM_START 0x200022D8UL
#define INKTAG_BOARD_RAM_SIZE 0x3D28UL

/* EL022H2 VER06 uses a 512 KiB FM25Q04 on P0.12-P0.15. The first
 * two sectors hold settings, the next 32 KiB hold GUI assets and reserved
 * space, and the remaining sectors hold image records. */
#define INKTAG_BOARD_EXTERNAL_FLASH_JEDEC_ID 0xA14013u
#define INKTAG_BOARD_EXTERNAL_FLASH_CAPACITY 0x00080000u
#define INKTAG_BOARD_IMAGE_STORE_EXTERNAL 1
#define INKTAG_BOARD_EXTERNAL_FLASH_SETTINGS_START 0x00000000u
#define INKTAG_BOARD_EXTERNAL_FLASH_ASSET_OFFSET 0x00002000u
#define INKTAG_BOARD_EXTERNAL_FLASH_ASSET_SIZE 0x00003000u
#define INKTAG_BOARD_EXTERNAL_FLASH_ASSET_REGION_SIZE 0x00008000u
#define INKTAG_BOARD_IMAGE_STORE_START 0x0000A000UL
#define INKTAG_BOARD_IMAGE_STORE_SIZE 0x00076000UL
#define INKTAG_BOARD_EXTERNAL_FLASH_CS_PIN 12u
#define INKTAG_BOARD_EXTERNAL_FLASH_MISO_PIN 13u
#define INKTAG_BOARD_EXTERNAL_FLASH_SCLK_PIN 14u
#define INKTAG_BOARD_EXTERNAL_FLASH_MOSI_PIN 15u

/* The 0.2 core implementation is present, but capabilities that require an
 * exact-board electrical or functional result remain undiscoverable until
 * that evidence exists. EL022H2 exposes TX=P0.25 and RX=P0.26; the candidate
 * has no qualified stock input; custom bindings are absent in InkTag. */
#define INKTAG_BOARD_IMAGE_STORE_QUALIFIED 1
#define INKTAG_BOARD_INPUT_QUALIFIED 0
#define INKTAG_BOARD_LIFECYCLE_QUALIFIED 0
#define INKTAG_BOARD_TELEMETRY_QUALIFIED 0
#define INKTAG_BOARD_ACTIVATION_QUALIFIED 0
#define INKTAG_BOARD_FIND_QUALIFIED 0
#define INKTAG_BOARD_FIND_EXPERIMENTAL_AVAILABLE 0
#define INKTAG_BOARD_HEALTH_QUALIFIED 0
/* Read-only software diagnostics are available independently of electrical
 * qualification. The health wire keeps its unqualified-threshold flag, and
 * absent/failed sensor samples remain invalid. No new sampling timer or rail. */
#define INKTAG_BOARD_HEALTH_SUPPORTED 1
#define INKTAG_BOARD_TELEMETRY_SUPPORTED 1
/* Software scope qualification is independent of panel/electrical tests. */
#define INKTAG_BOARD_LIFECYCLE_V2_SUPPORTED 1
#define INKTAG_BOARD_POWER_THRESHOLDS_QUALIFIED 0
#define INKTAG_BOARD_EXPANSION_TX_PIN 25u
#define INKTAG_BOARD_EXPANSION_RX_PIN 26u
#define INKTAG_BOARD_EXPANSION_QUALIFIED 0

/* RGB channels use P0.16/P0.17/P0.18 and are active low. */
#define INKTAG_BOARD_HAS_STATUS_LED 1
#define INKTAG_BOARD_RGB_STATUS_LED_QUALIFIED 1
#define INKTAG_BOARD_LED_RED_PIN 16u
#define INKTAG_BOARD_LED_GREEN_PIN 17u
#define INKTAG_BOARD_LED_BLUE_PIN 18u
#define INKTAG_BOARD_STATUS_LED_CHANNEL_COUNT 3u
#define INKTAG_BOARD_STATUS_LED_CHANNEL_0_PIN 0x10u
#define INKTAG_BOARD_STATUS_LED_CHANNEL_1_PIN 0x11u
#define INKTAG_BOARD_STATUS_LED_CHANNEL_2_PIN 0x12u
#define INKTAG_BOARD_STATUS_LED_PIN 0x12u
#define INKTAG_BOARD_STATUS_LED_ACTIVE_LOW 1
/* The legacy/common EPD lamp and Recovery output both use qualified blue. */
#define INKTAG_BOARD_RECOVERY_STATUS_LED_QUALIFIED 1
#define INKTAG_BOARD_BUTTON_PRESENT 0
/* BLE/storage access does not own the EPD rail. Display work and a direct
 * frame's BEGIN..refresh lifetime each acquire exactly one driver reference. */
#define INKTAG_BOARD_LAZY_DISPLAY_BUS 1
/* Preserve the hardware 4 MHz transfers; CS is GPIO-owned so a checked
 * command plus half-duplex response can retain one uninterrupted selection. */
#define INKTAG_EPD_SPI_SOFTWARE_CS 1
#define INKTAG_BOARD_PANEL_TEMPERATURE_ENABLED 1
/* Use the same bounded completion window as the generic SSD1619 path.
 * This changes only the host wait, never the controller activation/waveform. */
#define INKTAG_EPD_SSD1619_SMALL_REFRESH_TIMEOUT_MS 65535u

/* This board definition owns only the physical EPD wiring. Controller,
 * geometry, color and transform come from the persisted screen configuration. */
#define INKTAG_BOARD_DEFAULT_EPD_CONFIG \
    {0x14, 0x13, 0x06, 0x05, 0x04, 0x03, 0x02, 0x10, 0x11, 0x12, 0x07}

/* Shared pin16 defaults: display seven roles, RGB, reserved UART FF.
 * LFXO P0.00/01, board BS P0.02, NOR P0.12..15 and RESET P0.21 are
 * not configurable resources. All remaining routes still undergo the common
 * complete-group, uniqueness, polarity and reserved-role checks. */
#define INKTAG_BOARD_DEFAULT_PIN_WIRE \
    {1u,0u,0x80u,0x03u,20u,19u,6u,5u,4u,3u,7u,16u,17u,18u,255u,255u}
#define INKTAG_BOARD_CONFIG_GPIO_MASK UINT32_C(0xffdf0ff8)
#define INKTAG_BOARD_PIN_EDITABLE_ROLES UINT16_C(0x03ff)
#define INKTAG_BOARD_EPD_BS_PIN 2u

#ifndef INKTAG_NRF_BOARD_CLOCK_H
#define INKTAG_NRF_BOARD_CLOCK_H

/* The currently observed EL022H2-class blue PCB has a fitted 32.768 kHz
 * crystal in addition to its marked 32.000 MHz crystal. The exact LF crystal
 * part/tolerance is not identified, so advertise a conservative 50 ppm to
 * the SoftDevice until a datasheet or measurement supports a tighter value. */
#define INKTAG_BOARD_LFCLK_SOURCE 1
#define INKTAG_BOARD_LFCLK_RC_CTIV 0
#define INKTAG_BOARD_LFCLK_RC_TEMP_CTIV 0
#define INKTAG_BOARD_LFCLK_ACCURACY 5
#define INKTAG_BOARD_LFCLK_ACCURACY_PPM 50

#endif

#endif
