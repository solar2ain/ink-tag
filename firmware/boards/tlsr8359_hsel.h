#ifndef INKTAG_TLSR8359_HSEL_H
#define INKTAG_TLSR8359_HSEL_H

/* One compatible executable, existing board_id values1..10.
 * Constants preserve exact PCB facts and historical qualification.
 * Runtime Suspend/Deep policy is uniform in app.c; historical PM
 * descriptor fields below are not its enable/disable gates. */

/* hs_nebular420 exact-backend facts. */

/* Nebular uses the same TLSR application but has separate physical gates.
 * Its selector-gated upper 512 KiB image extent is exact-board qualified;
 * publisher authentication and power behavior remain unqualified. */
#define INKTAG_NEBULAR_NFC_PRESENT_QUALIFIED 1
#define INKTAG_NEBULAR_NFC_FIELD_ACTION_AVAILABLE 1
#define INKTAG_NEBULAR_NFC_FIELD_ACTION_QUALIFIED 0
/* Same fitted FM11NC08-compatible endpoint, I2C address and four signal pins
 * as Nowa. Reuse of the recovered Type-4 transcript is an explicit bench
 * candidate only; keep qualification false until phone read/write, rearm and
 * persistence pass on this exact Nebular PCB. */
#define INKTAG_NEBULAR_NFC_TYPE4_NDEF_AVAILABLE 1
#define INKTAG_NEBULAR_NFC_TYPE4_NDEF_QUALIFIED 0
#define INKTAG_NEBULAR_INDICATOR_QUALIFIED 1
#define INKTAG_NEBULAR_INDICATOR_COLORS 0x07u /* red PB4, green PD3, blue PA7 */
/* 2026-10-11: user confirms most 420R boards populate all three channels.
 * Only the first bench unit has open red/green series-resistor positions.
 * Use normal RGB scenes for this selector; an unfitted channel simply stays
 * dark. Do not substitute blue, bridge resistors or special-case device IDs. */
#define INKTAG_NEBULAR_LED_DIAGNOSTIC_AVAILABLE 1
#define INKTAG_NEBULAR_AUX_IO_QUALIFIED 0
#define INKTAG_NEBULAR_AUX_BUTTON_GPIO 255u
#define INKTAG_NEBULAR_AUX_RESERVED_0 255u
#define INKTAG_NEBULAR_AUX_RESERVED_1 0u
#define INKTAG_NEBULAR_AUX_RESERVED_2 20u
/* Shared lifecycle planning plus the two-page Settings Journal preserve the
 * protected identity/activation/Find records while clearing user state. */
#define INKTAG_NEBULAR_LIFECYCLE_QUALIFIED 1
/* Board capability markers from the legacy profile.
 * The BASIC App does not implement activation. */
#define INKTAG_NEBULAR_ACTIVATION_QUALIFIED 1
#define INKTAG_NEBULAR_FIND_QUALIFIED 0
#define INKTAG_NEBULAR_FIND_EXPERIMENTAL_AVAILABLE 1
/* The exact TH25Q-80UA board has guarded first-flash, fixed-Recovery entry,
 * normal OTA and 10/50/95% interrupted-transfer recovery evidence. */
#define INKTAG_NEBULAR_OTA_RECOVERY_QUALIFIED 1
#define INKTAG_NEBULAR_POWER_THRESHOLDS_QUALIFIED 0
#define INKTAG_NEBULAR_DEVICE_INFO_QR_QUALIFIED 1
/* Layout-v4 reserves the same two 16 KiB atomic ITFA banks used by Nowa.
 * Exact-board Full/OTA preservation plus inactive-bank install, validation,
 * selection and reset persistence were validated on this board. */
#define INKTAG_NEBULAR_GUI_ASSET_UPDATE_QUALIFIED 1
/* Candidate gate for the large-frame wire-compression pilot. Keep enabled
 * only after the exact 420R direct/store/resume matrix passes. */
#define INKTAG_NEBULAR_TRANSFER_PACKBITS_RLE_QUALIFIED 1
/* The multi-day advertising-suspend soak passed without another disconnect.
 * Match the GR/OM product floor by also allowing ordinary connection suspend,
 * while keeping both deep-retention states off until the HS9118/TH25Q-80UA
 * GPIO/peripheral restore path is qualified separately. */
#define INKTAG_NEBULAR_IDLE_SUSPEND_ENABLED 1
#define INKTAG_NEBULAR_CONNECTION_SUSPEND_ENABLED 1
#define INKTAG_NEBULAR_DEEP_RETENTION_ADV_ENABLED 0
#define INKTAG_NEBULAR_DEEP_RETENTION_CONN_ENABLED 0

/* Exact-PCB electrical routing. These tokens are consumed only by the
 * TLSR8359 board-profile adapter after the Telink GPIO definitions exist. */
#define INKTAG_NEBULAR_EPD_MOSI_GPIO GPIO_PB6
#define INKTAG_NEBULAR_EPD_SCLK_GPIO GPIO_PD7
#define INKTAG_NEBULAR_EPD_CS_GPIO GPIO_PD2
#define INKTAG_NEBULAR_EPD_DC_GPIO GPIO_PB7
#define INKTAG_NEBULAR_EPD_RESET_GPIO GPIO_PD4
#define INKTAG_NEBULAR_EPD_BUSY_GPIO GPIO_PA1
#define INKTAG_NEBULAR_EPD_ENABLE_GPIO GPIO_PB5
#define INKTAG_NEBULAR_EPD_BUSY_ACTIVE_HIGH 0
#define INKTAG_NEBULAR_EPD_HARDWARE_SPI 1
#define INKTAG_NEBULAR_EPD_RESET_HIGH_LOW_HIGH 1
#define INKTAG_NEBULAR_EPD_PARK_PULL PM_PIN_PULLDOWN_100K
#define INKTAG_NEBULAR_EPD_BUSY_PARK_FLOAT 0
#define INKTAG_NEBULAR_EPD_POWER_SETTLE_MS 0u
#define INKTAG_NEBULAR_EPD_BUS_SETTLE_MS 0u
#define INKTAG_NEBULAR_PANEL_DRIVER EPD_SCREEN_DRIVER_JD79668AA
#define INKTAG_NEBULAR_PANEL_RECIPE \
    INKTAG_RECIPE_JD79668AA_HANSHOW_NEBULAR_420R_N
#define INKTAG_NEBULAR_PANEL_COLOR EPD_SCREEN_COLOR_BWRY
#define INKTAG_NEBULAR_PANEL_TRANSFORM EPD_SCREEN_TRANSFORM_MIRROR_Y
#define INKTAG_NEBULAR_PANEL_WIDTH 400u
#define INKTAG_NEBULAR_PANEL_HEIGHT 300u
#define INKTAG_NEBULAR_PANEL_VISIBLE_X 0u
#define INKTAG_NEBULAR_PANEL_VISIBLE_Y 0u
#define INKTAG_NEBULAR_PANEL_VISIBLE_WIDTH 400u
#define INKTAG_NEBULAR_PANEL_VISIBLE_HEIGHT 300u
#define INKTAG_NEBULAR_LED_RED_GPIO GPIO_PB4
#define INKTAG_NEBULAR_LED_GREEN_GPIO GPIO_PD3
#define INKTAG_NEBULAR_LED_BLUE_GPIO GPIO_PA7
#define INKTAG_NEBULAR_SUPPLY_ADC_GPIO GPIO_PB1
#define INKTAG_NEBULAR_NFC_SDA_GPIO GPIO_PC0
#define INKTAG_NEBULAR_NFC_SCL_GPIO GPIO_PC1
#define INKTAG_NEBULAR_NFC_SELECT_GPIO GPIO_PC6
#define INKTAG_NEBULAR_NFC_STORAGE55 1
#define INKTAG_NEBULAR_NFC_USER_ADDRESS 0x0010u
#define INKTAG_NEBULAR_NFC_METADATA_ADDRESS 0x0384u
#define INKTAG_NEBULAR_NFC_IRQ_GPIO GPIO_PC4
#define INKTAG_NEBULAR_PHYSICAL_FLASH_BYTES 0x100000u
#define INKTAG_NEBULAR_HIGH_IMAGE_EXTENT 1
#define INKTAG_NEBULAR_LFCLK_SOURCE INKTAG_LFCLK_SOURCE_INTERNAL_RC
#define INKTAG_NEBULAR_LFCLK_QUALITY INKTAG_TIMEBASE_QUALITY_NON_PRECISION
#define INKTAG_NEBULAR_LFCLK_QUALIFIED 0
#define INKTAG_NEBULAR_LFCLK_FREQUENCY_HZ 32768u
#define INKTAG_NEBULAR_TIME_COUNTER_TICKS_PER_SECOND 16000000u
#define INKTAG_NEBULAR_LFCLK_COUNTER_MASK 0xFFFFFFFFu
#define INKTAG_NEBULAR_LFCLK_ACCURACY_PPM 0u
#define INKTAG_NEBULAR_UTC_RATE_DEFAULT_PPM 0

