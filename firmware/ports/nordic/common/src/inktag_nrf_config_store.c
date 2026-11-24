#include "inktag_nrf_build_profile.h"
#line 1 "firmware/ports/nordic/common/src/inktag_nrf_config_store.c"
#include "inktag_nrf_config_store.h"
#include "inktag_settings_journal.h"

#include <stddef.h>
#include <string.h>

#include "inktag_board.h"
#include "nrf_error.h"
#if defined(INKTAG_BOARD_EXTERNAL_FLASH_SETTINGS_START)
#include "inktag_nrf_external_flash.h"
#else
#include "nrf_sdh.h"
#include "nrf_sdh_soc.h"
#include "nrf_soc.h"
#endif

#if defined(INKTAG_NRF_PROTOCOL_V2)
#include "inktag_protocol_core.h"
#include "nrf_dfu_types.h"
#include "inktag_nrf_dfu_epoch.h"
#include "inktag_nrf_health.h"
#include "nrf_power.h"
#include "nrf_pwr_mgmt.h"
#endif

#if defined(INKTAG_BOARD_EXTERNAL_FLASH_SETTINGS_START)
#define CONFIG_STORE_START ((uint32_t)INKTAG_BOARD_EXTERNAL_FLASH_SETTINGS_START)
#define CONFIG_STORE_EXTERNAL 1
#elif defined(INKTAG_BOARD_SETTINGS_START)
#define CONFIG_STORE_START ((uint32_t)INKTAG_BOARD_SETTINGS_START)
#define CONFIG_STORE_EXTERNAL 0
#else
#error "The exact nRF board must define its two-page settings journal"
#endif

#if !defined(INKTAG_BOARD_SETTINGS_SIZE)
#error "The exact nRF board must define its settings journal size"
#endif
#define CONFIG_STORE_PAGE_SIZE 4096u
#define CONFIG_STORE_PAGE_COUNT 2u
#define CONFIG_STORE_END (CONFIG_STORE_START + CONFIG_STORE_PAGE_COUNT * CONFIG_STORE_PAGE_SIZE)
#define BOOT_DISPLAY_MAGIC 0x3153424Eu /* NBS1 */
#define BOOT_DISPLAY_VERSION 1u
#define BOOT_DISPLAY_FIRST_REQUEST 0x01u
#define DFU_SETTINGS_VERSION_OFFSET 0x04u
#define DFU_SETTINGS_BANK0_SIZE_OFFSET 0x18u
#define DFU_SETTINGS_BANK0_CRC_OFFSET 0x1Cu
#define DFU_SETTINGS_BANK0_CODE_OFFSET 0x20u
#define DFU_BANK_VALID_APP 0x01u

#if defined(INKTAG_NRF_PROTOCOL_V2)
#define DFU_SETTINGS_CRC_END_OFFSET offsetof(nrf_dfu_settings_t, init_command)
_Static_assert(offsetof(nrf_dfu_settings_t, bank_0.image_size) ==
                   DFU_SETTINGS_BANK0_SIZE_OFFSET, "Nordic Bank 0 size ABI");
_Static_assert(offsetof(nrf_dfu_settings_t, bank_0.image_crc) ==
                   DFU_SETTINGS_BANK0_CRC_OFFSET, "Nordic Bank 0 CRC ABI");
_Static_assert(offsetof(nrf_dfu_settings_t, bank_0.bank_code) ==
                   DFU_SETTINGS_BANK0_CODE_OFFSET, "Nordic Bank 0 code ABI");
#endif

#if !defined(INKTAG_BOARD_DFU_SETTINGS_START)
#error "Nordic boot display policy requires the exact DFU settings address"
#endif

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t flags;
    uint16_t reserved;
    uint32_t app_size;
    uint32_t app_crc;
} boot_display_record_t;

static inktag_settings_journal_t s_journal;
#if !CONFIG_STORE_EXTERNAL
static volatile bool s_flash_pending;
static volatile ret_code_t s_flash_result;
/* Common journal writes bounded 32-byte pieces; SDK source words must be
 * aligned and remain live until its asynchronous operation completes. */
static uint32_t s_program_words[8];
#endif
static uint32_t s_last_error = NRF_SUCCESS;

_Static_assert(sizeof(boot_display_record_t) == 16u, "boot display record size");
_Static_assert((CONFIG_STORE_START &
                (CONFIG_STORE_PAGE_SIZE - 1u)) == 0u,
               "nRF settings journal must be erase-page aligned");
