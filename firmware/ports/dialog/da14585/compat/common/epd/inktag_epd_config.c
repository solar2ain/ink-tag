#include "inktag_epd_config.h"

#include <stddef.h>
#include <string.h>

#include "sdk_config.h"
#include "inktag_board.h"

/* This predicate concerns the wiring projection only, not storage absence,
 * record validity or entitlement. Configuration callers retain their backend's
 * read-error contract. The 13-byte field order is a persistent ABI. */
#if defined(INKTAG_DIALOG)
_Static_assert(sizeof(epd_config_t) == 14u, "DA wiring plus RAM-only polarity ABI");
#else
_Static_assert(sizeof(epd_config_t) == 13u, "EPD wiring projection ABI");
#endif

bool epd_config_empty(epd_config_t* cfg) {
    const uint8_t* cursor = (const uint8_t*)cfg;
    size_t remaining = sizeof(*cfg);
    if (cfg == NULL) return true;
    while (remaining != 0u) {
        --remaining;
        if (cursor[remaining] != UINT8_MAX) return false;
    }
    return true;
}

#define EPD_DISPLAY_STATE_VERSION 1u

/* Preserve this snapshot's platform storage adapters for future comparison;
 * low-resource targets may omit only the dynamic QR persistence path. */
#ifndef INKTAG_DEVICE_INFO_QR_STORAGE_ENABLED
#define INKTAG_DEVICE_INFO_QR_STORAGE_ENABLED 1
#endif

static bool epd_display_state_valid(const epd_display_state_t* state) {
    if (state == NULL || state->mode > EPD_DISPLAY_STATE_DEVICE_INFO) return false;
    return state->mode != EPD_DISPLAY_STATE_SLIDESHOW ||
           (state->interval_seconds >= 30u && state->interval_seconds <= 86400u);
}

#if defined(INKTAG_DEVICE_SETTINGS_ENABLED) && INKTAG_DEVICE_SETTINGS_ENABLED
static bool device_settings_utf8_valid(const uint8_t* text, uint8_t length) {
    uint8_t i = 0u;
    if (length > EPD_DEVICE_LABEL_MAX_BYTES || (length > 0u && text == NULL)) return false;
    while (i < length) {
        uint8_t byte = text[i++];
        uint8_t continuation;
        if (byte < 0x20u || byte == 0x7Fu) return false;
        if (byte < 0x80u) continue;
        continuation = byte >= 0xC2u && byte <= 0xDFu ? 1u :
                       byte >= 0xE0u && byte <= 0xEFu ? 2u :
                       byte >= 0xF0u && byte <= 0xF4u ? 3u : 0xFFu;
        if (continuation == 0xFFu || continuation > length - i) return false;
        if ((byte == 0xE0u && text[i] < 0xA0u) ||
            (byte == 0xEDu && text[i] >= 0xA0u) ||
            (byte == 0xF0u && text[i] < 0x90u) ||
            (byte == 0xF4u && text[i] >= 0x90u)) return false;
        while (continuation-- > 0u) {
            if ((text[i++] & 0xC0u) != 0x80u) return false;
        }
    }
    return true;
}

bool epd_device_name_valid(const uint8_t* name, uint8_t length) {
    return inktag_ble_device_name_valid(name, length);
}

bool epd_device_label_valid(const uint8_t* label, uint8_t length) {
    return device_settings_utf8_valid(label, length);
}

void epd_device_settings_default(epd_device_settings_t* settings) {
    if (settings == NULL) return;
    memset(settings, 0, sizeof(*settings));
    settings->version = EPD_DEVICE_SETTINGS_VERSION;
#if !defined(INKTAG_DIALOG) || INKTAG_BOARD_BUTTON_PRESENT
    inktag_button_bindings_default(&settings->button);
#endif
}

#endif

static struct {
    epd_config_t value;
    bool available;
} m_config_cache;

void epd_config_read(epd_config_t* cfg) {
    if (m_config_cache.available) *cfg = m_config_cache.value;
    else memset(cfg, UINT8_MAX, sizeof(*cfg));
}

void epd_config_write(epd_config_t* cfg) {
    m_config_cache.value = *cfg;
    m_config_cache.available = true;
}

void epd_config_clear(epd_config_t* cfg) {
    memset(cfg, UINT8_MAX, sizeof(*cfg));
    memset(&m_config_cache.value, UINT8_MAX, sizeof(m_config_cache.value));
    m_config_cache.available = false;
}


#include "arch_wdg.h"
#include "inktag_display_settings.h"
#include "inktag_da_health.h"
#include "inktag_da_runtime_scratch.h"
#include "inktag_da_pins.h"
#include "inktag_page_cycle.h"
#include "inktag_protocol_core.h"
#include "inktag_settings_journal.h"
#include "ke_mem.h"
#include "spi_flash.h"

