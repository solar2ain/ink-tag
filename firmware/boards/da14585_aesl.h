#ifndef INKTAG_BOARD_H
#define INKTAG_BOARD_H

#define INKTAG_BOARD_ID "da14585_aesl"
#define INKTAG_BOARD_ARTIFACT "InkTag-da14585_aesl"
#define INKTAG_BOARD_DA_AESL29_ID "aesl29_da"
#define INKTAG_BOARD_DA_AESL42_ID "aesl42_da"
/* Display-only lowercase MCU_board / board names. Historical wire IDs and
 * stock selectors above are stable; never derive one from these labels. */
#define INKTAG_BOARD_DA_AESL29_DISPLAY_NAME "da14585_asel29"
#define INKTAG_BOARD_DA_AESL29_DISPLAY_NAME_SHORT "asel29"
#define INKTAG_BOARD_DA_AESL42_DISPLAY_NAME "da14585_asel42"
#define INKTAG_BOARD_DA_AESL42_DISPLAY_NAME_SHORT "asel42"
#define INKTAG_BOARD_HAS_STATUS_LED 0
#define INKTAG_BOARD_BUTTON_PRESENT 0
#define INKTAG_DEVICE_SETTINGS_ENABLED 1
#define INKTAG_BOARD_FREE_ONLY 1
#define INKTAG_BOARD_IMAGE_STORE_AVAILABLE 1
/* Host-tested implementation; exact-board decoded transfer/CRC/stack matrix
 * is still required. Offline -PackBitsCandidate is not a release build. */
#define INKTAG_BOARD_TRANSFER_PACKBITS_RLE_QUALIFIED 0

/* The shared AESL application uses one Core 8 KiB Settings Journal. Recovery
 * owns 0x037000 and 0x03B000 independently. The otherwise unused 0x036000
 * sector and final 0x03E000-0x03FFFF pair hold one split, read-only GUI asset
 * image; neither range is a Settings generation. */
#define INKTAG_BOARD_SETTINGS_START 0x0003C000u
#define INKTAG_BOARD_SETTINGS_SIZE 0x00002000u
#define INKTAG_BOARD_SETTINGS_PROFILE_CORE_8K 1
#define INKTAG_BOARD_EXTERNAL_FLASH_ASSET_A_START 0x00036000u
#define INKTAG_BOARD_EXTERNAL_FLASH_ASSET_A_SIZE 0x00001000u
#define INKTAG_BOARD_EXTERNAL_FLASH_ASSET_B_START 0x0003E000u
#define INKTAG_BOARD_EXTERNAL_FLASH_ASSET_B_SIZE 0x00002000u
#define INKTAG_BOARD_EXTERNAL_FLASH_ASSET_IMAGE_SIZE 0x00003000u

/* Reserve the repository-wide expansion roles without guessing an AESL pad.
 * A photographed, traced TX/RX pair must replace 0xFF and pass conflict,
 * wake and PWM tests before the backend or capabilities can be enabled. */
#define INKTAG_BOARD_AUX_IO_BACKEND_AVAILABLE 0
#define INKTAG_BOARD_AUX_IO_QUALIFIED 0
#define INKTAG_BOARD_AUX_UART_TX_PORT 0xFFu
#define INKTAG_BOARD_AUX_UART_TX_PIN 0xFFu
#define INKTAG_BOARD_AUX_UART_RX_PORT 0xFFu
#define INKTAG_BOARD_AUX_UART_RX_PIN 0xFFu
#define INKTAG_BOARD_AUX_BUTTON_ACTIVE_LOW 1
#define INKTAG_BOARD_AUX_BUZZER_PASSIVE 1
/* The SDK6 ADC supply conversion is implemented for both buck and boost paths.
 * Exact 65CA/asel29 RAM reads matched this App and found valid 3290/3320 mV
 * samples. Publish that real policy sample in GET_STATUS, not a default.
 * This does not qualify ADC accuracy, load thresholds or reset diagnostics;
 * the broader HEALTH gate stays separate and unchanged. */
