#ifndef INKTAG_BOARD_H
#define INKTAG_BOARD_H

#if !defined(NRF52832_XXAA)
#error "nrf52832_priceetag requires the nRF52832 port"
#endif

/* Existing executable compatibility identity, not a numeric board selector or
 * product display label. GET_DEVICE_INFO keeps this exact family token and
 * schema; host Full configuration owns silkscreen/product mappings. This
 * single-backend target needs no new selector, stored record or wire field. */
#define INKTAG_BOARD_FIRMWARE_FAMILY "nrf52832_priceetag"
#define INKTAG_BOARD_ARTIFACT "InkTag-nrf52832_priceetag"
/* No qualified oscillator calibration for this exact board yet. */
#define INKTAG_BOARD_CLOCK_RATE_DEFAULT_PPM 0

/* Retained exact-board LFXO facts, formerly in inktag_nrf_clock.h. The
 * preserved factory image selects {XTAL,0,0,20ppm} at0x2F2A8. No calibration,
 * power policy or runtime clock behavior changes with this consolidation. */
#ifndef INKTAG_NRF_BOARD_CLOCK_H
#define INKTAG_NRF_BOARD_CLOCK_H
#define INKTAG_BOARD_LFCLK_SOURCE 1
#define INKTAG_BOARD_LFCLK_RC_CTIV 0
#define INKTAG_BOARD_LFCLK_RC_TEMP_CTIV 0
#define INKTAG_BOARD_LFCLK_ACCURACY 7
#define INKTAG_BOARD_LFCLK_ACCURACY_PPM 20
#endif

/* This describes the shipped panel only. Pins, SPI wiring, BUSY polarity and
 * board power paths remain in the board configuration below. */
#define INKTAG_BOARD_DEFAULT_SCREEN_CONFIG \
    {EPD_SCREEN_SCHEMA_VERSION, EPD_SCREEN_DRIVER_SSD1675, INKTAG_RECIPE_SSD1675_STANDARD, EPD_SCREEN_COLOR_BW, 0u, 152u, 152u, 0u, 0u, 152u, 152u, 0u}

/* Verified 512-KiB physical Flash; the HW56 160/128-KiB App/image layout is a
 * guarded Full-only transition, not an inferred OTA execution window. Find
 * public P56/K16 lives in Settings ID31, with no dedicated keyring region. */
#define INKTAG_BOARD_FLASH_SIZE 0x80000UL
#define INKTAG_BOARD_APP_START 0x26000UL
#define INKTAG_BOARD_APP_LIMIT 0x28000UL
#define INKTAG_BOARD_DFU_REGION_END 0x4E000UL
#define INKTAG_BOARD_IMAGE_STORE_START 0x60000UL
#define INKTAG_BOARD_IMAGE_STORE_SIZE 0x20000UL
#define INKTAG_BOARD_USER_FONT_START 0x4E000UL
#define INKTAG_BOARD_USER_FONT_SIZE 0x8000UL
#define INKTAG_BOARD_SETTINGS_START 0x56000UL
#define INKTAG_BOARD_SETTINGS_SIZE 0x2000UL
#define INKTAG_BOARD_BOOTLOADER_START 0x58000UL
#ifndef INKTAG_NRF_PASSWORD_QUALIFICATION
#define INKTAG_NRF_PASSWORD_QUALIFICATION 1
#endif
#define INKTAG_BOARD_PASSWORD_QUALIFIED INKTAG_NRF_PASSWORD_QUALIFICATION
/* Exact-board password/AUTH/signed-OTA functional evidence passed. Production
 * interruption, QR optics and key-custody qualification remain separate. */
#define INKTAG_BOARD_MBR_PARAMS_START 0x5E000UL
#define INKTAG_BOARD_DFU_SETTINGS_START 0x5F000UL
#define INKTAG_BOARD_RAM_START 0x20004000UL
#define INKTAG_BOARD_RAM_SIZE 0xC000UL