_Static_assert(INKTAG_BOARD_SETTINGS_SIZE ==
                   CONFIG_STORE_PAGE_COUNT * CONFIG_STORE_PAGE_SIZE,
               "nRF settings journal must contain exactly two erase pages");
#if CONFIG_STORE_EXTERNAL
_Static_assert(CONFIG_STORE_END <= INKTAG_BOARD_EXTERNAL_FLASH_ASSET_OFFSET ||
                   CONFIG_STORE_START >=
                       INKTAG_BOARD_EXTERNAL_FLASH_ASSET_OFFSET +
                           INKTAG_BOARD_EXTERNAL_FLASH_ASSET_SIZE,
               "external settings journal must not overlap font assets");
_Static_assert(CONFIG_STORE_END <= INKTAG_BOARD_EXTERNAL_FLASH_CAPACITY,
               "external settings journal exceeds fitted Flash");
#elif defined(INKTAG_BOARD_BOOTLOADER_START)
_Static_assert(INKTAG_BOARD_SETTINGS_START + INKTAG_BOARD_SETTINGS_SIZE ==
                   INKTAG_BOARD_BOOTLOADER_START,
               "nRF settings journal must end at the Bootloader boundary");
#endif

#if !CONFIG_STORE_EXTERNAL
static void flash_event_handler(uint32_t event, void *context) {
    (void)context;
    if (!s_flash_pending) return;
    if (event == NRF_EVT_FLASH_OPERATION_SUCCESS) {
        s_flash_result = NRF_SUCCESS;
        s_flash_pending = false;
    } else if (event == NRF_EVT_FLASH_OPERATION_ERROR) {
        s_flash_result = NRF_ERROR_INTERNAL;
        s_flash_pending = false;
    }
}

NRF_SDH_SOC_OBSERVER(s_config_store_soc_observer, 1,
                     flash_event_handler, NULL);
#endif

static bool flash_read(uint32_t address, void *data, uint32_t length) {
    if (address < CONFIG_STORE_START || address >= CONFIG_STORE_END ||
        length == 0u || length > CONFIG_STORE_END - address || data == NULL) {
        s_last_error = NRF_ERROR_INVALID_PARAM;
        return false;
    }
#if CONFIG_STORE_EXTERNAL
    if (!inktag_nrf_external_flash_begin()) {
        s_last_error = inktag_nrf_external_flash_last_error();
        return false;
    }
    bool ok = inktag_nrf_external_flash_read(address, data, length);
    s_last_error = inktag_nrf_external_flash_last_error();
    inktag_nrf_external_flash_end();
    return ok;
#else
    memcpy(data, (const void *)address, length);
    s_last_error = NRF_SUCCESS;
    return true;
#endif
}

#if !CONFIG_STORE_EXTERNAL
static bool wait_for_flash(ret_code_t result) {
    if (result != NRF_SUCCESS) {
        s_last_error = result;
        s_flash_pending = false;
        return false;
    }
    /* Nordic's app-scheduler dispatch model queues SoftDevice events for the
     * main loop. Config writes run from a BLE command callback in that same
     * loop, so waiting without polling would deadlock the queued flash event. */
    while (s_flash_pending) {
        nrf_sdh_evts_poll();
        if (s_flash_pending) (void)sd_app_evt_wait();
    }
    s_last_error = s_flash_result;
    return s_flash_result == NRF_SUCCESS;
}
#endif