#define INKTAG_BOARD_DA_AESL29_VOLTAGE_TELEMETRY_AVAILABLE 1
#define INKTAG_BOARD_BATTERY_TELEMETRY_QUALIFIED 0
#define INKTAG_BOARD_DA_AESL42_BATTERY_TELEMETRY_QUALIFIED 1
#define INKTAG_BOARD_POWER_THRESHOLDS_QUALIFIED 0
#define INKTAG_BOARD_LIFECYCLE_QUALIFIED 0
/* Automatic product-contract implementation; not a physical qualification. */
#define INKTAG_BOARD_LIFECYCLE_V2_IMPLEMENTED 1
/* A 2026-09-14 AESL-4.2 exact-board run lost ordinary BLE advertising after
 * the 30-second transition into SDK6 extended sleep; a direct SWD attach/wake
 * made it visible again. Keep both AESL selectors on the event-aware
 * mode_idle/WFI path until advertising, reconnect, wake-source and current
 * measurements qualify a deeper state. */
#define INKTAG_BOARD_EXTENDED_SLEEP_QUALIFIED 0
#define INKTAG_BOARD_DA_AESL42_EXTENDED_SLEEP_QUALIFIED 0
/* Explicit bench opt-in only. Never promotes either board's qualification. */
#define INKTAG_DA_EXTENDED_SLEEP_CANDIDATE 0
/* One deferred temperature transaction per link; physical qualification pending. */
#define INKTAG_DA_CONNECTION_TEMPERATURE_CANDIDATE 0

/* This describes the shipped panel only. Pins, SPI wiring, BUSY polarity and
 * board power paths remain in the board configuration below. */
#define INKTAG_BOARD_DEFAULT_SCREEN_CONFIG \
    {EPD_SCREEN_SCHEMA_VERSION, EPD_SCREEN_DRIVER_UC8176, INKTAG_RECIPE_UC8176_STANDARD, EPD_SCREEN_COLOR_BWR, 0u, 400u, 300u, 0u, 0u, 400u, 300u}
/* Fonts, the product QR and lunar lookup tables are read from the split
 * external-Flash asset image. The GUI working budget is split between a
 * 12-line asset cache and the page buffer: Huawei 768x552 therefore
 * renders 10 physical rows per pass after reserving its isolated row-reorder
 * state (56 passes) while avoiding thousands of slow serial-
 * Flash reads per page. Shared pages, drivers, recipes and output bytes stay
 * intact. */
#define INKTAG_BOARD_GUI_FRAME_BUFFER_BYTES 2304
#ifndef INKTAG_BOARD_GUI_ASSET_CACHE_LINES
#define INKTAG_BOARD_GUI_ASSET_CACHE_LINES 12
#endif

/* Offline Full resource candidate; not adopted in normal/release builds. */
#ifndef INKTAG_DA_NAMED_QR_CANDIDATE
#define INKTAG_DA_NAMED_QR_CANDIDATE 0
#endif

/* Shared AESL-2.9/AESL-4.2 panel bus confirmed from the stock startup path.
 * It reads 0x39000, sees override mode 1, expands the eight bytes at 0x39008,
 * and consumes them in CS,BS,RST,SCLK,MOSI,DC,BUSY,EN order:
 * P2_1,P2_2,P1_0,P0_1,P2_0,P0_7,P1_1,P2_3.
 *
 * InkTag portable order is MOSI, CLK, CS, DC, RST, BUSY, BS, reserved,
 * WAKEUP, LED, EN. The boards have no status LED, so LED stays 0xFF.
 * The reserved byte stays 0xFF; it is not a controller or panel selector. */
#define INKTAG_BOARD_DEFAULT_EPD_CONFIG \
    {0x20, 0x01, 0x21, 0x07, 0x10, 0x11, 0x22, 0xFF, 0xFF, 0xFF, 0x23}

#define INKTAG_BOARD_DA_AESL29_SCREEN_CONFIG \
    {EPD_SCREEN_SCHEMA_VERSION, EPD_SCREEN_DRIVER_SSD1619, INKTAG_RECIPE_SSD1619_STANDARD, EPD_SCREEN_COLOR_BWR, 1u, 128u, 296u, 0u, 0u, 128u, 296u}
#define INKTAG_BOARD_DA_AESL29_STOCK_SELECTOR 0x02
#define INKTAG_BOARD_DA_AESL42_STOCK_SELECTOR 0x03

#endif