/* PriceETag has no status LED. The stock BUTTON module reads P0 IN bit 2 and
 * treats zero as pressed, establishing P0.02 active-low. Direct analysis of
 * the preserved stock EPD/SPI routines establishes P0.08-P0.13 below. The
 * stock executable contains a 152x152 B/W compatibility path using the
 * SSD1675 command family and a recovered 100-byte waveform LUT. The installed
 * panel itself is user-confirmed as 200x200, so exact full-panel qualification
 * remains pending. */
#define INKTAG_BOARD_HAS_STATUS_LED 0
/* Keep fixed Recovery LED handling compiled out on this exact board. */
#define INKTAG_BOARD_RECOVERY_STATUS_LED_QUALIFIED 0
#define INKTAG_BOARD_BUTTON_PRESENT 1
#define INKTAG_BOARD_BUTTON_PIN 2
#define INKTAG_BOARD_BUTTON_ACTIVE_LOW 1
#define INKTAG_DEVICE_SETTINGS_ENABLED 1

/* Public configuration may reuse only the six fitted display contacts. P0.07
 * prepare, SWD, ADC and all unproven nets stay outside this mask. A codec does
 * not qualify an extra rail, LED, PWM or button pad. P0.02 remains AUX slot1. */
#define INKTAG_BOARD_DISPLAY_CONFIG_MASK 0x00003F00UL
#define INKTAG_BOARD_DEFAULT_PIN_WIRE \
    {1u, 0u, 0u, 0u, 8u, 9u, 10u, 11u, 12u, 13u, 255u, 255u, 255u, 255u, 255u, 255u}
#define INKTAG_BOARD_DEFAULT_AUX_WIRE \
    {1u, INKTAG_BOARD_BUTTON_PIN, 3u, 255u, 0u, 255u, 0u, 0u, 255u, 255u, 0xD0u, 7u}
/* Existing successful raw SSD1675 reads establish no BUSY inverter for this
 * backend. The driver-specific SSD1680 BW waiter normalization is separate. */
#define INKTAG_BOARD_BUSY_INVERTED 0

/* 0.2.0 capability gates. The typed record store and the active-low P0.02
 * input were qualified on this exact board. Fixed-v5 upload/readback and RLE
 * have current exact-board functional evidence; power-cut qualification is
 * separate. The two-action lifecycle uses the frozen common scopes/password
 * proof and passed actual-port plus NOR fault regressions. Manual destructive
 * and long-power qualification remain explicit outstanding evidence. */
#define INKTAG_BOARD_IMAGE_STORE_QUALIFIED 1
#ifndef INKTAG_NRF_IMAGE_INDEX_QUALIFICATION
#define INKTAG_NRF_IMAGE_INDEX_QUALIFICATION 1
#endif
#define INKTAG_BOARD_IMAGE_INDEX_QUALIFIED INKTAG_NRF_IMAGE_INDEX_QUALIFICATION
/* Historical flag name retained; no persistent image index is linked. */
#ifndef INKTAG_NRF_GUI_ASSET_QUALIFICATION
#define INKTAG_NRF_GUI_ASSET_QUALIFICATION 1
#endif
#define INKTAG_BOARD_GUI_ASSET_UPDATE_QUALIFIED INKTAG_NRF_GUI_ASSET_QUALIFICATION
/* ITFA transaction hosts and exact-board update/readback are functional gates;
 * Full/cut/optics remain production evidence, not silent capability cuts. */
#ifndef INKTAG_NRF_RLE_QUALIFICATION
#define INKTAG_NRF_RLE_QUALIFICATION 1
#endif
#define INKTAG_BOARD_TRANSFER_PACKBITS_QUALIFIED INKTAG_NRF_RLE_QUALIFICATION
#define INKTAG_BOARD_INPUT_QUALIFIED 1
#define INKTAG_BOARD_LIFECYCLE_QUALIFIED 1
#define INKTAG_BOARD_TELEMETRY_QUALIFIED 1
#define INKTAG_BOARD_ACTIVATION_QUALIFIED 1
#define INKTAG_BOARD_FIND_QUALIFIED 0
#ifndef INKTAG_NRF_FIND_PUBLIC_QUALIFICATION
#define INKTAG_NRF_FIND_PUBLIC_QUALIFICATION 1
#endif
#define INKTAG_BOARD_FIND_PUBLIC_QUALIFIED INKTAG_NRF_FIND_PUBLIC_QUALIFICATION
/* Public-owner implementation is available, but Find still requires explicit
 * experimental opt-in. Production Find qualification stays zero above. */