static bool flash_write(uint32_t address, const void *data, uint32_t length) {
#if CONFIG_STORE_EXTERNAL
    const uint8_t *input = (const uint8_t *)data;
    if (address < CONFIG_STORE_START || address >= CONFIG_STORE_END ||
        length == 0u || length > CONFIG_STORE_END - address || data == NULL) {
        s_last_error = NRF_ERROR_INVALID_PARAM;
        return false;
    }
    if (!inktag_nrf_external_flash_begin()) {
        s_last_error = inktag_nrf_external_flash_last_error();
        return false;
    }
    while (length != 0u) {
        uint16_t chunk = (uint16_t)(256u - (address & 255u));
        if (chunk > length) chunk = (uint16_t)length;
        if (!inktag_nrf_external_flash_program(address, input, chunk)) {
            s_last_error = inktag_nrf_external_flash_last_error();
            inktag_nrf_external_flash_end();
            return false;
        }
        address += chunk;
        input += chunk;
        length -= chunk;
    }
    s_last_error = NRF_SUCCESS;
    inktag_nrf_external_flash_end();
    return true;
#else
    ret_code_t result;
    if ((address & 3u) != 0u || (length & 3u) != 0u || length == 0u) {
        s_last_error = NRF_ERROR_INVALID_PARAM;
        return false;
    }
    do {
#if defined(INKTAG_NRF_PROTOCOL_V2)
        /* This is the actual SoftDevice write point, not only the command
         * ingress check. Re-sample after every BUSY retry so a falling supply
         * cannot slip through a previously-approved settings operation. */
        if (!inktag_nrf_health_operation_allowed(
                INKTAG_POWER_OPERATION_FLASH, true)) {
            s_last_error = NRF_ERROR_FORBIDDEN;
            return false;
        }
#endif
        s_flash_result = NRF_ERROR_BUSY;
        s_flash_pending = true;
        result = sd_flash_write((uint32_t *)address, data, length / sizeof(uint32_t));
        if (result == NRF_ERROR_BUSY) {
            s_flash_pending = false;
            nrf_sdh_evts_poll();
            (void)sd_app_evt_wait();
        }
    } while (result == NRF_ERROR_BUSY);
    if (result != NRF_SUCCESS) return wait_for_flash(result);
    return wait_for_flash(NRF_SUCCESS);
#endif
}

static bool flash_erase(uint32_t address) {
    if (address < CONFIG_STORE_START || address >= CONFIG_STORE_END ||
        (address & (CONFIG_STORE_PAGE_SIZE - 1u)) != 0u) {
        s_last_error = NRF_ERROR_INVALID_PARAM;
        return false;
    }
#if CONFIG_STORE_EXTERNAL
    if (!inktag_nrf_external_flash_begin()) {
        s_last_error = inktag_nrf_external_flash_last_error();
        return false;
    }
    bool ok = inktag_nrf_external_flash_erase_sector(address);
    s_last_error = inktag_nrf_external_flash_last_error();
    inktag_nrf_external_flash_end();
    return ok;
#else
    ret_code_t result;
    do {
#if defined(INKTAG_NRF_PROTOCOL_V2)
        /* Compaction and first-page creation use this same lowest-level erase
         * entry, so every physical journal erase receives a fresh sample. */
        if (!inktag_nrf_health_operation_allowed(
                INKTAG_POWER_OPERATION_FLASH, true)) {
            s_last_error = NRF_ERROR_FORBIDDEN;
            return false;
        }
#endif
        s_flash_result = NRF_ERROR_BUSY;
        s_flash_pending = true;
        result = sd_flash_page_erase(address / CONFIG_STORE_PAGE_SIZE);
        if (result == NRF_ERROR_BUSY) {
            s_flash_pending = false;
            nrf_sdh_evts_poll();
            (void)sd_app_evt_wait();
        }
    } while (result == NRF_ERROR_BUSY);
    if (result != NRF_SUCCESS) return wait_for_flash(result);
    return wait_for_flash(NRF_SUCCESS);
#endif
}

static bool journal_read(void *context, uint32_t address, void *data,
                         uint16_t length) {
    (void)context;
    return flash_read(address, data, length);
}

static bool journal_write(void *context, uint32_t address, const void *data,
                          uint16_t length) {
    (void)context;
#if !CONFIG_STORE_EXTERNAL
    if (data == NULL || length > sizeof(s_program_words)) {
        s_last_error = NRF_ERROR_INVALID_PARAM;
        return false;
    }
    memcpy(s_program_words, data, length);
    data = s_program_words;
#endif
    return flash_write(address, data, length);
}

static bool journal_erase(void *context, uint32_t address) {
    (void)context;
    return flash_erase(address);
}

#define NRF_CONFIG_BASIC_RECORDS \
    (INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_BOARD_PROFILE) | \
     INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_TIME) | \
     INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_SCREEN_PROFILE) | \
     INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_DISPLAY_STATE) | \
     INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_DEVICE_INFO_QR) | \
     INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_DEVICE_IDENTITY) | \
     INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_DEVICE_SETTINGS) | \
     INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_ADVERTISING) | \
     INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_HEALTH) | \
     INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_INDICATOR) | \
     INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_BOOT_DISPLAY) | \
     INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_IMAGE_INDEX))