#define DIALOG_TIME_RECORD_MAGIC 0x494E4B54u /* INKT */
#define DIALOG_TIME_RECORD_VERSION 1u
#define DIALOG_DEVICE_INFO_MAGIC 0x31525157u /* WQR1 */
#define DIALOG_DEVICE_IDENTITY_MAGIC 0x31544944u /* DIT1 */

typedef struct {
    uint32_t magic;
    uint16_t version;
    int16_t timezone_minutes;
    uint32_t timestamp;
    uint32_t checksum;
} epd_dialog_time_record_t;

typedef struct {
    epd_display_state_t display_state;
    uint8_t settings_wire[INKTAG_DISPLAY_SETTINGS_BASE_SIZE +
                          INKTAG_DISPLAY_SETTINGS_MAX_NAME_BYTES];
} epd_dialog_display_state_record_t;

#if INKTAG_DEVICE_INFO_QR_STORAGE_ENABLED
typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t modules;
    uint16_t length;
    uint32_t checksum;
    uint8_t data[EPD_DEVICE_INFO_QR_MAX_BYTES];
} epd_dialog_device_info_qr_record_t;
#endif

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t label_length;
    uint8_t reserved[2];
    uint8_t label[EPD_DEVICE_LABEL_MAX_BYTES];
    uint32_t checksum;
} epd_dialog_device_identity_record_t;

_Static_assert(INKTAG_BOARD_SETTINGS_PROFILE_CORE_8K == 1,
               "DA must select the Core 8 KiB Settings profile");
_Static_assert(INKTAG_BOARD_SETTINGS_SIZE == INKTAG_SETTINGS_JOURNAL_SIZE,
               "DA Settings Journal must contain exactly two 4 KiB pages");
_Static_assert(INKTAG_BOARD_SETTINGS_START == 0x0003C000u,
               "DA Settings Journal moved without a reviewed Full layout");
_Static_assert(INKTAG_BOARD_SETTINGS_START + INKTAG_BOARD_SETTINGS_SIZE ==
                   0x0003E000u,
               "DA Settings Journal must leave the final 8 KiB reserved");
_Static_assert(sizeof(epd_dialog_time_record_t) == 16u,
               "common time record exceeds its registered bound");
_Static_assert(sizeof(epd_screen_record_t) == 28u,
               "common screen record exceeds its registered bound");
_Static_assert(sizeof(epd_dialog_display_state_record_t) == 32u,
               "common display-state record exceeds its registered bound");
_Static_assert(sizeof(epd_dialog_device_identity_record_t) == 36u,
               "common device-identity record exceeds its registered bound");
#if INKTAG_DEVICE_INFO_QR_STORAGE_ENABLED
_Static_assert(sizeof(epd_dialog_device_info_qr_record_t) ==
                   INKTAG_SETTINGS_JOURNAL_MAX_PAYLOAD,
               "Dialog QR record exceeds the Settings Journal bound");
#endif

static inktag_settings_journal_t m_settings_store;

#if INKTAG_DEVICE_INFO_QR_STORAGE_ENABLED
static epd_dialog_device_info_qr_record_t* dialog_qr_record(void) {
    return (epd_dialog_device_info_qr_record_t*)(void*)
        inktag_da_transfer_scratch;
}
#endif

static bool dialog_flash_begin(void) {
    return spi_flash_release_from_power_down() == SPI_FLASH_ERR_OK;
}

static void dialog_flash_end(void) { (void)spi_flash_power_down(); }

static bool dialog_store_read(void* context, uint32_t address, void* data,
                              uint16_t length) {
    uint32_t actual = 0;
    (void)context;
    /* Journal scans and compaction can read the entire page repeatedly before
     * the lifecycle ACK. Reload only between completed SDK transfers, not in
     * spi_access's polling loop: a stuck physical SPI transfer must still trip
     * the ordinary watchdog. Do not schedule BLE while this adapter owns SPI. */
    wdg_reload(WATCHDOG_DEFAULT_PERIOD);
    int8_t result = spi_flash_read_data((uint8_t*)data, address, length, &actual);
    wdg_reload(WATCHDOG_DEFAULT_PERIOD);
    return result == SPI_FLASH_ERR_OK && actual == length;
}

/* These wrappers sit immediately above the physical erase/write calls. The
 * command handler also checks policy at entry, but background journal writes
 * and a valid low sample between validation and I/O must still refuse.
 * DA's strict policy refuses both unavailable voltage and valid low samples. */
static bool dialog_flash_erase(void* context, uint32_t address) {
    (void)context;
    return inktag_da_health_operation_allowed(
               INKTAG_POWER_OPERATION_FLASH, false) &&
           spi_flash_block_erase(address, SPI_FLASH_OP_SE) == SPI_FLASH_ERR_OK;
}