#define INKTAG_NEBULAR_POWER_WARN_MV INKTAG_POWER_POLICY_WARNING_MV
#define INKTAG_NEBULAR_POWER_CRITICAL_MV INKTAG_POWER_POLICY_CRITICAL_MV
#define INKTAG_NEBULAR_POWER_DISPLAY_MIN_MV INKTAG_POWER_POLICY_DISPLAY_MIN_MV
#define INKTAG_NEBULAR_POWER_FLASH_MIN_MV INKTAG_POWER_POLICY_FLASH_MIN_MV
#define INKTAG_NEBULAR_POWER_OTA_MIN_MV INKTAG_POWER_POLICY_OTA_MIN_MV
#define INKTAG_NEBULAR_POWER_RECOVERY_HYSTERESIS_MV \
    INKTAG_POWER_POLICY_RECOVERY_HYSTERESIS_MV

/* hs_nebular213 exact-backend facts. */

/* Nebular-213R-N unit 01 pilot. The fitted peripheral paths below are opened
 * for exact-board probes; product qualification still requires phone/LED tests. */
#define INKTAG_NEBULAR213_NFC_PRESENT_QUALIFIED 1
/* Current InkTag RF is phone-confirmed ISO14443-4/Type-4 with command actions.
 * Selected 0x55 native static-NDEF write and independent readback now pass
 * on ACEE; 000C was the wrong contact layout, not the selected storage.
 * Keep qualification=0 for current phone/Free refusal, true power-off/power-cut
 * and instrumented-current gates; MCU/FIFO reads alone cannot close them.
 * TLV contact storage must not be mistaken for proof of Type-2 RF. See
 * docs/research/hanshow-nebular-213r-n.md. */
#define INKTAG_NEBULAR213_NFC_FIELD_ACTION_AVAILABLE 1
#define INKTAG_NEBULAR213_NFC_FIELD_ACTION_QUALIFIED 0
#define INKTAG_NEBULAR213_NFC_TYPE4_NDEF_AVAILABLE 1
#define INKTAG_NEBULAR213_NFC_TYPE4_NDEF_QUALIFIED 0
/* User confirms all three channels are fitted on this PCB. Exact PB4/PD3/PA7
 * output and startup optics are the current pilot. The first blue-only 420R
 * bench unit is a population exception, not a Nebular-family default. */
#define INKTAG_NEBULAR213_INDICATOR_QUALIFIED 1
#define INKTAG_NEBULAR213_INDICATOR_COLORS 0x07u /* red PB4, green PD3, blue PA7 */
#define INKTAG_NEBULAR213_LED_DIAGNOSTIC_AVAILABLE 1
#define INKTAG_NEBULAR213_AUX_IO_QUALIFIED 0
#define INKTAG_NEBULAR213_AUX_BUTTON_GPIO 255u
#define INKTAG_NEBULAR213_AUX_RESERVED_0 255u
#define INKTAG_NEBULAR213_AUX_RESERVED_1 0u
#define INKTAG_NEBULAR213_AUX_RESERVED_2 20u
#define INKTAG_NEBULAR213_LIFECYCLE_QUALIFIED 0
#define INKTAG_NEBULAR213_ACTIVATION_QUALIFIED 1
#define INKTAG_NEBULAR213_FIND_QUALIFIED 0
#define INKTAG_NEBULAR213_FIND_EXPERIMENTAL_AVAILABLE 1
/* Fixed Recovery was installed by guarded first flash; enable the exact-board
 * OTA pilot now that the application and Recovery headers have been read back. */
#define INKTAG_NEBULAR213_OTA_RECOVERY_QUALIFIED 1
#define INKTAG_NEBULAR213_POWER_THRESHOLDS_QUALIFIED 0
/* Shared backend pilot opened at user request; the exact-board QR, atomic
 * ITFA and compressed/direct/stored transfer matrix is recorded in the port
 * README. This experimental board remains excluded from release artifacts. */
#define INKTAG_NEBULAR213_DEVICE_INFO_QR_QUALIFIED 1
#define INKTAG_NEBULAR213_GUI_ASSET_UPDATE_QUALIFIED 1
#define INKTAG_NEBULAR213_TRANSFER_PACKBITS_RLE_QUALIFIED 1
/* Ordinary Suspend pilot; keep transaction/debug wake guards in the shared
 * adapter. Deep Retention remains disabled, pending a separate experiment. */
#define INKTAG_NEBULAR213_IDLE_SUSPEND_ENABLED 1
#define INKTAG_NEBULAR213_CONNECTION_SUSPEND_ENABLED 1
#define INKTAG_NEBULAR213_DEEP_RETENTION_ADV_ENABLED 0
#define INKTAG_NEBULAR213_DEEP_RETENTION_CONN_ENABLED 0

/* Candidate Nebular-family routing. The first Nowa-like pin trial booted BLE
 * but left the stock image unchanged after a full refresh; the shared factory
 * Nebular display overlay and this PCB's layout instead point here. */
#define INKTAG_NEBULAR213_EPD_MOSI_GPIO GPIO_PB6
#define INKTAG_NEBULAR213_EPD_SCLK_GPIO GPIO_PD7
#define INKTAG_NEBULAR213_EPD_CS_GPIO GPIO_PD2
#define INKTAG_NEBULAR213_EPD_DC_GPIO GPIO_PB7
#define INKTAG_NEBULAR213_EPD_RESET_GPIO GPIO_PD4
#define INKTAG_NEBULAR213_EPD_BUSY_GPIO GPIO_PA1
#define INKTAG_NEBULAR213_EPD_ENABLE_GPIO GPIO_PB5
#define INKTAG_NEBULAR213_EPD_BUSY_ACTIVE_HIGH 0
/* Same B6/B7/D2/D7 shared-data hardware group as Nebular420: PB6 data,
 * PD7 clock, PD2 CS; PB7 stays GPIO for DC. Mode 0, 3 MHz at 24 MHz SYSCLK. */
#define INKTAG_NEBULAR213_EPD_HARDWARE_SPI 1
#define INKTAG_NEBULAR213_EPD_RESET_HIGH_LOW_HIGH 1
#define INKTAG_NEBULAR213_EPD_PARK_PULL PM_PIN_PULLDOWN_100K
/* Power pilot: BUSY_N may sink the parked pull while the UC panel is off. */
#define INKTAG_NEBULAR213_EPD_BUSY_PARK_FLOAT 0
#define INKTAG_NEBULAR213_EPD_POWER_SETTLE_MS 0u
#define INKTAG_NEBULAR213_EPD_BUS_SETTLE_MS 0u
#define INKTAG_NEBULAR213_PANEL_DRIVER EPD_SCREEN_DRIVER_JD79XXX
#define INKTAG_NEBULAR213_PANEL_RECIPE INKTAG_RECIPE_JD79XXX_HANSHOW_NEBULAR_213R_N
/* 2026-09-28: registered mountain-journey BWRY image, user-confirmed stable
 * red and yellow after full refresh on AN0213A250-122KS682. */
#define INKTAG_NEBULAR213_PANEL_COLOR EPD_SCREEN_COLOR_BWRY
#define INKTAG_NEBULAR213_PANEL_TRANSFORM 5u
/* Provisional 128x250 RAM geometry with 122x250 visible glass. */
#define INKTAG_NEBULAR213_PANEL_WIDTH 128u
#define INKTAG_NEBULAR213_PANEL_HEIGHT 250u
#define INKTAG_NEBULAR213_PANEL_VISIBLE_X 0u
#define INKTAG_NEBULAR213_PANEL_VISIBLE_Y 0u
#define INKTAG_NEBULAR213_PANEL_VISIBLE_WIDTH 122u
#define INKTAG_NEBULAR213_PANEL_VISIBLE_HEIGHT 250u

#define INKTAG_NEBULAR213_LED_RED_GPIO GPIO_PB4
#define INKTAG_NEBULAR213_LED_GREEN_GPIO GPIO_PD3
#define INKTAG_NEBULAR213_LED_BLUE_GPIO GPIO_PA7
#define INKTAG_NEBULAR213_SUPPLY_ADC_GPIO GPIO_PB1
#define INKTAG_NEBULAR213_NFC_SDA_GPIO GPIO_PC0
#define INKTAG_NEBULAR213_NFC_SCL_GPIO GPIO_PC1
#define INKTAG_NEBULAR213_NFC_SELECT_GPIO GPIO_PC6
#define INKTAG_NEBULAR213_NFC_STORAGE55 1
#define INKTAG_NEBULAR213_NFC_USER_ADDRESS 0x0010u
#define INKTAG_NEBULAR213_NFC_METADATA_ADDRESS 0x0384u
#define INKTAG_NEBULAR213_NFC_IRQ_GPIO GPIO_PC4
#define INKTAG_NEBULAR213_PHYSICAL_FLASH_BYTES 0x80000u
#define INKTAG_NEBULAR213_HIGH_IMAGE_EXTENT 0
#define INKTAG_NEBULAR213_LFCLK_SOURCE INKTAG_LFCLK_SOURCE_INTERNAL_RC
#define INKTAG_NEBULAR213_LFCLK_QUALITY INKTAG_TIMEBASE_QUALITY_NON_PRECISION
#define INKTAG_NEBULAR213_LFCLK_QUALIFIED 0
#define INKTAG_NEBULAR213_LFCLK_FREQUENCY_HZ 32768u
#define INKTAG_NEBULAR213_TIME_COUNTER_TICKS_PER_SECOND 16000000u
#define INKTAG_NEBULAR213_LFCLK_COUNTER_MASK 0xFFFFFFFFu
#define INKTAG_NEBULAR213_LFCLK_ACCURACY_PPM 0u
#define INKTAG_NEBULAR213_UTC_RATE_DEFAULT_PPM 0
#define INKTAG_NEBULAR213_POWER_WARN_MV INKTAG_POWER_POLICY_WARNING_MV
#define INKTAG_NEBULAR213_POWER_CRITICAL_MV INKTAG_POWER_POLICY_CRITICAL_MV
#define INKTAG_NEBULAR213_POWER_DISPLAY_MIN_MV INKTAG_POWER_POLICY_DISPLAY_MIN_MV
#define INKTAG_NEBULAR213_POWER_FLASH_MIN_MV INKTAG_POWER_POLICY_FLASH_MIN_MV
#define INKTAG_NEBULAR213_POWER_OTA_MIN_MV INKTAG_POWER_POLICY_OTA_MIN_MV
#define INKTAG_NEBULAR213_POWER_RECOVERY_HYSTERESIS_MV \
    INKTAG_POWER_POLICY_RECOVERY_HYSTERESIS_MV