/* Opaque reserved/protected records survive edition changes. This bitmap
 * does not decode NFC/Find/grants or provide any corresponding operation. */
#define NRF_CONFIG_RECORDS \
    (NRF_CONFIG_BASIC_RECORDS | \
     INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_ACTIVATION) | \
     INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_NFC_BINDING) | \
     INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_NFC_ANTI_REPLAY) | \
     INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_FIND_CONFIG) | \
     INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_FIND_DERIVATION_SEED) | \
     INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_OWNER_RESET_PENDING))


static const inktag_settings_journal_io_t s_journal_io = {
    NULL, CONFIG_STORE_START, NRF_CONFIG_RECORDS,
    journal_read, journal_write, journal_erase,
};

static bool media_erased(void) {
    uint32_t address;
    uint8_t bytes[32];
    for (address = CONFIG_STORE_START; address < CONFIG_STORE_END;
         address += sizeof(bytes)) {
        uint8_t i;
        if (!flash_read(address, bytes, sizeof(bytes))) return false;
        for (i = 0u; i < sizeof(bytes); ++i)
            if (bytes[i] != 0xFFu) return false;
    }
    return true;
}

bool inktag_nrf_config_store_init(void) {
    if (s_journal.initialized) return true;
    s_last_error = NRF_SUCCESS;
    if (inktag_settings_journal_init(&s_journal, &s_journal_io, false))
        return true;
    /* A bare base is read-only unprovisioned RAM state. Even proven blank
     * media is not permission for a boot erase/program. First admitted
     * mutation rechecks the whole extent before creating a generation. */
    if (media_erased()) return true;
    if (s_last_error == NRF_SUCCESS) s_last_error = NRF_ERROR_INVALID_DATA;
    return false;
}

static bool config_store_ensure_writable(void) {
    return s_journal.initialized || (media_erased() &&
        inktag_settings_journal_init(&s_journal, &s_journal_io, true));
}

static bool config_record_allowed(inktag_nrf_config_record_type_t type) {
    return (unsigned)type >= 1u && (unsigned)type <= (unsigned)INKTAG_SETTINGS_RECORD_MAX &&
        (NRF_CONFIG_RECORDS & INKTAG_SETTINGS_RECORD_BIT(type)) != 0u;
}

inktag_settings_journal_read_result_t inktag_nrf_config_store_read_result(
    inktag_nrf_config_record_type_t type, void *data, uint16_t capacity,
    uint16_t *length) {
    inktag_settings_journal_read_result_t result;
    s_last_error = NRF_SUCCESS;
    if (!s_journal.initialized) {
        if (data == NULL || length == NULL || !config_record_allowed(type) || !media_erased()) {
            s_last_error = NRF_ERROR_INVALID_DATA;
            return INKTAG_SETTINGS_JOURNAL_READ_ERROR;
        }
        *length = 0u;
        s_last_error = NRF_ERROR_NOT_FOUND;
        return INKTAG_SETTINGS_JOURNAL_READ_MISSING;
    }
    result = inktag_settings_journal_read_result(
        &s_journal, type, data, capacity, length);
    if (result == INKTAG_SETTINGS_JOURNAL_READ_MISSING)
        s_last_error = NRF_ERROR_NOT_FOUND;
    else if (result == INKTAG_SETTINGS_JOURNAL_READ_ERROR &&
             s_last_error == NRF_SUCCESS)
        s_last_error = NRF_ERROR_INVALID_DATA;
    return result;
}

bool inktag_nrf_config_store_read(inktag_nrf_config_record_type_t type,
                                  void *data, uint16_t capacity,
                                  uint16_t *length) {
    return inktag_nrf_config_store_read_result(type, data, capacity, length) ==
        INKTAG_SETTINGS_JOURNAL_READ_FOUND;
}

static bool journal_result(bool result) {
    if (!result && s_last_error == NRF_SUCCESS)
        s_last_error = NRF_ERROR_INVALID_DATA;
    return result;
}

bool inktag_nrf_config_store_write(inktag_nrf_config_record_type_t type,
                                   const void *data, uint16_t length) {
    s_last_error = NRF_SUCCESS;
    if (!data || !length || length > INKTAG_SETTINGS_JOURNAL_MAX_PAYLOAD ||
        !config_record_allowed(type) || !config_store_ensure_writable()) return false;
    return journal_result(inktag_settings_journal_write(
        &s_journal, type, data, length));
}