static bool dialog_flash_write(void* context, uint32_t address,
                               const void* data, uint16_t length) {
    uint32_t actual = 0u;
    (void)context;
    return inktag_da_health_operation_allowed(
               INKTAG_POWER_OPERATION_FLASH, false) &&
           spi_flash_write_data((uint8_t*)data, address, length, &actual) ==
               SPI_FLASH_ERR_OK &&
           actual == length;
}

static const inktag_settings_journal_io_t m_settings_io = {
    NULL,
    INKTAG_BOARD_SETTINGS_START,
    INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_BOARD_PROFILE) |
        INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_TIME) |
        INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_SCREEN_PROFILE) |
        INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_DISPLAY_STATE) |
        INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_DEVICE_INFO_QR) |
        INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_DEVICE_IDENTITY) |
        INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_DEVICE_SETTINGS),
    dialog_store_read,
    dialog_flash_write,
    dialog_flash_erase,
};

static bool dialog_store_begin(void) {
    if (!dialog_flash_begin()) return false;
    /* The incompatible layout transition is Full-only. Factory containers
     * seed the committed empty page, so an old dual-journal image reached by
     * application-only OTA is never erased or reinterpreted here. */
    if (!inktag_settings_journal_init(
            &m_settings_store, &m_settings_io, false)) {
        dialog_flash_end();
        return false;
    }
    return true;
}

static void dialog_store_end(void) { dialog_flash_end(); }

bool epd_device_settings_read(epd_device_settings_t* settings) {
    epd_dialog_device_identity_record_t identity;
    uint16_t length = 0u;
    bool ok;
    if (settings == NULL) return false;
    /* Dialog callers always need a usable default when a label is absent or
     * storage is unavailable. Initialize once, including the begin-failure
     * path; the false return still reports the actual read outcome. */
    epd_device_settings_default(settings);
    if (!dialog_store_begin()) return false;
    ok = inktag_settings_journal_read(
             &m_settings_store, INKTAG_SETTINGS_RECORD_DEVICE_IDENTITY,
             &identity, sizeof(identity), &length) &&
         length == sizeof(identity) &&
         identity.magic == DIALOG_DEVICE_IDENTITY_MAGIC &&
         identity.version == EPD_DEVICE_SETTINGS_VERSION &&
         identity.reserved[0] == 0u && identity.reserved[1] == 0u &&
         identity.label_length <= EPD_DEVICE_LABEL_MAX_BYTES &&
         epd_device_label_valid(identity.label, identity.label_length) &&
         identity.checksum == epd_record_checksum(
             (const uint8_t*)&identity,
             offsetof(epd_dialog_device_identity_record_t, checksum));
    dialog_store_end();
    if (ok) {
        settings->label_length = identity.label_length;
        memcpy(settings->label, identity.label, sizeof(settings->label));
    }
    return ok;
}

bool epd_device_settings_write(const epd_device_settings_t* settings) {
    epd_dialog_device_identity_record_t identity;
    bool ok;
    /* DA has no writable BLE name and its unqualified input backend is not
     * persisted. Only the common identity record's Free device label applies. */
    if (settings == NULL ||
        settings->version != EPD_DEVICE_SETTINGS_VERSION ||
        settings->name_length != 0u ||
        !epd_device_label_valid(settings->label, settings->label_length) ||
        !dialog_store_begin()) return false;
    memset(&identity, 0, sizeof(identity));
    identity.magic = DIALOG_DEVICE_IDENTITY_MAGIC;
    identity.version = settings->version;
    identity.label_length = settings->label_length;
    memcpy(identity.label, settings->label, sizeof(identity.label));
    identity.checksum = epd_record_checksum(
        (const uint8_t*)&identity,
        offsetof(epd_dialog_device_identity_record_t, checksum));
    ok = inktag_settings_journal_write(
        &m_settings_store, INKTAG_SETTINGS_RECORD_DEVICE_IDENTITY,
        &identity, sizeof(identity));
    dialog_store_end();
    return ok;
}

void epd_device_settings_clear(void) {
    if (!dialog_store_begin()) return;
    (void)inktag_settings_journal_delete(
        &m_settings_store, INKTAG_SETTINGS_RECORD_DEVICE_IDENTITY);
    dialog_store_end();
}

static bool dialog_pin_defaults_read_open(epd_config_t* cfg) {
    uint8_t wire[INKTAG_PIN_CONFIG_SIZE];
    uint16_t length = 0u;
    inktag_settings_journal_read_result_t result;
    bool valid = false;
    if (cfg == NULL) return false;
    memset(cfg, 0xff, sizeof(*cfg));
    result = inktag_settings_journal_read_result(&m_settings_store,
        INKTAG_SETTINGS_RECORD_BOARD_PROFILE, wire, sizeof(wire), &length);
    if (result == INKTAG_SETTINGS_JOURNAL_READ_MISSING) {
        static const uint8_t defaults[] = INKTAG_BOARD_DEFAULT_EPD_CONFIG;
        memcpy(cfg, defaults, sizeof(defaults));
        cfg->enable_active_low = 0u;
        valid = true;
    } else if (result == INKTAG_SETTINGS_JOURNAL_READ_FOUND) {
        valid = inktag_da_pins_decode(wire, length, cfg);
    }
    /* A read error, malformed or unknown block never becomes absence or a
     * boot seed. Invalid config keeps every GPIO disabled in RAM. */
    return valid;
}