/* hs_nowa154 exact-backend facts. */

/* Exact-PCB qualification facts. Keep these separate from the Telink SDK
 * adapter: both Hanshow products share one application, but their release
 * gates and first-flash containers remain independent. */
#define INKTAG_NOWA_NFC_PRESENT_QUALIFIED 1
#define INKTAG_NOWA_NFC_FIELD_ACTION_AVAILABLE 1
#define INKTAG_NOWA_NFC_FIELD_ACTION_QUALIFIED 0
#define INKTAG_NOWA_NFC_TYPE4_NDEF_AVAILABLE 1
#define INKTAG_NOWA_NFC_TYPE4_NDEF_QUALIFIED 0
#define INKTAG_NOWA_INDICATOR_QUALIFIED 1
#define INKTAG_NOWA_INDICATOR_COLORS 0x07u /* physical red, green and blue */
#define INKTAG_NOWA_LED_DIAGNOSTIC_AVAILABLE 1
#define INKTAG_NOWA_AUX_IO_QUALIFIED 0
#define INKTAG_NOWA_AUX_BUTTON_GPIO 255u
#define INKTAG_NOWA_AUX_RESERVED_0 255u
#define INKTAG_NOWA_AUX_RESERVED_1 0u
#define INKTAG_NOWA_AUX_RESERVED_2 20u
/* Shared lifecycle planning plus the two-page Settings Journal preserve the
 * protected identity/activation/Find records while clearing user state. */
#define INKTAG_NOWA_LIFECYCLE_QUALIFIED 1
/* The exact this board fixture has stable device identity, guarded IKAC1
 * storage in the redundant feature journal and OTA-preserved board state.
 * The BASIC App does not implement activation or Find.
 */
#define INKTAG_NOWA_ACTIVATION_QUALIFIED 1
#define INKTAG_NOWA_FIND_QUALIFIED 0
#define INKTAG_NOWA_FIND_EXPERIMENTAL_AVAILABLE 1
#define INKTAG_NOWA_OTA_RECOVERY_QUALIFIED 1
#define INKTAG_NOWA_POWER_THRESHOLDS_QUALIFIED 0
#define INKTAG_NOWA_DEVICE_INFO_QR_QUALIFIED 1
#define INKTAG_NOWA_GUI_ASSET_UPDATE_QUALIFIED 1
/* V2 PackBits is a BLE-wire optimization only. Exact Nowa qualification must
 * cover raw fallback, decoded-offset resume, persistent raw records and final
 * output before this remains enabled for release. */
#define INKTAG_NOWA_TRANSFER_PACKBITS_RLE_QUALIFIED 1
/* Keep the release baseline aligned with the other ports: event-aware
 * ordinary suspend preserves BLE/time/debug state, while deep retention stays
 * available only as a separately qualified experiment. Historical Nowa
 * reachability did not measure average current or exercise every NFC/SWS and
 * peripheral-restore path. */
#define INKTAG_NOWA_IDLE_SUSPEND_ENABLED 1
#define INKTAG_NOWA_CONNECTION_SUSPEND_ENABLED 1
#define INKTAG_NOWA_DEEP_RETENTION_ADV_ENABLED 0
#define INKTAG_NOWA_DEEP_RETENTION_CONN_ENABLED 0

/* Exact-PCB electrical routing. These tokens are consumed only by the
 * TLSR8359 board-profile adapter after the Telink GPIO definitions exist. */
#define INKTAG_NOWA_EPD_MOSI_GPIO GPIO_PB6
#define INKTAG_NOWA_EPD_SCLK_GPIO GPIO_PB5
#define INKTAG_NOWA_EPD_CS_GPIO GPIO_PB4
#define INKTAG_NOWA_EPD_DC_GPIO GPIO_PD7
#define INKTAG_NOWA_EPD_RESET_GPIO GPIO_PD4
#define INKTAG_NOWA_EPD_BUSY_GPIO GPIO_PA1
#define INKTAG_NOWA_EPD_ENABLE_GPIO GPIO_PB7
#define INKTAG_NOWA_EPD_BUSY_ACTIVE_HIGH 1
#define INKTAG_NOWA_EPD_HARDWARE_SPI 0
#define INKTAG_NOWA_EPD_RESET_HIGH_LOW_HIGH 0
#define INKTAG_NOWA_EPD_PARK_PULL PM_PIN_PULLUP_10K
#define INKTAG_NOWA_EPD_BUSY_PARK_FLOAT 0
#define INKTAG_NOWA_EPD_POWER_SETTLE_MS 20u
#define INKTAG_NOWA_EPD_BUS_SETTLE_MS 50u
#define INKTAG_NOWA_PANEL_DRIVER EPD_SCREEN_DRIVER_SSD1675
#define INKTAG_NOWA_PANEL_RECIPE INKTAG_RECIPE_SSD1675_HANSHOW_NOWA_154R_N
#define INKTAG_NOWA_PANEL_COLOR EPD_SCREEN_COLOR_BWR
#define INKTAG_NOWA_PANEL_TRANSFORM \
    (EPD_SCREEN_TRANSFORM_ROTATE_90 | EPD_SCREEN_TRANSFORM_MIRROR_X)
#define INKTAG_NOWA_PANEL_WIDTH 152u
#define INKTAG_NOWA_PANEL_HEIGHT 152u
#define INKTAG_NOWA_PANEL_VISIBLE_X 0u
#define INKTAG_NOWA_PANEL_VISIBLE_Y 0u
#define INKTAG_NOWA_PANEL_VISIBLE_WIDTH 152u
#define INKTAG_NOWA_PANEL_VISIBLE_HEIGHT 152u
#define INKTAG_NOWA_LED_RED_GPIO GPIO_PD2
#define INKTAG_NOWA_LED_GREEN_GPIO GPIO_PD3
#define INKTAG_NOWA_LED_BLUE_GPIO GPIO_PA7
#define INKTAG_NOWA_SUPPLY_ADC_GPIO GPIO_PB1
#define INKTAG_NOWA_NFC_SDA_GPIO GPIO_PC0
#define INKTAG_NOWA_NFC_SCL_GPIO GPIO_PC1
#define INKTAG_NOWA_NFC_SELECT_GPIO GPIO_PC6
#define INKTAG_NOWA_NFC_STORAGE55 0
#define INKTAG_NOWA_NFC_IRQ_GPIO GPIO_PC4
#define INKTAG_NOWA_PHYSICAL_FLASH_BYTES 0x80000u
#define INKTAG_NOWA_HIGH_IMAGE_EXTENT 0
#define INKTAG_NOWA_LFCLK_SOURCE INKTAG_LFCLK_SOURCE_INTERNAL_RC
#define INKTAG_NOWA_LFCLK_QUALITY INKTAG_TIMEBASE_QUALITY_NON_PRECISION
#define INKTAG_NOWA_LFCLK_QUALIFIED 0
#define INKTAG_NOWA_LFCLK_FREQUENCY_HZ 32768u
#define INKTAG_NOWA_TIME_COUNTER_TICKS_PER_SECOND 16000000u
#define INKTAG_NOWA_LFCLK_COUNTER_MASK 0xFFFFFFFFu
#define INKTAG_NOWA_LFCLK_ACCURACY_PPM 0u
/* No qualified voltage/temperature/idle calibration; manual RAM trial only. */
#define INKTAG_NOWA_UTC_RATE_DEFAULT_PPM 0

#define INKTAG_NOWA_POWER_WARN_MV INKTAG_POWER_POLICY_WARNING_MV
#define INKTAG_NOWA_POWER_CRITICAL_MV INKTAG_POWER_POLICY_CRITICAL_MV
#define INKTAG_NOWA_POWER_DISPLAY_MIN_MV INKTAG_POWER_POLICY_DISPLAY_MIN_MV
#define INKTAG_NOWA_POWER_FLASH_MIN_MV INKTAG_POWER_POLICY_FLASH_MIN_MV
#define INKTAG_NOWA_POWER_OTA_MIN_MV INKTAG_POWER_POLICY_OTA_MIN_MV
#define INKTAG_NOWA_POWER_RECOVERY_HYSTERESIS_MV \
    INKTAG_POWER_POLICY_RECOVERY_HYSTERESIS_MV

/* hs_nowa213 exact-backend facts. */

/* Unit-01 phone tests passed URL, NDEF read/write and text-command action.
 * Keep production Type-4 qualification false until power-cycle/retention and
 * supply/current tests; this selector remains experimental. */
