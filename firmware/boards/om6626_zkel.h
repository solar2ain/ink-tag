/* Existing compatible firmware family: organization only.
 * Stable SDK filenames receive exactly one named section through the maintained
 * materializer. Export guards prevent duplicate macros/tables when inspected as
 * a C header. Existing exact-build views remain compatibility descriptions, not
 * additional App/OTA families. No static descriptor is instantiated here. */
#ifndef INKTAG_OM6626_ZKEL_H
#define INKTAG_OM6626_ZKEL_H

#if defined(INKTAG_OM_EXPORT_APP)
/* OM SECTION app BEGIN */
#ifndef INKTAG_BOARD_H
#define INKTAG_BOARD_H

#if !defined(INKTAG_ONMICRO_OM6626)
#error "om6626_zkcxxb requires the OnMicro OM6626 port"
#endif

/* ZKC21V, ZKC21B and ZKC42B use one application. The exact product identity and
 * shipped screen recipe are factory-provisioned and then persisted in NVDS;
 * neither is inferred from the shared GPIO wiring below. */
#define INKTAG_BOARD_ID "om6626_zkcxxb"
#define INKTAG_BOARD_ARTIFACT "InkTag-om6626_zkcxxb"
#define INKTAG_BOARD_EPD_SPI_INDEX 1

/* These product-protocol gates are qualified on both exact boards that share
 * this application. Find is intentionally opt-in/experimental until its
 * advertising and crowd-location regression has been repeated on OM6626. */
#define INKTAG_BOARD_ACTIVATION_QUALIFIED 1
#define INKTAG_BOARD_LIFECYCLE_QUALIFIED 1
/* Aggregate clear and factory reset stay gated separately from Find. */
#define INKTAG_BOARD_CLEAR_USER_DATA_QUALIFIED 0
#define INKTAG_BOARD_FACTORY_RESET_QUALIFIED 0
/* The shared ZKC21B/ZKC21V/ZKC42B App uses one authenticated Find store. */
#define INKTAG_BOARD_FIND_OWNER_REPLACEMENT 1
#define INKTAG_BOARD_AUX_IO_QUALIFIED 1
#define INKTAG_BOARD_NFC_VARIANT_QUALIFIED 1
#define INKTAG_BOARD_FIND_QUALIFIED 0
#define INKTAG_BOARD_FIND_EXPERIMENTAL_AVAILABLE 1
/* The adapter and guarded factory fixture exist, but the release capability
 * stays closed until both exact OM boards pass install/corrupt/cancel/reset/
 * OTA-retention/render qualification. */
#define INKTAG_BOARD_GUI_ASSET_UPDATE_QUALIFIED 0
/* These gates are deliberately per persisted exact-board identity even
 * though both products execute the same application image.  A board starts
 * advertising the wire codec only after direct, stored and read-back tests
 * have passed on that exact PCB/panel combination. */
#define INKTAG_BOARD_ZKC21B_TRANSFER_PACKBITS_QUALIFIED 1
#define INKTAG_BOARD_ZKC42B_TRANSFER_PACKBITS_QUALIFIED 0
#define INKTAG_BOARD_ZKC21V_TRANSFER_PACKBITS_QUALIFIED 0

/* Both inspected exact PCBs leave the low-frequency crystal footprint empty.
 * The shared application therefore uses OM's always-running 32.768 kHz PMU
 * counter sourced from the internal RC oscillator. It is non-precision until
 * an exact-board temperature/drift soak supplies a bounded accuracy claim. */
#define INKTAG_BOARD_LFCLK_SOURCE INKTAG_LFCLK_SOURCE_INTERNAL_RC
#define INKTAG_BOARD_LFCLK_QUALITY INKTAG_TIMEBASE_QUALITY_NON_PRECISION
#define INKTAG_BOARD_LFCLK_QUALIFIED 0
#define INKTAG_BOARD_LFCLK_FREQUENCY_HZ 32768u
#define INKTAG_BOARD_TIMEBASE_TICKS_PER_SECOND 32768u
#define INKTAG_BOARD_TIMEBASE_COUNTER_MASK UINT32_MAX
#define INKTAG_BOARD_LFCLK_ACCURACY_PPM 0u