bool epd_dialog_pin_defaults_load(epd_config_t* cfg) {
    bool ok;
    if (cfg == NULL) return false;
    memset(cfg, 0xff, sizeof(*cfg));
    if (!dialog_store_begin()) return false;
    ok = dialog_pin_defaults_read_open(cfg);
    dialog_store_end();
    return ok;
}

void epd_config_init(epd_config_t* cfg) {
    (void)cfg;
    m_config_cache.available = epd_dialog_pin_defaults_load(&m_config_cache.value);
}


bool epd_time_state_read(uint32_t* timestamp, int16_t* timezone_minutes) {
    epd_dialog_time_record_t record;
    uint16_t length = 0u;
    bool ok;
    if (timestamp == NULL || !dialog_store_begin()) return false;
    ok = inktag_settings_journal_read(
             &m_settings_store, INKTAG_SETTINGS_RECORD_TIME,
             &record, sizeof(record), &length) &&
         length == sizeof(record) &&
         record.magic == DIALOG_TIME_RECORD_MAGIC &&
         record.version == DIALOG_TIME_RECORD_VERSION &&
         record.timezone_minutes >= -840 && record.timezone_minutes <= 840 &&
         record.timestamp >= DIALOG_TIME_MIN_VALID &&
         record.timestamp <= DIALOG_TIME_MAX_VALID &&
         record.checksum == epd_record_checksum(
             (const uint8_t*)&record,
             offsetof(epd_dialog_time_record_t, checksum));
    dialog_store_end();
    if (!ok) return false;
    *timestamp = record.timestamp;
    if (timezone_minutes != NULL) *timezone_minutes = record.timezone_minutes;
    return true;
}

bool epd_time_state_write(uint32_t timestamp, int16_t timezone_minutes) {
    epd_dialog_time_record_t record;
    bool ok;

    if (timestamp < DIALOG_TIME_MIN_VALID || timestamp > DIALOG_TIME_MAX_VALID ||
        timezone_minutes < -840 || timezone_minutes > 840) return false;
    memset(&record, 0, sizeof(record));
    record.magic = DIALOG_TIME_RECORD_MAGIC;
    record.version = DIALOG_TIME_RECORD_VERSION;
    record.timezone_minutes = timezone_minutes;
    record.timestamp = timestamp;
    record.checksum = epd_record_checksum(
        (const uint8_t*)&record,
        offsetof(epd_dialog_time_record_t, checksum));
    if (!dialog_store_begin()) return false;
    ok = inktag_settings_journal_write(
        &m_settings_store, INKTAG_SETTINGS_RECORD_TIME,
        &record, sizeof(record));
    dialog_store_end();
    return ok;
}

bool epd_time_read(uint32_t* timestamp) {
    return epd_time_state_read(timestamp, NULL);
}

bool epd_time_write(uint32_t timestamp) {
    uint32_t previous_timestamp;
    int16_t timezone_minutes = 0;
    (void)epd_time_state_read(&previous_timestamp, &timezone_minutes);
    return epd_time_state_write(timestamp, timezone_minutes);
}

bool epd_screen_config_read(epd_screen_config_t* screen) {
    epd_screen_record_t record;
    uint16_t length = 0u;
    bool ok;
    if (screen == NULL || !dialog_store_begin()) return false;
    ok = inktag_settings_journal_read(
             &m_settings_store, INKTAG_SETTINGS_RECORD_SCREEN_PROFILE,
             &record, sizeof(record), &length) &&
         length == sizeof(record) &&
         epd_screen_record_decode(&record, screen) == EPD_SCREEN_STATUS_OK;
    dialog_store_end();
    return ok;
}

bool epd_screen_config_write(const epd_screen_config_t* screen) {
    epd_screen_record_t record;
    bool ok;
    if (screen == NULL ||
        epd_screen_record_encode(screen, &record) != EPD_SCREEN_STATUS_OK ||
        !dialog_store_begin()) return false;
    ok = inktag_settings_journal_write(
        &m_settings_store, INKTAG_SETTINGS_RECORD_SCREEN_PROFILE,
        &record, sizeof(record));
    dialog_store_end();
    return ok;
}