#define INKTAG_NOWA213_NFC_PRESENT_QUALIFIED 1
#define INKTAG_NOWA213_NFC_FIELD_ACTION_AVAILABLE 1
#define INKTAG_NOWA213_NFC_FIELD_ACTION_QUALIFIED 0
#define INKTAG_NOWA213_NFC_TYPE4_NDEF_AVAILABLE 1
#define INKTAG_NOWA213_NFC_TYPE4_NDEF_QUALIFIED 0
/* Unit-01 direct-channel test passed red PD2, green PD3 and blue PA7.
 * PA7 is restored to SWS whenever blue is off. */
#define INKTAG_NOWA213_INDICATOR_QUALIFIED 1
#define INKTAG_NOWA213_INDICATOR_COLORS 7u
#define INKTAG_NOWA213_LED_DIAGNOSTIC_AVAILABLE 1
#define INKTAG_NOWA213_AUX_IO_QUALIFIED 0
#define INKTAG_NOWA213_AUX_BUTTON_GPIO 255u
#define INKTAG_NOWA213_AUX_RESERVED_0 255u
#define INKTAG_NOWA213_AUX_RESERVED_1 0u
#define INKTAG_NOWA213_AUX_RESERVED_2 20u
/* Controlled owner-reset pilot on unit 01 only. This enables the common
 * lifecycle command for physical qualification; the board remains excluded
 * from release until owner proof, power-cut resume, data clearing, Pro-code
 * re-install and Find re-enrolment all pass on this exact board. */
#define INKTAG_NOWA213_LIFECYCLE_QUALIFIED 1
/* Unit 01 passed exact-ID Free -> signed test Pro -> Free, then Pro-gated
 * public-only Find v2 provision/Apple report, ordinary OTA and real supply-cut
 * identity retention. Production still requires a device-bound grant signed
 * for its distinct product verification key; Find remains default-off and
 * experimental, not release-qualified. */
#define INKTAG_NOWA213_ACTIVATION_QUALIFIED 1
#define INKTAG_NOWA213_FIND_QUALIFIED 0
#define INKTAG_NOWA213_FIND_EXPERIMENTAL_AVAILABLE 1
#define INKTAG_NOWA213_OTA_RECOVERY_QUALIFIED 1
#define INKTAG_NOWA213_POWER_THRESHOLDS_QUALIFIED 0
/* Unit-01 v5 shared-CLI QR commit/reconnect, inactive-bank four-font ITFA,
 * RAW/PackBits upload/display and matching stored-frame readback passed.
 * These operational backends do not qualify the whole experimental PCB,
 * physical power cuts, browser scans or RF/current behavior. See the v5 closeout. */
#define INKTAG_NOWA213_DEVICE_INFO_QR_QUALIFIED 1
#define INKTAG_NOWA213_GUI_ASSET_UPDATE_QUALIFIED 1
#define INKTAG_NOWA213_TRANSFER_PACKBITS_RLE_QUALIFIED 1
/* Unit-01 ordinary connection Suspend: health-only 30s connection observed
 * sleepEntries 379 -> 2383, no crashes. Not an instrument/long-soak result.
 * Neither advertising nor connection Deep Retention is enabled. */
#define INKTAG_NOWA213_IDLE_SUSPEND_ENABLED 1
#define INKTAG_NOWA213_CONNECTION_SUSPEND_ENABLED 1
#define INKTAG_NOWA213_DEEP_RETENTION_ADV_ENABLED 0
#define INKTAG_NOWA213_DEEP_RETENTION_CONN_ENABLED 0

/* Candidate from this unit's UC-style stock overlay and the Nowa-154 sibling;
 * see docs/research/hanshow-nowa-213r-n.md before trusting any net. */
#define INKTAG_NOWA213_EPD_MOSI_GPIO GPIO_PB6
#define INKTAG_NOWA213_EPD_SCLK_GPIO GPIO_PB5
#define INKTAG_NOWA213_EPD_CS_GPIO GPIO_PB4
#define INKTAG_NOWA213_EPD_DC_GPIO GPIO_PD7
#define INKTAG_NOWA213_EPD_RESET_GPIO GPIO_PD4
#define INKTAG_NOWA213_EPD_BUSY_GPIO GPIO_PA1
#define INKTAG_NOWA213_EPD_ENABLE_GPIO GPIO_PB7
#define INKTAG_NOWA213_EPD_BUSY_ACTIVE_HIGH 0
#define INKTAG_NOWA213_EPD_HARDWARE_SPI 0
#define INKTAG_NOWA213_EPD_RESET_HIGH_LOW_HIGH 0
#define INKTAG_NOWA213_EPD_PARK_PULL PM_PIN_PULLUP_10K
/* Power pilot: BUSY_N may sink the parked pull while the UC panel is off. */
#define INKTAG_NOWA213_EPD_BUSY_PARK_FLOAT 1
#define INKTAG_NOWA213_EPD_POWER_SETTLE_MS 20u
#define INKTAG_NOWA213_EPD_BUS_SETTLE_MS 50u
#define INKTAG_NOWA213_PANEL_DRIVER EPD_SCREEN_DRIVER_UC8XXX
#define INKTAG_NOWA213_PANEL_RECIPE INKTAG_RECIPE_UC8XXX_HANSHOW_NOWA_213R_N
#define INKTAG_NOWA213_PANEL_COLOR EPD_SCREEN_COLOR_BWR
#define INKTAG_NOWA213_PANEL_TRANSFORM 5u
/* The glass is 250x122, but the controller scans 250 rows of 16 bytes.
 * Six columns of the 128-pixel byte pitch are outside the visible glass. */
#define INKTAG_NOWA213_PANEL_WIDTH 128u
#define INKTAG_NOWA213_PANEL_HEIGHT 250u
#define INKTAG_NOWA213_PANEL_VISIBLE_X 0u
#define INKTAG_NOWA213_PANEL_VISIBLE_Y 0u
#define INKTAG_NOWA213_PANEL_VISIBLE_WIDTH 122u
#define INKTAG_NOWA213_PANEL_VISIBLE_HEIGHT 250u

#define INKTAG_NOWA213_LED_RED_GPIO GPIO_PD2
#define INKTAG_NOWA213_LED_GREEN_GPIO GPIO_PD3
#define INKTAG_NOWA213_LED_BLUE_GPIO GPIO_PA7
#define INKTAG_NOWA213_SUPPLY_ADC_GPIO GPIO_PB1
#define INKTAG_NOWA213_NFC_SDA_GPIO GPIO_PC0
#define INKTAG_NOWA213_NFC_SCL_GPIO GPIO_PC1
#define INKTAG_NOWA213_NFC_SELECT_GPIO GPIO_PC6
#define INKTAG_NOWA213_NFC_STORAGE55 0
#define INKTAG_NOWA213_NFC_IRQ_GPIO GPIO_PC4
#define INKTAG_NOWA213_PHYSICAL_FLASH_BYTES 0x80000u
#define INKTAG_NOWA213_HIGH_IMAGE_EXTENT 0
#define INKTAG_NOWA213_LFCLK_SOURCE INKTAG_LFCLK_SOURCE_INTERNAL_RC
#define INKTAG_NOWA213_LFCLK_QUALITY INKTAG_TIMEBASE_QUALITY_NON_PRECISION
#define INKTAG_NOWA213_LFCLK_QUALIFIED 0
#define INKTAG_NOWA213_LFCLK_FREQUENCY_HZ 32768u
#define INKTAG_NOWA213_TIME_COUNTER_TICKS_PER_SECOND 16000000u
#define INKTAG_NOWA213_LFCLK_COUNTER_MASK 0xFFFFFFFFu
#define INKTAG_NOWA213_LFCLK_ACCURACY_PPM 0u
#define INKTAG_NOWA213_UTC_RATE_DEFAULT_PPM 0
#define INKTAG_NOWA213_POWER_WARN_MV INKTAG_POWER_POLICY_WARNING_MV
#define INKTAG_NOWA213_POWER_CRITICAL_MV INKTAG_POWER_POLICY_CRITICAL_MV
#define INKTAG_NOWA213_POWER_DISPLAY_MIN_MV INKTAG_POWER_POLICY_DISPLAY_MIN_MV
#define INKTAG_NOWA213_POWER_FLASH_MIN_MV INKTAG_POWER_POLICY_FLASH_MIN_MV
#define INKTAG_NOWA213_POWER_OTA_MIN_MV INKTAG_POWER_POLICY_OTA_MIN_MV
#define INKTAG_NOWA213_POWER_RECOVERY_HYSTERESIS_MV \
    INKTAG_POWER_POLICY_RECOVERY_HYSTERESIS_MV

/* hs_nebularpro154q exact-backend facts. */
/* HSEL4Q_01_54M_31, verified original SHA 5FFE668D...848B.
 * Bus flag1 -> callbacks at11000 ->33BCC(group1), then3A4F8.
 * Static routing evidence is not electrical/display qualification. */