/* Recovered identically from both factory board records. Record order is
 * CS, BS, RST, SCLK, MOSI, DC, BUSY, EN; InkTag config order differs. */
#define INKTAG_BOARD_DEFAULT_EPD_CONFIG \
    {0x15, 0x0F, 0x16, 0x14, 0x10, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0x07}

#define INKTAG_BOARD_HAS_STATUS_LED 1
#define INKTAG_BOARD_HAS_LED_DIAG 1
/* The RGB channels belong to the status-scene runtime. They are not the
 * legacy epd_config.led_pin used by panel BUSY wait code. */
#define INKTAG_BOARD_MANAGED_STATUS_LED 1
#define INKTAG_BOARD_STATUS_LED_PIN 0x11
#define INKTAG_BOARD_LED_BLUE_PIN 0x11
#define INKTAG_BOARD_LED_GREEN_PIN 0x12
#define INKTAG_BOARD_LED_RED_PIN 0x04
#define INKTAG_BOARD_LED_ACTIVE_LOW 1

/* Same GPIO17/Timer1 route used by Recovery. Hardware edges continue through
 * synchronous Flash work; currently claimed input pins still preempt output. */
#define INKTAG_BOARD_STATUS_PWM_TIMER OM_TIM1
#define INKTAG_BOARD_STATUS_PWM_CHANNEL TIM_CHAN_1
#define INKTAG_BOARD_STATUS_PWM_PINMUX PINMUX_PAD17_TIMER1_OUTN1_CFG
#define INKTAG_BOARD_STATUS_PWM_PM_ID PM_ID_TIM1

/* The preserved ZKC21B factory image initializes these unfitted EVB LED
 * pads as push-pull GPIO outputs low. ZKC42B uses the same pins for its
 * qualified external NOR, so only the runtime ZKC21B selector may park them. */
#define INKTAG_BOARD_ZKC21B_STOCK_LOW_PAD_MASK 0x00000F00u
/* Both qualified ZKCxxB records route the active-high panel rail enable to
 * GPIO7. Hold it low while idle, after exact-board selection. */
#define INKTAG_BOARD_ZKCXXB_PANEL_EN_PIN 7u

/* The common image must render the largest qualified panel. This is runtime
 * RAM, not a per-panel Flash asset. */
#define INKTAG_BOARD_GUI_FRAME_BUFFER_BYTES 30000

/* No exact-board exception to the common 0.2 product policy is qualified. */
#define INKTAG_BOARD_POWER_WARN_MV INKTAG_POWER_POLICY_WARNING_MV
#define INKTAG_BOARD_POWER_DISPLAY_MIN_MV INKTAG_POWER_POLICY_DISPLAY_MIN_MV
#define INKTAG_BOARD_POWER_FLASH_MIN_MV INKTAG_POWER_POLICY_FLASH_MIN_MV
#define INKTAG_BOARD_POWER_OTA_MIN_MV INKTAG_POWER_POLICY_OTA_MIN_MV
#define INKTAG_BOARD_POWER_RECOVERY_HYSTERESIS_MV \
    INKTAG_POWER_POLICY_RECOVERY_HYSTERESIS_MV
#define INKTAG_BOARD_POWER_THRESHOLDS_QUALIFIED 0

/* NFC belongs to board configuration, never screen configuration. Both
 * recovered products use the same external-tag wiring. */
#define INKTAG_BOARD_HAS_NFC 1
#define INKTAG_BOARD_NFC_SCL_PIN 0u
#define INKTAG_BOARD_NFC_SDA_PIN 1u
#define INKTAG_BOARD_NFC_IRQ_PIN 2u
#define INKTAG_BOARD_NFC_ENABLE_PIN 19u
#define INKTAG_BOARD_NFC_IRQ_ACTIVE_LOW 1
#define INKTAG_BOARD_NFC_ENABLE_ACTIVE_HIGH 1