static uint16_t dialog_display_settings_reserved(
    uint16_t reserved, const inktag_display_settings_t* settings) {
    uint16_t user_flags = 0u;
    if ((settings->flags & INKTAG_DISPLAY_SETTINGS_FLAG_BOOT_REFRESH) != 0u)
        user_flags |= EPD_DISPLAY_STATE_FLAG_BOOT_REFRESH;
    if ((settings->flags & INKTAG_DISPLAY_SETTINGS_FLAG_BRAND_HIDDEN) != 0u)
        user_flags |= EPD_DISPLAY_STATE_FLAG_BRAND_HIDDEN;
    return (uint16_t)(
        (reserved & (uint16_t)~(EPD_DISPLAY_STATE_WEEK_START_MASK |
                                EPD_DISPLAY_STATE_USER_FLAGS_MASK |
                                EPD_DISPLAY_STATE_PRO_FLAGS_MASK)) |
        ((uint16_t)settings->week_start << EPD_DISPLAY_STATE_WEEK_START_SHIFT) |
        user_flags);
}

static void dialog_display_settings_default(
    inktag_display_settings_t* settings) {
    memset(settings, 0, sizeof(*settings));
    settings->week_start = INKTAG_DISPLAY_SETTINGS_DEFAULT_WEEK_START;
}

static bool dialog_display_record_decode(
    const epd_dialog_display_state_record_t* record,
    uint16_t length,
    epd_display_state_t* state, inktag_display_settings_t* settings) {
    inktag_display_settings_t decoded;
    uint16_t expected_reserved;
    if (length < offsetof(epd_dialog_display_state_record_t, settings_wire) +
                     INKTAG_DISPLAY_SETTINGS_BASE_SIZE ||
        length > sizeof(*record) ||
        !epd_display_state_valid(&record->display_state) ||
        !inktag_display_settings_decode(
            record->settings_wire,
            length - offsetof(epd_dialog_display_state_record_t, settings_wire),
            &decoded))
        return false;
    expected_reserved = dialog_display_settings_reserved(
        record->display_state.reserved, &decoded);
    if (expected_reserved != record->display_state.reserved) return false;
    if (state != NULL) *state = record->display_state;
    if (settings != NULL) *settings = decoded;
    return true;
}

/* New Settings namespace: ID7 alone owns preferences. ID4 is replay state,
 * never a second authority or a boot migration source. Confirmed absence
 * supplies RAM defaults; malformed/unknown/unreadable data stays an error. */
#define DIALOG_PREFERENCES_CAPACITY \
    (INKTAG_DISPLAY_SETTINGS_BASE_SIZE + INKTAG_DISPLAY_SETTINGS_MAX_NAME_BYTES + \
     INKTAG_PIN_CONFIG_SIZE)

static inktag_settings_journal_read_result_t dialog_preferences_record_read_open(
    uint8_t *wire, uint16_t *length, inktag_display_settings_t *settings,
    epd_config_t *override, bool *has_override) {
    epd_config_t scratch;
    *length = 0u;
    if (has_override != NULL) *has_override = false;
    inktag_settings_journal_read_result_t result =
        inktag_settings_journal_read_result(&m_settings_store,
            INKTAG_SETTINGS_RECORD_DEVICE_SETTINGS, wire,
            DIALOG_PREFERENCES_CAPACITY, length);
    if (result == INKTAG_SETTINGS_JOURNAL_READ_FOUND) {
        uint16_t prefix;
        if (*length < INKTAG_DISPLAY_SETTINGS_BASE_SIZE)
            return INKTAG_SETTINGS_JOURNAL_READ_ERROR;
        prefix = INKTAG_DISPLAY_SETTINGS_BASE_SIZE + wire[8];
        if ((*length != prefix && *length != prefix + INKTAG_PIN_CONFIG_SIZE) ||
            !inktag_display_settings_decode(wire, prefix, settings))
            return INKTAG_SETTINGS_JOURNAL_READ_ERROR;
        if (*length != prefix) {
            if (!inktag_da_pins_decode(wire + prefix, INKTAG_PIN_CONFIG_SIZE,
                                       override != NULL ? override : &scratch))
                return INKTAG_SETTINGS_JOURNAL_READ_ERROR;
            if (has_override != NULL) *has_override = true;
        }
    }
    if (result == INKTAG_SETTINGS_JOURNAL_READ_MISSING) {
        dialog_display_settings_default(settings);
        *length = (uint16_t)inktag_display_settings_encode(
            settings, wire, DIALOG_PREFERENCES_CAPACITY);
    }
    return result;
}

static inktag_settings_journal_read_result_t dialog_preferences_read_open(
    inktag_display_settings_t* settings) {
    uint8_t wire[DIALOG_PREFERENCES_CAPACITY];
    uint16_t length;
    return dialog_preferences_record_read_open(wire, &length, settings, NULL, NULL);
}