#define INKTAG_NEBULARPRO154Q_EPD_MOSI_GPIO GPIO_PB6
#define INKTAG_NEBULARPRO154Q_EPD_SCLK_GPIO GPIO_PD7
#define INKTAG_NEBULARPRO154Q_EPD_CS_GPIO GPIO_PD2
#define INKTAG_NEBULARPRO154Q_EPD_DC_GPIO GPIO_PB7
#define INKTAG_NEBULARPRO154Q_EPD_RESET_GPIO GPIO_PD4
#define INKTAG_NEBULARPRO154Q_EPD_BUSY_GPIO GPIO_PA1
#define INKTAG_NEBULARPRO154Q_EPD_ENABLE_GPIO GPIO_PB5
#define INKTAG_NEBULARPRO154Q_PANEL_WIDTH 200u
#define INKTAG_NEBULARPRO154Q_PANEL_HEIGHT 200u
#define INKTAG_NEBULARPRO154Q_VERIFIED_FLASH_WINDOW_BYTES 524288u
#define INKTAG_NEBULARPRO154Q_PANEL_RECIPE INKTAG_RECIPE_JD79XXX_HANSHOW_HSEL4Q_01_54M_31
/* Exact original5FFE668D...:1C3E4 ->32C68 selects0101/PB0 and the
 * drive-high VDD method. Do not inherit the older420 PB1 path.
 * Selected bus1:134C6 ->2647C/24B4C maps RGB PB4/PD3/PA7, low-active.
 * NFC323DC ->33A70 group2 is PC0/PC1;23554 selects PC6;23DEE reads PC4.
 * Physical voltage/optical/NFC/current tests remain open; offline pilot only. */
#define INKTAG_NEBULARPRO154Q_SUPPLY_ADC_GPIO GPIO_PB0
#define INKTAG_NEBULARPRO154Q_LED_RED_GPIO GPIO_PB4
#define INKTAG_NEBULARPRO154Q_LED_GREEN_GPIO GPIO_PD3
#define INKTAG_NEBULARPRO154Q_LED_BLUE_GPIO GPIO_PA7
#define INKTAG_NEBULARPRO154Q_NFC_SDA_GPIO GPIO_PC0
#define INKTAG_NEBULARPRO154Q_NFC_SCL_GPIO GPIO_PC1
#define INKTAG_NEBULARPRO154Q_NFC_SELECT_GPIO GPIO_PC6
#define INKTAG_NEBULARPRO154Q_NFC_IRQ_GPIO GPIO_PC4

/* hs_hsel3_266q exact-backend facts. */
/* HSEL3_02_66M_36, verified original SHA256 2563E1CC...DD71.
 * Bus flag1 ->3A6F4 ->11000 callbacks ->31A34(group1).
 * Product name/COG marking and electrical qualification remain open. */
#define INKTAG_HSEL3266Q_EPD_MOSI_GPIO GPIO_PB6
#define INKTAG_HSEL3266Q_EPD_SCLK_GPIO GPIO_PD7
#define INKTAG_HSEL3266Q_EPD_CS_GPIO GPIO_PD2
#define INKTAG_HSEL3266Q_EPD_DC_GPIO GPIO_PB7
#define INKTAG_HSEL3266Q_EPD_RESET_GPIO GPIO_PD4
#define INKTAG_HSEL3266Q_EPD_BUSY_GPIO GPIO_PA1
#define INKTAG_HSEL3266Q_EPD_ENABLE_GPIO GPIO_PB5
#define INKTAG_HSEL3266Q_PANEL_WIDTH 152u
#define INKTAG_HSEL3266Q_PANEL_HEIGHT 296u
#define INKTAG_HSEL3266Q_PANEL_RECIPE INKTAG_RECIPE_JD79XXX_HANSHOW_HSEL3_02_66M_36
/* Exact selected stock:1BB78 ->309EC ->308D8/3086C, PB0 VDD;
 * busflag1 at843C10 chooses PB4, not the alternate PD2 red route. */
#define INKTAG_HSEL3266Q_SUPPLY_ADC_GPIO GPIO_PB0
#define INKTAG_HSEL3266Q_LED_RED_GPIO GPIO_PB4
#define INKTAG_HSEL3266Q_LED_GREEN_GPIO GPIO_PD3
#define INKTAG_HSEL3266Q_LED_BLUE_GPIO GPIO_PA7
#define INKTAG_HSEL3266Q_VERIFIED_FLASH_WINDOW_BYTES (512u * 1024u)
/* Exact-unit stable1D/10 header; stock227A6->226EC/2F4D4, IRQ2257A/2FD48. */
#define INKTAG_HSEL3266Q_NFC_SDA_GPIO GPIO_PC0
#define INKTAG_HSEL3266Q_NFC_SCL_GPIO GPIO_PC1
#define INKTAG_HSEL3266Q_NFC_SELECT_GPIO GPIO_PC6
#define INKTAG_HSEL3266Q_NFC_IRQ_GPIO GPIO_PC4
#define INKTAG_HSEL3266Q_NFC_INIT_ADDRESS 0x03B5u
#define INKTAG_HSEL3266Q_NFC_INIT_VALUE 0xA4u
#define INKTAG_HSEL3266Q_NFC_CONTROL_ADDRESS 0x03B2u
#define INKTAG_HSEL3266Q_NFC_CONTROL_VALUE 0x03u

/* hs_hsel5101_200bwr exact-backend facts. */
/* Human-confirmed HS_EL5101_5M_60_04; independent original SHA FAD626AB...
 * CCC6. Selected display module3D000 uses PB6/PB5/PB4/PD7, PA1 BUSY;
 * 3D8BC/8EC are PB7 rail/park, 3D930 pulses PD4 for200ms. Exact controller
 * die and optional devices remain unknown, not inherited Nowa qualification. */
#define INKTAG_HSEL5101200_EPD_MOSI_GPIO GPIO_PB6
#define INKTAG_HSEL5101200_EPD_SCLK_GPIO GPIO_PB5
#define INKTAG_HSEL5101200_EPD_CS_GPIO GPIO_PB4
#define INKTAG_HSEL5101200_EPD_DC_GPIO GPIO_PD7
#define INKTAG_HSEL5101200_EPD_RESET_GPIO GPIO_PD4
#define INKTAG_HSEL5101200_EPD_BUSY_GPIO GPIO_PA1
#define INKTAG_HSEL5101200_EPD_ENABLE_GPIO GPIO_PB7
#define INKTAG_HSEL5101200_PANEL_WIDTH 200u
#define INKTAG_HSEL5101200_PANEL_HEIGHT 200u
#define INKTAG_HSEL5101200_PANEL_RECIPE INKTAG_RECIPE_SSD1XXX_HANSHOW_200_BWR_OLD
/* Exact selected stock path 225F8 -> 224F0 -> 224B4 drives PB0 high
 * (active-low OEN cleared); reuse the calibrated VDD sampler, not a second
 * input-only ADC mode. Original capture FAD626AB... and corrected A3FF
 * development measurements are recorded in the P154 closeout. */
#define INKTAG_HSEL5101200_SUPPLY_ADC_GPIO GPIO_PB0
#define INKTAG_HSEL5101200_LED_RED_GPIO GPIO_PD2
#define INKTAG_HSEL5101200_LED_GREEN_GPIO GPIO_PD3
#define INKTAG_HSEL5101200_LED_BLUE_GPIO GPIO_PA7 /* release SWS after diagnostic */
#define INKTAG_HSEL5101200_NFC_SDA_GPIO GPIO_PC0
#define INKTAG_HSEL5101200_NFC_SCL_GPIO GPIO_PC1
#define INKTAG_HSEL5101200_NFC_SELECT_GPIO GPIO_PC6
#define INKTAG_HSEL5101200_NFC_IRQ_GPIO GPIO_PC4
/* Qualified lower address window, not a claim about an unknown upper die. */
#define INKTAG_HSEL5101200_VERIFIED_FLASH_WINDOW_BYTES (512u * 1024u)

/* Exact HSEL5_01_54M_02, stock SHA657390FC...F6168. Independent selector10:
 *3D9DC waits while PA1 is high;3D32C/35C rail;3D3A0 H/L/H reset.
 * Init3DA34 uses native OTP, not P154 recipe1 or _00's JD recipe. */
#define INKTAG_HSEL5154M02_EPD_MOSI_GPIO GPIO_PB6
#define INKTAG_HSEL5154M02_EPD_SCLK_GPIO GPIO_PB5
#define INKTAG_HSEL5154M02_EPD_CS_GPIO GPIO_PB4
#define INKTAG_HSEL5154M02_EPD_DC_GPIO GPIO_PD7
#define INKTAG_HSEL5154M02_EPD_RESET_GPIO GPIO_PD4
#define INKTAG_HSEL5154M02_EPD_BUSY_GPIO GPIO_PA1
#define INKTAG_HSEL5154M02_EPD_ENABLE_GPIO GPIO_PB7
#define INKTAG_HSEL5154M02_SUPPLY_ADC_GPIO GPIO_PB0
#define INKTAG_HSEL5154M02_LED_RED_GPIO GPIO_PD2
#define INKTAG_HSEL5154M02_LED_GREEN_GPIO GPIO_PD3
#define INKTAG_HSEL5154M02_LED_BLUE_GPIO GPIO_PA7
#define INKTAG_HSEL5154M02_NFC_SDA_GPIO GPIO_PC0
#define INKTAG_HSEL5154M02_NFC_SCL_GPIO GPIO_PC1
#define INKTAG_HSEL5154M02_NFC_SELECT_GPIO GPIO_PC6
#define INKTAG_HSEL5154M02_NFC_IRQ_GPIO GPIO_PC4
#define INKTAG_HSEL5154M02_NFC_INIT_ADDRESS 0x03B5u
#define INKTAG_HSEL5154M02_NFC_INIT_VALUE 0xA4u
#define INKTAG_HSEL5154M02_NFC_CONTROL_ADDRESS 0x03B2u
#define INKTAG_HSEL5154M02_NFC_CONTROL_VALUE 0x03u
#define INKTAG_HSEL5154M02_VERIFIED_FLASH_WINDOW_BYTES (512u * 1024u)