bool inktag_nrf_config_store_delete(inktag_nrf_config_record_type_t type) {
    s_last_error = NRF_SUCCESS;
    if (!config_record_allowed(type) || !config_store_ensure_writable()) return false;
    return journal_result(inktag_settings_journal_delete(&s_journal, type));
}

static bool config_cleanup_result(bool result) {
    uint32_t error = s_last_error;
    if (!result) {
        /* A failure after magic-last publication may have installed either
         * complete generation. Never keep stale in-RAM authority. */
        s_journal.initialized = false;
        if (!inktag_settings_journal_init(&s_journal, &s_journal_io, false)) {
            if (s_last_error == NRF_SUCCESS)
                s_last_error = NRF_ERROR_INVALID_DATA;
            return false;
        }
        s_last_error = error;
    }
    return journal_result(result);
}

bool inktag_nrf_config_store_put_atomic(inktag_nrf_config_record_type_t type,
                                       const void *data, uint16_t length) {
    s_last_error = NRF_SUCCESS;
    if (!data || !length || length > INKTAG_SETTINGS_JOURNAL_MAX_PAYLOAD ||
        !config_record_allowed(type) || !config_store_ensure_writable()) return false;
    return config_cleanup_result(inktag_settings_journal_put_record_atomic(
        &s_journal, type, data, length));
}

bool inktag_nrf_config_store_clear_images(void) {
    s_last_error = NRF_SUCCESS;
    if (!config_store_ensure_writable()) return false;
    return config_cleanup_result(
        inktag_settings_journal_clear_images(&s_journal));
}

bool inktag_nrf_config_store_time_settings(const void *time, uint16_t time_length,
                                          const void *settings, uint16_t settings_length) {
    const inktag_settings_journal_record_t first = {INKTAG_NRF_CONFIG_TIME, time, time_length};
    const inktag_settings_journal_record_t second = {INKTAG_NRF_CONFIG_DEVICE_SETTINGS,
                                                    settings, settings_length};
    s_last_error = NRF_SUCCESS;
    if (!config_store_ensure_writable()) return false;
    return config_cleanup_result(inktag_settings_journal_put_pair_atomic(
        &s_journal, &first, &second));
}

bool inktag_nrf_config_store_reset_settings(void) {
    s_last_error = NRF_SUCCESS;
    if (!config_store_ensure_writable()) return false;
    return config_cleanup_result(
        inktag_settings_journal_reset_settings_v2(&s_journal));
}

bool inktag_nrf_config_store_inactive_erased(void) {
    if (!s_journal.initialized) return media_erased();
    return journal_result(inktag_settings_journal_inactive_erased(&s_journal));
}

bool inktag_nrf_config_store_scrub_inactive(void) {
    s_last_error = NRF_SUCCESS;
    if (!s_journal.initialized) return media_erased();
    return journal_result(inktag_settings_journal_scrub_inactive(&s_journal));
}

bool inktag_nrf_config_store_find_public_safe(bool first_provision) {
#if INKTAG_NRF_FIND_ENABLED
#define INKTAG_NRF_PRO_SECTION 2
#include "inktag_nrf_config_find_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#line 471 "firmware/ports/nordic/common/src/inktag_nrf_config_store.c"
#else
    (void)first_provision;
    return false;
#endif
}

uint32_t inktag_nrf_config_store_last_error(void) { return s_last_error; }

#if INKTAG_NRF_FIND_ENABLED
#define INKTAG_NRF_PRO_SECTION 3
#include "inktag_nrf_config_find_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#endif

#if defined(INKTAG_NRF_PROTOCOL_V2)
bool inktag_nrf_application_info(uint32_t *size, uint32_t *checksum) {
    const uint32_t *settings =
        (const uint32_t *)(uintptr_t)INKTAG_BOARD_DFU_SETTINGS_START;
    uint32_t image_size;
    if (size == NULL || checksum == NULL) return false;
    *size = 0u;
    *checksum = 0u;
    image_size = settings[DFU_SETTINGS_BANK0_SIZE_OFFSET / sizeof(uint32_t)];
    if (settings[0] == UINT32_MAX ||
        settings[DFU_SETTINGS_VERSION_OFFSET / sizeof(uint32_t)] != 2u ||
        settings[DFU_SETTINGS_BANK0_CODE_OFFSET / sizeof(uint32_t)] != DFU_BANK_VALID_APP ||
        image_size == 0u || image_size > INKTAG_BOARD_APP_LIMIT ||
        (inktag_crc32_update(UINT32_MAX, (const uint8_t *)settings + 4u,
                            DFU_SETTINGS_CRC_END_OFFSET - 4u) ^ UINT32_MAX) != settings[0])
        return false;
    *size = image_size;
    *checksum = settings[DFU_SETTINGS_BANK0_CRC_OFFSET / sizeof(uint32_t)];
    return true;
}
#endif