bool epd_dialog_pin_config_load(epd_config_t *defaults, epd_config_t *effective,
                                bool *has_override) {
    uint8_t wire[DIALOG_PREFERENCES_CAPACITY];
    inktag_display_settings_t settings;
    uint16_t length;
    bool ok;
    if (defaults == NULL || effective == NULL || has_override == NULL) return false;
    *has_override = false;
    ok = dialog_store_begin();
    if (ok) {
        ok = m_settings_store.tail_clean && dialog_pin_defaults_read_open(defaults);
        if (ok) {
            *effective = *defaults;
            ok = dialog_preferences_record_read_open(wire, &length, &settings,
                        effective, has_override) != INKTAG_SETTINGS_JOURNAL_READ_ERROR;
        }
        dialog_store_end();
    }
    if (!ok) {
        memset(defaults, 0xff, sizeof(*defaults));
        memset(effective, 0xff, sizeof(*effective));
        *has_override = false;
    }
    return ok;
}

static bool dialog_preferences_write_verified(const uint8_t *wire, uint16_t length) {
    uint8_t actual[DIALOG_PREFERENCES_CAPACITY];
    uint16_t actual_length = 0u;
    /* False can follow a completed commit. The caller treats it as unknown;
     * do not write again or claim that the previous value survived. */
    bool written = inktag_settings_journal_put_record_atomic(&m_settings_store,
        INKTAG_SETTINGS_RECORD_DEVICE_SETTINGS, wire, length);
    bool verified = inktag_settings_journal_read_result(&m_settings_store,
        INKTAG_SETTINGS_RECORD_DEVICE_SETTINGS, actual, sizeof(actual),
        &actual_length) == INKTAG_SETTINGS_JOURNAL_READ_FOUND &&
        m_settings_store.tail_clean && actual_length == length &&
        memcmp(actual, wire, length) == 0;
    /* A complete page commit can still report failure. Never let later reads
     * select the cached old generation instead of reopening both headers. */
    if (!written || !verified) m_settings_store.initialized = false;
    return written && verified;
}

bool epd_dialog_pin_override_write(const uint8_t *pins) {
    uint8_t wire[DIALOG_PREFERENCES_CAPACITY];
    inktag_display_settings_t settings;
    epd_config_t checked;
    uint16_t length;
    bool ok;
    if (pins != NULL && !inktag_da_pins_decode(pins, INKTAG_PIN_CONFIG_SIZE, &checked))
        return false;
    if (!dialog_store_begin()) return false;
    ok = m_settings_store.tail_clean &&
         dialog_preferences_record_read_open(wire, &length, &settings, NULL, NULL) !=
             INKTAG_SETTINGS_JOURNAL_READ_ERROR;
    if (ok) {
        length = INKTAG_DISPLAY_SETTINGS_BASE_SIZE + settings.name_length;
        if (pins != NULL) {
            memcpy(wire + length, pins, INKTAG_PIN_CONFIG_SIZE);
            length += INKTAG_PIN_CONFIG_SIZE;
        }
        ok = dialog_preferences_write_verified(wire, length);
    }
    dialog_store_end();
    return ok;
}

static bool dialog_display_record_read(
    epd_display_state_t* state, inktag_display_settings_t* settings) {
    epd_dialog_display_state_record_t record;
    uint16_t length = 0u;
    bool ok;
    if (!dialog_store_begin()) return false;
    ok = inktag_settings_journal_read(
             &m_settings_store, INKTAG_SETTINGS_RECORD_DISPLAY_STATE,
             &record, sizeof(record), &length) &&
         dialog_display_record_decode(&record, length, NULL, NULL);
    if (ok) {
        inktag_display_settings_t preferences;
        inktag_settings_journal_read_result_t result =
            dialog_preferences_read_open(&preferences);
        if (result == INKTAG_SETTINGS_JOURNAL_READ_ERROR) ok = false;
        else {
            record.display_state.reserved = dialog_display_settings_reserved(
                record.display_state.reserved, &preferences);
            if (state != NULL) *state = record.display_state;
            if (settings != NULL) *settings = preferences;
        }
    }
    dialog_store_end();
    return ok;
}

static bool dialog_display_record_write(
    const epd_display_state_t* state) {
    epd_dialog_display_state_record_t record;
    inktag_display_settings_t settings;
    size_t wire_length;
    bool ok;
    memset(&record, 0, sizeof(record));
    if (!epd_display_state_valid(state) || !dialog_store_begin()) return false;
    if (dialog_preferences_read_open(&settings) ==
            INKTAG_SETTINGS_JOURNAL_READ_ERROR) {
        dialog_store_end();
        return false;
    }
    wire_length = inktag_display_settings_encode(
        &settings, record.settings_wire, sizeof(record.settings_wire));
    if (wire_length == 0u) {
        dialog_store_end();
        return false;
    }
    record.display_state = *state;
    record.display_state.reserved = dialog_display_settings_reserved(
        record.display_state.reserved, &settings);
    ok = inktag_settings_journal_write(
        &m_settings_store, INKTAG_SETTINGS_RECORD_DISPLAY_STATE,
        &record,
        (uint16_t)(offsetof(epd_dialog_display_state_record_t, settings_wire) +
                   wire_length));
    dialog_store_end();
    return ok;
}