/* hs_stellar154 exact-backend facts. */
/* Exact HSEL5_01_54M_00 original SHA37AD2F79...BEFFB3, module3D000.
 * Software bus3D870/990/9C4; BUSY3D9F8; rail3D32C/35C; reset3D3A0.
 * Removable FPC-5301 / DEPG0154RWF653CF. Optional paths not inherited. */
#define INKTAG_STELLAR154_EPD_MOSI_GPIO GPIO_PB6
#define INKTAG_STELLAR154_EPD_SCLK_GPIO GPIO_PB5
#define INKTAG_STELLAR154_EPD_CS_GPIO GPIO_PB4
#define INKTAG_STELLAR154_EPD_DC_GPIO GPIO_PD7
#define INKTAG_STELLAR154_EPD_RESET_GPIO GPIO_PD4
#define INKTAG_STELLAR154_EPD_BUSY_GPIO GPIO_PA1
#define INKTAG_STELLAR154_EPD_ENABLE_GPIO GPIO_PB7
/* Original2081C ->190C4 ->24A04 ->248FC selects0101/PB0, not PB1.
 * 43D2 donor-PB0 App sampled3264..3265mV with valid normal power state. */
#define INKTAG_STELLAR154_SUPPLY_ADC_GPIO GPIO_PB0
#define INKTAG_STELLAR154_VERIFIED_FLASH_WINDOW_BYTES 524288u
#define INKTAG_STELLAR154_LED_RED_GPIO GPIO_PD2
#define INKTAG_STELLAR154_LED_GREEN_GPIO GPIO_PD3
#define INKTAG_STELLAR154_LED_BLUE_GPIO GPIO_PA7
#define INKTAG_STELLAR154_NFC_SDA_GPIO GPIO_PC0
#define INKTAG_STELLAR154_NFC_SCL_GPIO GPIO_PC1
#define INKTAG_STELLAR154_NFC_SELECT_GPIO GPIO_PC6
#define INKTAG_STELLAR154_NFC_IRQ_GPIO GPIO_PC4

/* hs_stellarpro154q exact-backend facts. */
/* Exact stock B30853E3...90B3F38, selected bus1/profile8. See HARDWARE.md.
 * Owner-authorized profile8 pilot permits bounded configuration routes.
 * PB0 is the stock drive-high VDD ADC path, not inherited sibling PB1. */
#define INKTAG_STELLARPRO154Q_SUPPLY_ADC_GPIO GPIO_PB0
#define INKTAG_STELLARPRO154Q_PANEL_AVAILABLE 1
#define INKTAG_STELLARPRO154Q_LED_RED_GPIO GPIO_PB4
#define INKTAG_STELLARPRO154Q_LED_GREEN_GPIO GPIO_PD3
#define INKTAG_STELLARPRO154Q_LED_BLUE_GPIO GPIO_PA7
#define INKTAG_STELLARPRO154Q_NFC_SDA_GPIO GPIO_PC0
#define INKTAG_STELLARPRO154Q_NFC_SCL_GPIO GPIO_PC1
#define INKTAG_STELLARPRO154Q_NFC_SELECT_GPIO GPIO_PC6
#define INKTAG_STELLARPRO154Q_NFC_IRQ_GPIO GPIO_PC4

#endif /* INKTAG_TLSR8359_HSEL_H */

#if defined(INKTAG_TLSR8359_HSEL_DEFINE_APP) && !defined(INKTAG_TLSR8359_HSEL_APP_DEFINED)
#define INKTAG_TLSR8359_HSEL_APP_DEFINED
#define BOARD_PROFILE(prefix) { \
    .epd_mosi = prefix##_EPD_MOSI_GPIO, \
    .epd_clk = prefix##_EPD_SCLK_GPIO, \
    .epd_cs = prefix##_EPD_CS_GPIO, \
    .epd_dc = prefix##_EPD_DC_GPIO, \
    .epd_reset = prefix##_EPD_RESET_GPIO, \
    .epd_busy = prefix##_EPD_BUSY_GPIO, \
    .epd_enable = prefix##_EPD_ENABLE_GPIO, \
    .led_red = prefix##_LED_RED_GPIO, \
    .led_green = prefix##_LED_GREEN_GPIO, \
    .led_blue = prefix##_LED_BLUE_GPIO, \
    .supply_adc = prefix##_SUPPLY_ADC_GPIO, \
    .physical_flash_bytes = prefix##_PHYSICAL_FLASH_BYTES, \
    .feature_flags = \
        ((prefix##_NFC_PRESENT_QUALIFIED ? \
              INKTAG_TLSR_BOARD_NFC_PRESENT_QUALIFIED : 0u) | \
         (prefix##_NFC_FIELD_ACTION_AVAILABLE ? \
              INKTAG_TLSR_BOARD_NFC_FIELD_ACTION_AVAILABLE : 0u) | \
         (prefix##_NFC_FIELD_ACTION_QUALIFIED ? \
              INKTAG_TLSR_BOARD_NFC_FIELD_ACTION_QUALIFIED : 0u) | \
         (prefix##_NFC_TYPE4_NDEF_AVAILABLE ? \
              INKTAG_TLSR_BOARD_NFC_TYPE4_AVAILABLE : 0u) | \
         (prefix##_NFC_TYPE4_NDEF_QUALIFIED ? \
              INKTAG_TLSR_BOARD_NFC_TYPE4_QUALIFIED : 0u) | \
         (prefix##_INDICATOR_QUALIFIED ? \
              INKTAG_TLSR_BOARD_INDICATOR_QUALIFIED : 0u) | \
         (prefix##_LED_DIAGNOSTIC_AVAILABLE ? \
              INKTAG_TLSR_BOARD_LED_DIAGNOSTIC_AVAILABLE : 0u) | \
         (prefix##_AUX_IO_QUALIFIED ? \
              INKTAG_TLSR_BOARD_AUX_IO_QUALIFIED : 0u) | \
         (prefix##_LIFECYCLE_QUALIFIED ? \
              INKTAG_TLSR_BOARD_LIFECYCLE_QUALIFIED : 0u) | \
         (prefix##_ACTIVATION_QUALIFIED ? \
              INKTAG_TLSR_BOARD_ACTIVATION_QUALIFIED : 0u) | \
         (prefix##_FIND_QUALIFIED ? \
              INKTAG_TLSR_BOARD_FIND_QUALIFIED : 0u) | \
         (prefix##_FIND_EXPERIMENTAL_AVAILABLE ? \
              INKTAG_TLSR_BOARD_FIND_EXPERIMENT_AVAILABLE : 0u) | \
         (prefix##_OTA_RECOVERY_QUALIFIED ? \
              INKTAG_TLSR_BOARD_OTA_RECOVERY_QUALIFIED : 0u) | \
         (prefix##_POWER_THRESHOLDS_QUALIFIED ? \
              INKTAG_TLSR_BOARD_POWER_THRESHOLDS_QUALIFIED : 0u) | \
         (prefix##_GUI_ASSET_UPDATE_QUALIFIED ? \
              INKTAG_TLSR_BOARD_GUI_ASSET_UPDATE_QUALIFIED : 0u) | \
         (prefix##_TRANSFER_PACKBITS_RLE_QUALIFIED ? \
              INKTAG_TLSR_BOARD_PACKBITS_RLE_QUALIFIED : 0u) | \
         (prefix##_NFC_STORAGE55 ? INKTAG_TLSR_BOARD_NFC_STORAGE55 : 0u)), \
    .aux_button_gpio = prefix##_AUX_BUTTON_GPIO, \
    .aux_reserved_0 = prefix##_AUX_RESERVED_0, \
    .lfclk_source_frequency_hz = prefix##_LFCLK_FREQUENCY_HZ, \
    .time_counter_ticks_per_second = \
        prefix##_TIME_COUNTER_TICKS_PER_SECOND, \
    .time_counter_mask = prefix##_LFCLK_COUNTER_MASK, \
    .lfclk_accuracy_ppm = prefix##_LFCLK_ACCURACY_PPM, \
    .epd_park_pull = prefix##_EPD_PARK_PULL, \
    .epd_power_settle_ms = prefix##_EPD_POWER_SETTLE_MS, \
    .epd_bus_settle_ms = prefix##_EPD_BUS_SETTLE_MS, \
    .indicator_colors = prefix##_INDICATOR_COLORS, \
    .aux_reserved_1 = prefix##_AUX_RESERVED_1, \
    .aux_reserved_2 = prefix##_AUX_RESERVED_2, \
    .lfclk_source = prefix##_LFCLK_SOURCE, \
    .lfclk_quality = prefix##_LFCLK_QUALITY, \
    .epd_busy_park_float = prefix##_EPD_BUSY_PARK_FLOAT != 0, \
    .epd_hardware_spi = prefix##_EPD_HARDWARE_SPI != 0, \
    .epd_reset_high_low_high = prefix##_EPD_RESET_HIGH_LOW_HIGH != 0, \
    .high_image_extent = prefix##_HIGH_IMAGE_EXTENT != 0, \
    .lfclk_qualified = prefix##_LFCLK_QUALIFIED != 0, \
}

static const inktag_tlsr_board_profile_t s_nowa =
    BOARD_PROFILE(INKTAG_NOWA);
static const inktag_tlsr_board_profile_t s_nowa213 =
    BOARD_PROFILE(INKTAG_NOWA213);
static const inktag_tlsr_board_profile_t s_nebular213 =
    BOARD_PROFILE(INKTAG_NEBULAR213);
static const inktag_tlsr_board_profile_t s_nebular =
    BOARD_PROFILE(INKTAG_NEBULAR);

/* Exact routing comes from each board-owned header. Unknown optional paths
 * are zero/unavailable; no capability qualification is inherited from420. */
#define SMALL_Q_PROFILE(prefix, features, flash_window, park_pull, busy_float) { \
    .epd_mosi = prefix##_EPD_MOSI_GPIO, .epd_clk = prefix##_EPD_SCLK_GPIO, \
    .epd_cs = prefix##_EPD_CS_GPIO, .epd_dc = prefix##_EPD_DC_GPIO, \
    .epd_reset = prefix##_EPD_RESET_GPIO, .epd_busy = prefix##_EPD_BUSY_GPIO, \
    .epd_enable = prefix##_EPD_ENABLE_GPIO, \
    .led_red = prefix##_LED_RED_GPIO, .led_green = prefix##_LED_GREEN_GPIO, \
    .led_blue = prefix##_LED_BLUE_GPIO, .supply_adc = prefix##_SUPPLY_ADC_GPIO, \
    .indicator_colors = (features) ? 7u : 0u, .feature_flags = (features), \
    .physical_flash_bytes = (flash_window), \
    .aux_button_gpio = 255u, .aux_reserved_0 = 255u, \
    .time_counter_ticks_per_second = 16000000u, .time_counter_mask = UINT32_MAX, \
    .lfclk_quality = INKTAG_TIMEBASE_QUALITY_NON_PRECISION, \
    .epd_park_pull = (park_pull), .epd_busy_park_float = (busy_float), \
    .epd_hardware_spi = true, \
    .epd_reset_high_low_high = true, \
}
static const inktag_tlsr_board_profile_t s_nebularpro154q =
    SMALL_Q_PROFILE(INKTAG_NEBULARPRO154Q,
        INKTAG_TLSR_BOARD_LED_DIAGNOSTIC_AVAILABLE |
        INKTAG_TLSR_BOARD_INDICATOR_QUALIFIED |
        INKTAG_TLSR_BOARD_NFC_PRESENT_QUALIFIED |
        INKTAG_TLSR_BOARD_NFC_FIELD_ACTION_AVAILABLE |
        INKTAG_TLSR_BOARD_NFC_TYPE4_AVAILABLE |
        INKTAG_TLSR_BOARD_NFC_DETECT_CONTACT_FORMAT |
        INKTAG_TLSR_BOARD_LIFECYCLE_QUALIFIED |
        INKTAG_TLSR_BOARD_ACTIVATION_QUALIFIED |
        INKTAG_TLSR_BOARD_FIND_EXPERIMENT_AVAILABLE |
        INKTAG_TLSR_BOARD_OTA_RECOVERY_QUALIFIED |
        INKTAG_TLSR_BOARD_GUI_ASSET_UPDATE_QUALIFIED |
        INKTAG_TLSR_BOARD_PACKBITS_RLE_QUALIFIED,
        INKTAG_NEBULARPRO154Q_VERIFIED_FLASH_WINDOW_BYTES,
        PM_PIN_PULLDOWN_100K, false);