#define INKTAG_BOARD_FIND_EXPERIMENTAL_AVAILABLE 1

/* The qualification flag is evidence metadata, not a gate bypass. There is
 * no exact-board exception to the common 0.2 product policy. */
#define INKTAG_BOARD_POWER_WARN_MV INKTAG_POWER_POLICY_WARNING_MV
#define INKTAG_BOARD_POWER_DISPLAY_MIN_MV INKTAG_POWER_POLICY_DISPLAY_MIN_MV
#define INKTAG_BOARD_POWER_FLASH_MIN_MV INKTAG_POWER_POLICY_FLASH_MIN_MV
#define INKTAG_BOARD_POWER_OTA_MIN_MV INKTAG_POWER_POLICY_OTA_MIN_MV
#define INKTAG_BOARD_POWER_RECOVERY_HYSTERESIS_MV \
    INKTAG_POWER_POLICY_RECOVERY_HYSTERESIS_MV
#define INKTAG_BOARD_POWER_THRESHOLDS_QUALIFIED 0
#ifndef INKTAG_NRF_HEALTH_QUALIFICATION
#define INKTAG_NRF_HEALTH_QUALIFICATION 1
#endif
#define INKTAG_BOARD_HEALTH_QUALIFIED INKTAG_NRF_HEALTH_QUALIFICATION
/* Read-only health access never qualifies electrical threshold calibration. */

/* The preserved factory executable drives P0.10 low/high in software around
 * every transfer. Its original panel uses SPI mode 2 at 8 Mbps, but that is a
 * panel/controller property rather than a universal PriceETag bus setting.
 * Replacement profiles keep InkTag's qualified mode-0/4-MHz defaults (the
 * same UC8176 bus settings used by nRF52811) while retaining software CS.
 *
 * P0.07 is a board-level update-preparation output whose external function is
 * not yet proven by continuity or a schematic.  The stock update path pulses
 * it LOW for 1 ms and HIGH for 50 ms five times, then keeps it LOW throughout
 * controller init/data/refresh and returns it HIGH before releasing the panel
 * GPIOs.  Reproduce that observed sequence without mislabelling the pin as a
 * user-selectable panel EN signal. */
#define INKTAG_EPD_SPI_SOFTWARE_CS 1
#define INKTAG_EPD_BUSY_INPUT_MODE INPUT_PULLUP
#define INKTAG_EPD_NATIVE_SPI_DRIVER_ID EPD_SCREEN_DRIVER_SSD1675
#define INKTAG_EPD_NATIVE_SPI_FREQUENCY NRF_DRV_SPI_FREQ_8M
#define INKTAG_EPD_NATIVE_SPI_MODE NRF_DRV_SPI_MODE_2
#define INKTAG_BOARD_EPD_PREPARE_PIN 0x07u
#define INKTAG_BOARD_EPD_PREPARE_DELAY_MS 50u
#define INKTAG_BOARD_EPD_PREPARE_PULSE_COUNT 5u
#define INKTAG_BOARD_EPD_PREPARE_LOW_MS 1u
#define INKTAG_BOARD_EPD_PREPARE_HIGH_MS 50u

/* Keep the recovered and hardware-qualified X+/Y- RAM mode byte-for-byte.
 * Physical v108 testing established the exact mapping: an untransformed frame
 * is flipped vertically, while applying a 180-degree transform produces only
 * a horizontal mirror. The board correction is therefore a software-only row
 * reversal; columns and bit order must remain unchanged. */
#define INKTAG_BOARD_SSD1675_FLIP_Y 1

#define INKTAG_BOARD_DEFAULT_EPD_CONFIG \
    {0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}

#endif