bool epd_display_state_read(epd_display_state_t* state) {
    if (state == NULL) return false;
    return dialog_display_record_read(state, NULL);
}

bool epd_dialog_display_settings_read(inktag_display_settings_t* settings) {
    inktag_settings_journal_read_result_t result;
    if (settings == NULL || !dialog_store_begin()) return false;
    result = dialog_preferences_read_open(settings);
    dialog_store_end();
    return result != INKTAG_SETTINGS_JOURNAL_READ_ERROR;
}

bool epd_display_state_write(const epd_display_state_t* state) {
    return state != NULL && dialog_display_record_write(state);
}

bool epd_dialog_display_settings_write(
    const epd_display_state_t* state,
    const inktag_display_settings_t* settings) {
    uint8_t wire[DIALOG_PREFERENCES_CAPACITY];
    inktag_display_settings_t current;
    uint16_t previous_length;
    size_t length;
    bool has_override;
    bool ok;
    if (!epd_display_state_valid(state) || settings == NULL) return false;
    if (settings->name_length > INKTAG_DISPLAY_SETTINGS_MAX_NAME_BYTES ||
        !dialog_store_begin()) return false;
    /* A new explicit preference save has exactly one persistent authority.
     * Do not also mutate replay ID4 or overwrite unknown/corrupt ID7 data. */
    ok = m_settings_store.tail_clean &&
         dialog_preferences_record_read_open(wire, &previous_length, &current,
            NULL, &has_override) != INKTAG_SETTINGS_JOURNAL_READ_ERROR;
    if (ok) {
        length = INKTAG_DISPLAY_SETTINGS_BASE_SIZE + settings->name_length;
        if (has_override)
            memmove(wire + length,
                wire + INKTAG_DISPLAY_SETTINGS_BASE_SIZE + current.name_length,
                INKTAG_PIN_CONFIG_SIZE);
        ok = inktag_display_settings_encode(settings, wire, length) == length &&
             dialog_preferences_write_verified(wire,
                (uint16_t)(length + (has_override ? INKTAG_PIN_CONFIG_SIZE : 0u)));
    }
    dialog_store_end();
    return ok;
}

void epd_display_state_clear(void) {
    if (!dialog_store_begin()) return;
    (void)inktag_settings_journal_delete(
        &m_settings_store, INKTAG_SETTINGS_RECORD_DISPLAY_STATE);
    dialog_store_end();
}

void epd_screen_config_clear(void) {
    if (!dialog_store_begin()) return;
    (void)inktag_settings_journal_delete(
        &m_settings_store, INKTAG_SETTINGS_RECORD_SCREEN_PROFILE);
    dialog_store_end();
}

#if INKTAG_DEVICE_INFO_QR_STORAGE_ENABLED
static bool dialog_qr_descriptor_valid(
    uint8_t slot, uint8_t modules, uint16_t length) {
    inktag_transfer_descriptor_t descriptor = {
        INKTAG_TRANSFER_OBJECT_DEVICE_INFO_QR,
        modules, 1u, slot, length, 0u, {length, 0u},
    };
    return inktag_device_info_qr_transfer_valid(&descriptor);
}

bool epd_dialog_device_info_qr_prepare(
    uint8_t slot, uint8_t modules, uint16_t length) {
    epd_dialog_device_info_qr_record_t* record = dialog_qr_record();
    (void)slot;
    record->magic = DIALOG_DEVICE_INFO_MAGIC;
    record->version = 1u;
    record->modules = modules;
    record->length = length;
    record->checksum = 0u;
    return true;
}

bool epd_dialog_device_info_qr_write(
    uint8_t slot, uint16_t offset, const uint8_t* data, uint16_t length) {
    epd_dialog_device_info_qr_record_t* record = dialog_qr_record();
    if (record->magic != DIALOG_DEVICE_INFO_MAGIC || slot != 0u ||
        data == NULL || length == 0u || offset > record->length ||
        length > record->length - offset) return false;
    memcpy(&record->data[offset], data, length);
    return true;
}

bool epd_dialog_device_info_qr_commit(uint8_t slot) {
    epd_dialog_device_info_qr_record_t* record = dialog_qr_record();
    uint16_t record_length;
    bool ok;
    if (record->magic != DIALOG_DEVICE_INFO_MAGIC || slot != 0u ||
        !dialog_store_begin()) return false;
    record->checksum = epd_record_checksum(record->data, record->length);
    record_length = (uint16_t)(
        offsetof(epd_dialog_device_info_qr_record_t, data) + record->length);
    ok = inktag_settings_journal_write(
        &m_settings_store, INKTAG_SETTINGS_RECORD_DEVICE_INFO_QR,
        record, record_length);
    dialog_store_end();
    if (ok) record->magic = 0u;
    return ok;
}