static const inktag_tlsr_board_profile_t s_hsel3266q =
    SMALL_Q_PROFILE(INKTAG_HSEL3266Q,
        INKTAG_TLSR_BOARD_LED_DIAGNOSTIC_AVAILABLE |
        INKTAG_TLSR_BOARD_INDICATOR_QUALIFIED |
        /* Exact 1D/10 contact presence; not phone/RF or Type4 qualification. */
        INKTAG_TLSR_BOARD_NFC_PRESENT_QUALIFIED |
        INKTAG_TLSR_BOARD_NFC_FIELD_ACTION_AVAILABLE |
        INKTAG_TLSR_BOARD_NFC_TYPE4_AVAILABLE |
        INKTAG_TLSR_BOARD_NFC_DETECT_CONTACT_FORMAT |
        INKTAG_TLSR_BOARD_NFC_NATIVE55_INIT |
        INKTAG_TLSR_BOARD_LIFECYCLE_QUALIFIED |
        INKTAG_TLSR_BOARD_ACTIVATION_QUALIFIED |
        INKTAG_TLSR_BOARD_FIND_EXPERIMENT_AVAILABLE |
        INKTAG_TLSR_BOARD_OTA_RECOVERY_QUALIFIED |
        INKTAG_TLSR_BOARD_GUI_ASSET_UPDATE_QUALIFIED |
        INKTAG_TLSR_BOARD_PACKBITS_RLE_QUALIFIED,
        /* Restore the human-accepted passive parking from583779a42:
         *4-6uA after disconnect, not a2-3uA/cold-start qualification.
         * This is electrical data; rail/NFC/active drive are unchanged. */
        INKTAG_HSEL3266Q_VERIFIED_FLASH_WINDOW_BYTES,
        PM_PIN_UP_DOWN_FLOAT, true);
static const inktag_tlsr_board_profile_t s_hsel5101200 = {
    .epd_mosi = INKTAG_HSEL5101200_EPD_MOSI_GPIO,
    .epd_clk = INKTAG_HSEL5101200_EPD_SCLK_GPIO,
    .epd_cs = INKTAG_HSEL5101200_EPD_CS_GPIO,
    .epd_dc = INKTAG_HSEL5101200_EPD_DC_GPIO,
    .epd_reset = INKTAG_HSEL5101200_EPD_RESET_GPIO,
    .epd_busy = INKTAG_HSEL5101200_EPD_BUSY_GPIO,
    .epd_enable = INKTAG_HSEL5101200_EPD_ENABLE_GPIO,
    .supply_adc = INKTAG_HSEL5101200_SUPPLY_ADC_GPIO,
    .led_red = INKTAG_HSEL5101200_LED_RED_GPIO,
    .led_green = INKTAG_HSEL5101200_LED_GREEN_GPIO,
    .led_blue = INKTAG_HSEL5101200_LED_BLUE_GPIO,
    .indicator_colors = 7u,
    /* Existing shared backends, not alternate board-specific product logic.
     * RF/optical/release qualification remains separate from availability. */
    .feature_flags = INKTAG_TLSR_BOARD_LED_DIAGNOSTIC_AVAILABLE |
        INKTAG_TLSR_BOARD_INDICATOR_QUALIFIED |
        INKTAG_TLSR_BOARD_NFC_PRESENT_QUALIFIED |
        INKTAG_TLSR_BOARD_NFC_FIELD_ACTION_AVAILABLE |
        INKTAG_TLSR_BOARD_NFC_TYPE4_AVAILABLE |
        INKTAG_TLSR_BOARD_NFC_DETECT_CONTACT_FORMAT |
        INKTAG_TLSR_BOARD_LIFECYCLE_QUALIFIED |
        INKTAG_TLSR_BOARD_ACTIVATION_QUALIFIED |
        INKTAG_TLSR_BOARD_FIND_EXPERIMENT_AVAILABLE |
        INKTAG_TLSR_BOARD_OTA_RECOVERY_QUALIFIED |
        INKTAG_TLSR_BOARD_GUI_ASSET_UPDATE_QUALIFIED |
        INKTAG_TLSR_BOARD_PACKBITS_RLE_QUALIFIED,
    .physical_flash_bytes = INKTAG_HSEL5101200_VERIFIED_FLASH_WINDOW_BYTES,
    .aux_button_gpio = 255u, .aux_reserved_0 = 255u,
    .time_counter_ticks_per_second = 16000000u, .time_counter_mask = UINT32_MAX,
    .lfclk_quality = INKTAG_TIMEBASE_QUALITY_NON_PRECISION,
    .epd_park_pull = PM_PIN_PULLUP_10K,
    .epd_power_settle_ms = 200u,
};