/* User-directed GT N692 short-command pilots: B892, 21V-N/3AA2 and 21B.
 * Owner confirmed both 21-series top marks on 2026-09-28, correcting the
 * earlier N892 reading of 21B. Restrict transactions to the
 * reviewed session/SRAM whitelist and validate UID/CC before setup. These
 * flags do not qualify a complete ordering code or a family-wide tag map. */
#define INKTAG_BOARD_ZKC42B_GT_MIRROR_PILOT 1
#define INKTAG_BOARD_ZKC42B_GT_MIRROR_QUALIFIED 0
#define INKTAG_BOARD_ZKC21B_GT_MIRROR_PILOT 1
#define INKTAG_BOARD_ZKC21B_GT_MIRROR_QUALIFIED 0
#define INKTAG_BOARD_ZKC21V_GT_MIRROR_PILOT 1
#define INKTAG_BOARD_ZKC21V_GT_MIRROR_QUALIFIED 0

#endif
/* OM SECTION app END */
#endif

#if defined(INKTAG_OM_EXPORT_ZKC21B)
/* OM SECTION zkc21b BEGIN */
#ifndef INKTAG_BOARD_H
#define INKTAG_BOARD_H

#if !defined(INKTAG_ONMICRO_OM6626)
#error "om6626_zkc21b requires the OnMicro OM6626 port"
#endif

#define INKTAG_BOARD_ID "om6626_zkc21b"
#define INKTAG_BOARD_ARTIFACT "InkTag-om6626_zkc21b"

/* Product-protocol gates are exact-board claims. Find remains opt-in and
 * experimental; the voltage thresholds are conservative candidates until a
 * controlled battery-sag run is completed on this PCB. */
#define INKTAG_BOARD_ACTIVATION_QUALIFIED 1
#define INKTAG_BOARD_LIFECYCLE_QUALIFIED 1
#define INKTAG_BOARD_AUX_IO_QUALIFIED 1
#define INKTAG_BOARD_NFC_VARIANT_QUALIFIED 1
#define INKTAG_BOARD_FIND_QUALIFIED 0
#define INKTAG_BOARD_FIND_EXPERIMENTAL_AVAILABLE 1
#define INKTAG_BOARD_TRANSFER_PACKBITS_QUALIFIED 1

#define INKTAG_BOARD_LFCLK_SOURCE INKTAG_LFCLK_SOURCE_INTERNAL_RC
#define INKTAG_BOARD_LFCLK_QUALITY INKTAG_TIMEBASE_QUALITY_NON_PRECISION
#define INKTAG_BOARD_LFCLK_QUALIFIED 0
#define INKTAG_BOARD_LFCLK_FREQUENCY_HZ 32768u
#define INKTAG_BOARD_TIMEBASE_TICKS_PER_SECOND 32768u
#define INKTAG_BOARD_TIMEBASE_COUNTER_MASK UINT32_MAX
#define INKTAG_BOARD_LFCLK_ACCURACY_PPM 0u

/* This describes the shipped panel only. Pins, SPI wiring, BUSY polarity and
 * board power paths remain in the board configuration below. */
#define INKTAG_BOARD_DEFAULT_SCREEN_CONFIG \
    {EPD_SCREEN_SCHEMA_VERSION, EPD_SCREEN_DRIVER_JD79661, INKTAG_RECIPE_JD79661_ZKC21B_FACTORY_B2, EPD_SCREEN_COLOR_BWRY, 9u, 128u, 250u, 0u, 0u, 122u, 250u}
#define INKTAG_BOARD_EPD_SPI_INDEX 1

/* Recovered from the ZKC21B-NAE4 factory board record.  Record order is
 * CS, BS, RST, SCLK, MOSI, DC, BUSY, EN; InkTag config order differs. */
#define INKTAG_BOARD_DEFAULT_EPD_CONFIG \
    {0x15, 0x0F, 0x16, 0x14, 0x10, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0x07}

#define INKTAG_BOARD_HAS_STATUS_LED 1
#define INKTAG_BOARD_HAS_LED_DIAG 1
/* The RGB channels belong to the status-scene runtime. They are not the
 * legacy epd_config.led_pin used by panel BUSY wait code. */