bool inktag_nrf_boot_display_context(bool *first_display_requested,
                                     bool *ota_install_boot,
                                     bool consume_first_display) {
    const uint32_t *settings =
        (const uint32_t *)(uintptr_t)INKTAG_BOARD_DFU_SETTINGS_START;
    boot_display_record_t saved;
    boot_display_record_t next;
    uint16_t length = 0u;
    bool saved_valid;

    if (first_display_requested == NULL || ota_install_boot == NULL ||
        settings[DFU_SETTINGS_VERSION_OFFSET / sizeof(uint32_t)] == 0u ||
        settings[DFU_SETTINGS_BANK0_CODE_OFFSET / sizeof(uint32_t)] !=
            DFU_BANK_VALID_APP ||
        settings[DFU_SETTINGS_BANK0_SIZE_OFFSET / sizeof(uint32_t)] == 0u) {
        return false;
    }

    saved_valid = inktag_nrf_config_store_read(
                      INKTAG_NRF_CONFIG_BOOT_DISPLAY, &saved,
                      sizeof(saved), &length) &&
                  length == sizeof(saved) && saved.magic == BOOT_DISPLAY_MAGIC &&
                  saved.version == BOOT_DISPLAY_VERSION && saved.reserved == 0u &&
                  (saved.flags & (uint8_t)~BOOT_DISPLAY_FIRST_REQUEST) == 0u;

    memset(&next, 0, sizeof(next));
    next.magic = BOOT_DISPLAY_MAGIC;
    next.version = BOOT_DISPLAY_VERSION;
    next.app_size = settings[DFU_SETTINGS_BANK0_SIZE_OFFSET / sizeof(uint32_t)];
    next.app_crc = settings[DFU_SETTINGS_BANK0_CRC_OFFSET / sizeof(uint32_t)];

    *first_display_requested = saved_valid &&
        (saved.flags & BOOT_DISPLAY_FIRST_REQUEST) != 0u;
    *ota_install_boot = saved_valid &&
        (saved.app_size != next.app_size || saved.app_crc != next.app_crc);

    if (!consume_first_display && *first_display_requested)
        next.flags = BOOT_DISPLAY_FIRST_REQUEST;
    /* Consume an admitted Full marker and checkpoint a newly installed application
     * before any automatic display can be queued. If this power-gated write
     * fails, the caller suppresses automatic work and retries next boot. */
    if (saved_valid && ((*first_display_requested && consume_first_display) || *ota_install_boot)) {
        if (!inktag_nrf_config_store_write(
                INKTAG_NRF_CONFIG_BOOT_DISPLAY, &next, sizeof(next))) {
            *first_display_requested = false;
            *ota_install_boot = false;
            return false;
        }
    }
#if defined(INKTAG_NRF_SDK17)
    /* This function runs only after BLE services and advertising are ready.
     * The signed candidate already passed the 2.60 V admission gate before
     * download. Ask Recovery to commit it as the durable epoch floor using
     * the 2.40 V physical Flash-write floor, including a fresh sample here
     * and again at each settings-page mutation after reset. */
    if (settings[INKTAG_NRF_DFU_PENDING_VERSION_WORD] !=
        INKTAG_NRF_DFU_NO_PENDING_VERSION) {
        if (!inktag_nrf_health_operation_allowed(
                INKTAG_POWER_OPERATION_FLASH, true)) {
            /* Keep ordinary BLE/read-only service available, but neither
             * confirm nor signal OTA success below the write-safe floor. */
            return false;
        }
        nrf_power_gpregret_set(INKTAG_NRF_DFU_CONFIRM_GPREGRET);
        nrf_pwr_mgmt_shutdown(NRF_PWR_MGMT_SHUTDOWN_RESET);
    }
#endif
    return true;
}