static const inktag_tlsr_board_profile_t s_hsel5154m02 = {
    .epd_mosi = INKTAG_HSEL5154M02_EPD_MOSI_GPIO,
    .epd_clk = INKTAG_HSEL5154M02_EPD_SCLK_GPIO,
    .epd_cs = INKTAG_HSEL5154M02_EPD_CS_GPIO,
    .epd_dc = INKTAG_HSEL5154M02_EPD_DC_GPIO,
    .epd_reset = INKTAG_HSEL5154M02_EPD_RESET_GPIO,
    .epd_busy = INKTAG_HSEL5154M02_EPD_BUSY_GPIO,
    .epd_enable = INKTAG_HSEL5154M02_EPD_ENABLE_GPIO,
    .supply_adc = INKTAG_HSEL5154M02_SUPPLY_ADC_GPIO,
    .led_red = INKTAG_HSEL5154M02_LED_RED_GPIO,
    .led_green = INKTAG_HSEL5154M02_LED_GREEN_GPIO,
    .led_blue = INKTAG_HSEL5154M02_LED_BLUE_GPIO,
    .indicator_colors = 7u,
    /* Existing shared backends, not alternate board-specific product logic.
     * RF/optical/release qualification remains separate from availability. */
    .feature_flags = INKTAG_TLSR_BOARD_LED_DIAGNOSTIC_AVAILABLE |
        INKTAG_TLSR_BOARD_INDICATOR_QUALIFIED |
        INKTAG_TLSR_BOARD_NFC_PRESENT_QUALIFIED |
        INKTAG_TLSR_BOARD_NFC_FIELD_ACTION_AVAILABLE |
        INKTAG_TLSR_BOARD_NFC_TYPE4_AVAILABLE |
        INKTAG_TLSR_BOARD_NFC_DETECT_CONTACT_FORMAT |
        INKTAG_TLSR_BOARD_NFC_NATIVE_BASE_INIT |
        INKTAG_TLSR_BOARD_LIFECYCLE_QUALIFIED |
        INKTAG_TLSR_BOARD_ACTIVATION_QUALIFIED |
        INKTAG_TLSR_BOARD_FIND_EXPERIMENT_AVAILABLE |
        INKTAG_TLSR_BOARD_OTA_RECOVERY_QUALIFIED |
        INKTAG_TLSR_BOARD_GUI_ASSET_UPDATE_QUALIFIED |
        INKTAG_TLSR_BOARD_PACKBITS_RLE_QUALIFIED,
    .physical_flash_bytes = INKTAG_HSEL5154M02_VERIFIED_FLASH_WINDOW_BYTES,
    .aux_button_gpio = 255u, .aux_reserved_0 = 255u,
    .time_counter_ticks_per_second = 16000000u, .time_counter_mask = UINT32_MAX,
    .lfclk_quality = INKTAG_TIMEBASE_QUALITY_NON_PRECISION,
    .epd_park_pull = PM_PIN_PULLUP_10K,
    .epd_power_settle_ms = 200u,
    .epd_reset_high_low_high = true,
};

static const inktag_tlsr_board_profile_t s_stellar154 = {
    .epd_mosi = INKTAG_STELLAR154_EPD_MOSI_GPIO,
    .epd_clk = INKTAG_STELLAR154_EPD_SCLK_GPIO,
    .epd_cs = INKTAG_STELLAR154_EPD_CS_GPIO,
    .epd_dc = INKTAG_STELLAR154_EPD_DC_GPIO,
    .epd_reset = INKTAG_STELLAR154_EPD_RESET_GPIO,
    .epd_busy = INKTAG_STELLAR154_EPD_BUSY_GPIO,
    .epd_enable = INKTAG_STELLAR154_EPD_ENABLE_GPIO,
    .led_red = INKTAG_STELLAR154_LED_RED_GPIO,
    .led_green = INKTAG_STELLAR154_LED_GREEN_GPIO,
    .led_blue = INKTAG_STELLAR154_LED_BLUE_GPIO,
    .indicator_colors = 7u,
    .feature_flags = INKTAG_TLSR_BOARD_LED_DIAGNOSTIC_AVAILABLE |
        INKTAG_TLSR_BOARD_INDICATOR_QUALIFIED |
        INKTAG_TLSR_BOARD_NFC_PRESENT_QUALIFIED |
        INKTAG_TLSR_BOARD_NFC_FIELD_ACTION_AVAILABLE |
        INKTAG_TLSR_BOARD_NFC_TYPE4_AVAILABLE |
        INKTAG_TLSR_BOARD_NFC_DETECT_CONTACT_FORMAT |
        INKTAG_TLSR_BOARD_LIFECYCLE_QUALIFIED |
        INKTAG_TLSR_BOARD_ACTIVATION_QUALIFIED |
        INKTAG_TLSR_BOARD_FIND_EXPERIMENT_AVAILABLE |
        INKTAG_TLSR_BOARD_OTA_RECOVERY_QUALIFIED |
        INKTAG_TLSR_BOARD_GUI_ASSET_UPDATE_QUALIFIED |
        INKTAG_TLSR_BOARD_PACKBITS_RLE_QUALIFIED,
    .supply_adc = INKTAG_STELLAR154_SUPPLY_ADC_GPIO,
    .physical_flash_bytes = INKTAG_STELLAR154_VERIFIED_FLASH_WINDOW_BYTES,
    .aux_button_gpio = 255u, .aux_reserved_0 = 255u,
    .time_counter_ticks_per_second = 16000000u, .time_counter_mask = UINT32_MAX,
    .lfclk_quality = INKTAG_TIMEBASE_QUALITY_NON_PRECISION,
    /* Original3D528 releases the six signals with pull mode3/10k-up;
     *3D35C releases PB7 with the same pull. Keep active drive separate. */
    .epd_park_pull = PM_PIN_PULLUP_10K, .epd_power_settle_ms = 200u,
    .epd_reset_high_low_high = true,
};
static const inktag_tlsr_board_profile_t s_stellarpro154q = {
    .epd_hardware_spi = true,
    .epd_reset_high_low_high = true,
    /* Development-only idle-current A/B: follow stock profile8 callback12
     * signal release instead of the previous all-down callback14 candidate.
     * Final stock reach and no-fixture current remain qualification gates. */
    .epd_park_pull = INKTAG_TLSR_EPD_PARK_RESET_CS_UP_FLOAT_OTHER,
    .led_red = INKTAG_STELLARPRO154Q_LED_RED_GPIO,
    .led_green = INKTAG_STELLARPRO154Q_LED_GREEN_GPIO,
    .led_blue = INKTAG_STELLARPRO154Q_LED_BLUE_GPIO,
    .indicator_colors = 7u,
    .feature_flags = INKTAG_TLSR_BOARD_LED_DIAGNOSTIC_AVAILABLE |
        INKTAG_TLSR_BOARD_INDICATOR_QUALIFIED |
        INKTAG_TLSR_BOARD_NFC_PRESENT_QUALIFIED |
        INKTAG_TLSR_BOARD_NFC_FIELD_ACTION_AVAILABLE |
        INKTAG_TLSR_BOARD_NFC_TYPE4_AVAILABLE |
        INKTAG_TLSR_BOARD_NFC_DETECT_CONTACT_FORMAT |
        INKTAG_TLSR_BOARD_LIFECYCLE_QUALIFIED |
        INKTAG_TLSR_BOARD_ACTIVATION_QUALIFIED |
        INKTAG_TLSR_BOARD_FIND_EXPERIMENT_AVAILABLE |
        INKTAG_TLSR_BOARD_OTA_RECOVERY_QUALIFIED |
        INKTAG_TLSR_BOARD_GUI_ASSET_UPDATE_QUALIFIED |
        INKTAG_TLSR_BOARD_PACKBITS_RLE_QUALIFIED,
    .supply_adc = INKTAG_STELLARPRO154Q_SUPPLY_ADC_GPIO,
    .physical_flash_bytes = 524288u,
    .aux_button_gpio = 255u, .aux_reserved_0 = 255u,
    .time_counter_ticks_per_second = 16000000u, .time_counter_mask = UINT32_MAX,
    .lfclk_quality = INKTAG_TIMEBASE_QUALITY_NON_PRECISION,
};

#endif /* APP one-TU descriptors */

#if defined(INKTAG_TLSR8359_HSEL_DEFINE_RECOVERY) && !defined(INKTAG_TLSR8359_HSEL_RECOVERY_DEFINED)
#define INKTAG_TLSR8359_HSEL_RECOVERY_DEFINED
#define RECOVERY_PROFILE(prefix) { prefix##_LED_RED_GPIO, prefix##_LED_GREEN_GPIO, \
    prefix##_SUPPLY_ADC_GPIO, prefix##_INDICATOR_COLORS }
static const inktag_tlsr_recovery_profile_t r_nowa = RECOVERY_PROFILE(INKTAG_NOWA);
static const inktag_tlsr_recovery_profile_t r_nowa213 = RECOVERY_PROFILE(INKTAG_NOWA213);
static const inktag_tlsr_recovery_profile_t r_nebular = RECOVERY_PROFILE(INKTAG_NEBULAR);
static const inktag_tlsr_recovery_profile_t r_nebular213 = RECOVERY_PROFILE(INKTAG_NEBULAR213);
/* Independently recovered PB0 VDD route on selectors5..9; all LED routing
 * still comes from the small persistent record, not a full board descriptor. */
#if INKTAG_NEBULARPRO154Q_SUPPLY_ADC_GPIO != INKTAG_STELLARPRO154Q_SUPPLY_ADC_GPIO || \
    INKTAG_HSEL3266Q_SUPPLY_ADC_GPIO != INKTAG_STELLARPRO154Q_SUPPLY_ADC_GPIO || \
    INKTAG_HSEL5101200_SUPPLY_ADC_GPIO != INKTAG_STELLARPRO154Q_SUPPLY_ADC_GPIO || \
    INKTAG_HSEL5154M02_SUPPLY_ADC_GPIO != INKTAG_STELLARPRO154Q_SUPPLY_ADC_GPIO || \
    INKTAG_STELLAR154_SUPPLY_ADC_GPIO != INKTAG_STELLARPRO154Q_SUPPLY_ADC_GPIO
#error "PB0 Recovery view requires independently matching supply routes"
#endif
static const inktag_tlsr_recovery_profile_t r_pb0 =
    {0,0,INKTAG_STELLARPRO154Q_SUPPLY_ADC_GPIO,0};
#undef RECOVERY_PROFILE

#endif /* RECOVERY one-TU descriptors */