#define INKTAG_BOARD_MANAGED_STATUS_LED 1
#define INKTAG_BOARD_STATUS_LED_PIN 0x11
#define INKTAG_BOARD_LED_BLUE_PIN 0x11
#define INKTAG_BOARD_LED_GREEN_PIN 0x12
#define INKTAG_BOARD_LED_RED_PIN 0x04
#define INKTAG_BOARD_LED_ACTIVE_LOW 1
#define INKTAG_BOARD_GUI_FRAME_BUFFER_BYTES 8000

#define INKTAG_BOARD_POWER_WARN_MV INKTAG_POWER_POLICY_WARNING_MV
#define INKTAG_BOARD_POWER_DISPLAY_MIN_MV INKTAG_POWER_POLICY_DISPLAY_MIN_MV
#define INKTAG_BOARD_POWER_FLASH_MIN_MV INKTAG_POWER_POLICY_FLASH_MIN_MV
#define INKTAG_BOARD_POWER_OTA_MIN_MV INKTAG_POWER_POLICY_OTA_MIN_MV
#define INKTAG_BOARD_POWER_RECOVERY_HYSTERESIS_MV \
    INKTAG_POWER_POLICY_RECOVERY_HYSTERESIS_MV
#define INKTAG_BOARD_POWER_THRESHOLDS_QUALIFIED 0

/* The ZKC21B-NAE4 and ZKC42B-NE4 factory NFC records and selected initializer
 * are identical. NFC wiring remains board configuration, never screen data. */
#define INKTAG_BOARD_HAS_NFC 1
#define INKTAG_BOARD_NFC_SCL_PIN 0u
#define INKTAG_BOARD_NFC_SDA_PIN 1u
#define INKTAG_BOARD_NFC_IRQ_PIN 2u
#define INKTAG_BOARD_NFC_ENABLE_PIN 19u
#define INKTAG_BOARD_NFC_IRQ_ACTIVE_LOW 1
#define INKTAG_BOARD_NFC_ENABLE_ACTIVE_HIGH 1

/* Factory type B2 is evidence for the ZKC21B command path only.
 * Its 122x250 viewport and rotation/mirror defaults live in the screen
 * config above so calendar, clock and uploaded pictures share one mapping. */

#endif
/* OM SECTION zkc21b END */
#endif

#if defined(INKTAG_OM_EXPORT_ZKC42B)
/* OM SECTION zkc42b BEGIN */
#ifndef INKTAG_BOARD_H
#define INKTAG_BOARD_H

#if !defined(INKTAG_ONMICRO_OM6626)
#error "om6626_zkc42b requires the OnMicro OM6626 port"
#endif

#define INKTAG_BOARD_ID "om6626_zkc42b"
#define INKTAG_BOARD_ARTIFACT "InkTag-om6626_zkc42b"

/* Product-protocol gates are exact-board claims. Find remains opt-in and
 * experimental; the voltage thresholds are conservative candidates until a
 * controlled battery-sag run is completed on this PCB. */
#define INKTAG_BOARD_ACTIVATION_QUALIFIED 1
#define INKTAG_BOARD_LIFECYCLE_QUALIFIED 1
#define INKTAG_BOARD_AUX_IO_QUALIFIED 1
#define INKTAG_BOARD_NFC_VARIANT_QUALIFIED 1
#define INKTAG_BOARD_FIND_QUALIFIED 0
#define INKTAG_BOARD_FIND_EXPERIMENTAL_AVAILABLE 1
#define INKTAG_BOARD_TRANSFER_PACKBITS_QUALIFIED 0

#define INKTAG_BOARD_LFCLK_SOURCE INKTAG_LFCLK_SOURCE_INTERNAL_RC
#define INKTAG_BOARD_LFCLK_QUALITY INKTAG_TIMEBASE_QUALITY_NON_PRECISION
#define INKTAG_BOARD_LFCLK_QUALIFIED 0
#define INKTAG_BOARD_LFCLK_FREQUENCY_HZ 32768u
#define INKTAG_BOARD_TIMEBASE_TICKS_PER_SECOND 32768u
#define INKTAG_BOARD_TIMEBASE_COUNTER_MASK UINT32_MAX
#define INKTAG_BOARD_LFCLK_ACCURACY_PPM 0u