void epd_dialog_device_info_qr_abort(void) {
    dialog_qr_record()->magic = 0u;
}

bool epd_dialog_device_info_qr_load(
    uint8_t slot, const uint8_t** data, uint8_t* modules) {
    epd_dialog_device_info_qr_record_t* record = dialog_qr_record();
    uint16_t record_length = 0u;
    bool ok;
    if (data == NULL || modules == NULL) return false;
    if (!dialog_store_begin()) return false;
    ok = inktag_settings_journal_read(
             &m_settings_store, INKTAG_SETTINGS_RECORD_DEVICE_INFO_QR,
             record, sizeof(*record), &record_length) &&
         record->magic == DIALOG_DEVICE_INFO_MAGIC && record->version == 1u &&
         dialog_qr_descriptor_valid(slot, record->modules, record->length) &&
         record_length ==
             offsetof(epd_dialog_device_info_qr_record_t, data) +
                 record->length &&
         epd_record_checksum(record->data, record->length) == record->checksum;
    dialog_store_end();
    if (!ok) return false;
    /* The QR staging record and the GUI framebuffer are separate. Keep the
     * validated matrix in post-boot scratch for the render lifetime instead
     * of consuming the BLE message heap. */
    *data = record->data;
    *modules = record->modules;
    return true;
}

void epd_dialog_device_info_qr_release(const uint8_t* data) {
    (void)data;
}

bool epd_device_info_qr_read(uint8_t slot, epd_device_info_qr_t* qr) {
    const uint8_t* data;
    if (qr == NULL || !epd_dialog_device_info_qr_load(
            slot, &data, &qr->modules)) return false;
    qr->version = 1u;
    qr->length = ((uint16_t)qr->modules * qr->modules + 7u) / 8u;
    memcpy(qr->data, data, qr->length);
    epd_dialog_device_info_qr_release(data);
    return true;
}
#else
bool epd_device_info_qr_read(uint8_t slot, epd_device_info_qr_t* qr) {
    (void)slot;
    (void)qr;
    return false;
}
#endif

bool epd_user_data_settings_prepare_and_verify(void) {
    inktag_display_settings_t settings;
    bool ok;
    if (!dialog_store_begin()) return false;
    if (!m_settings_store.tail_clean) {
        dialog_store_end();
        return false;
    }
    ok = dialog_preferences_read_open(&settings) !=
         INKTAG_SETTINGS_JOURNAL_READ_ERROR;
    dialog_store_end();
    return ok;
}

static bool dialog_settings_clear(bool images_only) {
    bool ok;
    if (!dialog_store_begin()) return false;
    if (!m_settings_store.tail_clean) {
        dialog_store_end();
        return false;
    }
    /* ID4 cannot resurrect preferences: reset drops ID7 once, while image
     * clearing preserves it. Keep the common atomic generation transition. */
    ok = images_only ? inktag_settings_journal_clear_images(&m_settings_store) :
                      inktag_settings_journal_clear_ordinary_settings_v2(&m_settings_store);
    if (!ok) m_settings_store.initialized = false;
    dialog_store_end();
    return ok;
}

bool epd_settings_reset_and_verify(void) {
    /* Permanent-Free DA has no password feature. Preserve any opaque ID5;
     * do not import credentials or interpret an unknown record as password OFF. */
    return dialog_settings_clear(false);
}

bool epd_user_data_settings_clear_and_verify(void) {
    return dialog_settings_clear(true);
}


bool epd_user_settings_clear_and_verify(epd_config_t* current_config) {
    epd_config_t local_config;
    epd_screen_config_t stored_screen;
    epd_display_state_t stored_display;
#if defined(INKTAG_DEVICE_SETTINGS_ENABLED) && INKTAG_DEVICE_SETTINGS_ENABLED
    epd_device_settings_t stored_device_settings;
#endif

    if (current_config == NULL) current_config = &local_config;
    epd_config_clear(current_config);
    epd_screen_config_clear();
    epd_display_state_clear();
#if defined(INKTAG_DEVICE_SETTINGS_ENABLED) && INKTAG_DEVICE_SETTINGS_ENABLED
    epd_device_settings_clear();
#endif
    epd_config_read(&local_config);
    if (!epd_config_empty(&local_config) ||
        epd_screen_config_read(&stored_screen) ||
        epd_display_state_read(&stored_display)) {
        return false;
    }
#if defined(INKTAG_DEVICE_SETTINGS_ENABLED) && INKTAG_DEVICE_SETTINGS_ENABLED
    if (epd_device_settings_read(&stored_device_settings)) return false;
#endif
    return true;
}