/* This describes the shipped panel only. Pins, SPI wiring, BUSY polarity and
 * board power paths remain in the board configuration below. */
#define INKTAG_BOARD_DEFAULT_SCREEN_CONFIG \
    {EPD_SCREEN_SCHEMA_VERSION, EPD_SCREEN_DRIVER_JD79668AA, INKTAG_RECIPE_JD79668AA_ZKC42B_FACTORY_B3, EPD_SCREEN_COLOR_BWRY, 6u, 400u, 300u, 0u, 0u, 400u, 300u}
#define INKTAG_BOARD_EPD_SPI_INDEX 1

/* Recovered from the ZKC42B-NE4 factory board record.  Record order is
 * CS, BS, RST, SCLK, MOSI, DC, BUSY, EN; InkTag config order differs. */
#define INKTAG_BOARD_DEFAULT_EPD_CONFIG \
    {0x15, 0x0F, 0x16, 0x14, 0x10, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0x07}

#define INKTAG_BOARD_HAS_STATUS_LED 1
#define INKTAG_BOARD_HAS_LED_DIAG 1
/* The RGB channels belong to the status-scene runtime. They are not the
 * legacy epd_config.led_pin used by panel BUSY wait code. */
#define INKTAG_BOARD_MANAGED_STATUS_LED 1
#define INKTAG_BOARD_STATUS_LED_PIN 0x11
#define INKTAG_BOARD_LED_BLUE_PIN 0x11
#define INKTAG_BOARD_LED_GREEN_PIN 0x12
#define INKTAG_BOARD_LED_RED_PIN 0x04
#define INKTAG_BOARD_LED_ACTIVE_LOW 1
#define INKTAG_BOARD_GUI_FRAME_BUFFER_BYTES 30000

#define INKTAG_BOARD_POWER_WARN_MV INKTAG_POWER_POLICY_WARNING_MV
#define INKTAG_BOARD_POWER_DISPLAY_MIN_MV INKTAG_POWER_POLICY_DISPLAY_MIN_MV
#define INKTAG_BOARD_POWER_FLASH_MIN_MV INKTAG_POWER_POLICY_FLASH_MIN_MV
#define INKTAG_BOARD_POWER_OTA_MIN_MV INKTAG_POWER_POLICY_OTA_MIN_MV
#define INKTAG_BOARD_POWER_RECOVERY_HYSTERESIS_MV \
    INKTAG_POWER_POLICY_RECOVERY_HYSTERESIS_MV
#define INKTAG_BOARD_POWER_THRESHOLDS_QUALIFIED 0

/* Recovered from the selected factory NFC initializer, not from the screen
 * record: I2C0 uses PAD0/PAD1, PAD2 is the active-low field/wake input and
 * PAD19 enables the external GT23NT081D-named tag path. */
#define INKTAG_BOARD_HAS_NFC 1
#define INKTAG_BOARD_NFC_SCL_PIN 0u
#define INKTAG_BOARD_NFC_SDA_PIN 1u
#define INKTAG_BOARD_NFC_IRQ_PIN 2u
#define INKTAG_BOARD_NFC_ENABLE_PIN 19u
#define INKTAG_BOARD_NFC_IRQ_ACTIVE_LOW 1
#define INKTAG_BOARD_NFC_ENABLE_ACTIVE_HIGH 1

/* Factory type B3 is evidence for the ZKC42B command path only.
 * The qualified 180-degree rotation and horizontal mirror are encoded in
 * the screen config above and apply to every rendered content type. */

#endif
/* OM SECTION zkc42b END */
#endif

#if defined(INKTAG_OM_EXPORT_ZKC21V)
/* OM SECTION zkc21v BEGIN */
#ifndef INKTAG_BOARD_H
#define INKTAG_BOARD_H

#if !defined(INKTAG_ONMICRO_OM6626)
#error "om6626_zkc21v requires the OnMicro OM6626 port"
#endif

/* Exact OM6626 ZKC21V board with the factory -N (NFC) option, not the
 * GR5331 ZKC21V PCB. The release application is shared with other OM boards.
 * A future non-N assembly must be independently qualified. */
#define INKTAG_BOARD_ID "om6626_zkc21v"
#define INKTAG_BOARD_ARTIFACT "InkTag-om6626_zkc21v"
#define INKTAG_BOARD_EPD_SPI_INDEX 1

/* Type 0x5B dispatches a 128x250/8000-byte dual-plane SSD1680-compatible
 * path in the preserved factory application. Mount transform and 122-column
 * viewport are provisional until an exact-panel four-edge visual test. */
#define INKTAG_BOARD_DEFAULT_SCREEN_CONFIG \
    {EPD_SCREEN_SCHEMA_VERSION, EPD_SCREEN_DRIVER_SSD1680, INKTAG_RECIPE_SSD1680_ZKC21V_FACTORY, EPD_SCREEN_COLOR_BWR, 9u, 128u, 250u, 0u, 0u, 122u, 250u}

/* Factory record at Flash offset 0xA7000: CS, BS, RST, SCLK, MOSI, DC,
 * BUSY, EN = 16 FF 10 0F 15 14 03 07. InkTag order differs. */
#define INKTAG_BOARD_DEFAULT_EPD_CONFIG \
    {0x15, 0x0F, 0x16, 0x14, 0x10, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0x07}

/* The 27-byte stock NFC record equals both known OM boards. Contact-bus and
 * RF behavior still require the exact hardware matrix after first flash. */
#define INKTAG_BOARD_HAS_NFC 1
#define INKTAG_BOARD_NFC_SCL_PIN 0u
#define INKTAG_BOARD_NFC_SDA_PIN 1u
#define INKTAG_BOARD_NFC_IRQ_PIN 2u
#define INKTAG_BOARD_NFC_ENABLE_PIN 19u
#define INKTAG_BOARD_NFC_IRQ_ACTIVE_LOW 1
#define INKTAG_BOARD_NFC_ENABLE_ACTIVE_HIGH 1
#define INKTAG_BOARD_NFC_VARIANT_QUALIFIED 0

/* Exact-board BLE diagnostics optically confirmed all three active-low
 * channels on 2026-09-28. GPIO4 is BOOT at reset and is driven only after
 * the application has completed the boot-pin sampling window. */
#define INKTAG_BOARD_HAS_STATUS_LED 1
#define INKTAG_BOARD_HAS_LED_DIAG 1
#define INKTAG_BOARD_MANAGED_STATUS_LED 1
#define INKTAG_BOARD_LED_RED_PIN 4u
#define INKTAG_BOARD_LED_GREEN_PIN 18u
#define INKTAG_BOARD_LED_BLUE_PIN 17u
#define INKTAG_BOARD_LED_ACTIVE_LOW 1

#define INKTAG_BOARD_ACTIVATION_QUALIFIED 0
#define INKTAG_BOARD_FIND_QUALIFIED 0
#define INKTAG_BOARD_FIND_EXPERIMENTAL_AVAILABLE 0
#define INKTAG_BOARD_TRANSFER_PACKBITS_QUALIFIED 0
#define INKTAG_BOARD_GUI_ASSET_UPDATE_QUALIFIED 0

#define INKTAG_BOARD_LFCLK_SOURCE INKTAG_LFCLK_SOURCE_INTERNAL_RC
#define INKTAG_BOARD_LFCLK_QUALITY INKTAG_TIMEBASE_QUALITY_NON_PRECISION
#define INKTAG_BOARD_LFCLK_QUALIFIED 0
#define INKTAG_BOARD_LFCLK_FREQUENCY_HZ 32768u
#define INKTAG_BOARD_TIMEBASE_TICKS_PER_SECOND 32768u
#define INKTAG_BOARD_TIMEBASE_COUNTER_MASK UINT32_MAX
#define INKTAG_BOARD_LFCLK_ACCURACY_PPM 0u

#endif
/* OM SECTION zkc21v END */
#endif

#if defined(INKTAG_OM_EXPORT_FLASH)
/* OM SECTION flash BEGIN */
#ifndef INKTAG_OM_ZKC42B_EXTERNAL_FLASH_H
#define INKTAG_OM_ZKC42B_EXTERNAL_FLASH_H

/* Exact ZKC42B v1.0 data-Flash record. The package top mark maps to a
 * GT25Q16A and two identical complete reads confirmed JEDEC C4 60 15,
 * SFDP and non-mirrored 2 MiB capacity. */
#define INKTAG_BOARD_EXTERNAL_FLASH_JEDEC_ID 0xC46015u
#define INKTAG_BOARD_EXTERNAL_FLASH_CAPACITY 0x00200000u
/* Layout v2: user explicitly released the stock-picture reservation on
 * 2026-09-28. All 2 MiB are image storage; archival backups remain intact. */
#define INKTAG_BOARD_EXTERNAL_FLASH_STORAGE_OFFSET 0x00000000u
#define INKTAG_BOARD_EXTERNAL_FLASH_MISO_PIN 8u
#define INKTAG_BOARD_EXTERNAL_FLASH_MOSI_PIN 9u
#define INKTAG_BOARD_EXTERNAL_FLASH_CS_PIN   10u
#define INKTAG_BOARD_EXTERNAL_FLASH_SCLK_PIN 11u

#endif
/* OM SECTION flash END */
#endif

#if defined(INKTAG_OM_EXPORT_POLICY)
/* OM SECTION policy BEGIN */
#ifndef INKTAG_OM_OTA_POLICY_H
#define INKTAG_OM_OTA_POLICY_H

/* Signed OTA compatibility policy for the common OM6626 ZKCxxB application.
 * Release versions identify builds but are not an anti-rollback boundary.
 * Increment this epoch only when an older signed application is genuinely
 * incompatible with durable state or the fixed Recovery contract. */
#define INKTAG_OM_OTA_BOARD_ID "om6626_zkcxxb"
#if !defined(INKTAG_OM_NEXT_LAYOUT)
#define INKTAG_OM_OTA_LAYOUT_ID "single-with-ble-recovery-v2"
#define INKTAG_OM_OTA_LAYOUT_SIGNATURE     0x32524D4Fu /* OMR2 */
#else
#define INKTAG_OM_OTA_LAYOUT_ID "layout-v1"
#define INKTAG_OM_OTA_LAYOUT_SIGNATURE     0x31565A4Fu /* OZV1 */
#endif
#define INKTAG_OM_OTA_BOARD_SIGNATURE      0x42584D4Fu /* "OMXB" */
#define INKTAG_OM_OTA_VERIFIER_SIGNATURE   0x314B4D4Fu /* "OMK1" */
/* Epoch 1 is the minimum for this Recovery, including a freshly Full-flashed
 * device with erased journals. Release-number changes within it remain valid. */
#define INKTAG_OM_OTA_COMPATIBILITY_EPOCH  1u

#endif
/* OM SECTION policy END */
#endif

#if defined(INKTAG_OM_EXPORT_RECOVERY)
/* OM SECTION recovery BEGIN */
#define INKTAG_OM_RECOVERY_LED_RED_PIN      4u
#define INKTAG_OM_RECOVERY_LED_GREEN_PIN    18u
#define INKTAG_OM_RECOVERY_LED_BLUE_PIN     17u
#define INKTAG_OM_RECOVERY_PWM_TIMER OM_TIM1
#define INKTAG_OM_RECOVERY_PWM_CHANNEL TIM_CHAN_1
#define INKTAG_OM_RECOVERY_PWM_PINMUX PINMUX_PAD17_TIMER1_OUTN1_CFG
#define INKTAG_OM_RECOVERY_PWM_PM_ID PM_ID_TIM1
/* OM SECTION recovery END */
#endif

#endif
