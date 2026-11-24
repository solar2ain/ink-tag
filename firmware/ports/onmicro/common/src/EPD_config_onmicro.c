#include "inktag_epd_config.h"

#include <string.h>

#include "nvds.h"
#include "om_driver.h"
#include "inktag_om_device_settings.h"
#include "inktag_om_nvds.h"
#include "inktag_board.h"
#include "inktag_wall_clock.h"

#define INKTAG_NVDS_TAG_EPD_CONFIG ((nvds_tag_id_t)NVDS_TAG_USER_BEGIN)
#define INKTAG_NVDS_TAG_SCREEN_CONFIG ((nvds_tag_id_t)(NVDS_TAG_USER_BEGIN + 1))
#define INKTAG_NVDS_TAG_DISPLAY_STATE ((nvds_tag_id_t)(NVDS_TAG_USER_BEGIN + 2))
#define INKTAG_NVDS_TAG_TIME ((nvds_tag_id_t)(NVDS_TAG_USER_BEGIN + 10))
#define INKTAG_NVDS_CONFIG_MAGIC 0x494B5447u
#define INKTAG_NVDS_CONFIG_VERSION 1u
#define INKTAG_NVDS_DISPLAY_STATE_MAGIC 0x53504445u
#define INKTAG_NVDS_DISPLAY_STATE_VERSION 1u
#define INKTAG_NVDS_TIME_MAGIC 0x314D544Fu /* OTM1 */
#define INKTAG_NVDS_TIME_VERSION 1u
#define INKTAG_TIME_MIN_VALID 1704067200u
#define INKTAG_TIME_MAX_VALID 4102444800u
#define INKTAG_TIME_REBOOT_ANCHOR_MARKER 0x494Bu
#define INKTAG_TIME_REBOOT_MAX_SECONDS (4u * 60u * 60u)

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t version;
    epd_config_t config;
    uint8_t checksum;
} inktag_nvds_config_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t version;
    uint8_t state[sizeof(epd_display_state_t)];
    uint8_t checksum;
} inktag_nvds_display_state_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t version;
    uint32_t timestamp;
    int16_t timezone_minutes;
    uint32_t reboot_anchor_tick;
    uint8_t checksum;
} inktag_nvds_time_t;

static uint8_t config_checksum(const inktag_nvds_config_t *record) {
    uint8_t checksum = record->version;
    const uint8_t *bytes = (const uint8_t *)&record->config;
    for (uint8_t i = 0; i < sizeof(record->config); i++) checksum ^= bytes[i];
    return checksum;
}

static uint8_t display_state_checksum(const inktag_nvds_display_state_t *record) {
    uint8_t checksum = record->version;
    const uint8_t *bytes = record->state;
    for (uint8_t i = 0; i < sizeof(record->state); i++) checksum ^= bytes[i];
    return checksum;
}

static uint8_t time_checksum(const inktag_nvds_time_t *record) {
    uint8_t checksum = record->version;
    const uint8_t *bytes = (const uint8_t *)&record->timestamp;
    for (uint8_t i = 0; i < sizeof(record->timestamp) +
                                  sizeof(record->timezone_minutes) +
                                  sizeof(record->reboot_anchor_tick); i++) checksum ^= bytes[i];
    return checksum;
}

static bool display_state_valid(const epd_display_state_t *state) {
    if (state == NULL || state->mode > EPD_DISPLAY_STATE_DEVICE_INFO) return false;
    return state->mode != EPD_DISPLAY_STATE_SLIDESHOW ||
           (state->interval_seconds >= 30u && state->interval_seconds <= 86400u);
}

void epd_config_init(epd_config_t *cfg) {
    (void)cfg;
}

void epd_config_read(epd_config_t *cfg) {
    inktag_nvds_config_t record;
    nvds_tag_len_t length = sizeof(record);
    memset(cfg, 0xFF, sizeof(*cfg));
    memset(&record, 0, sizeof(record));
    if (nvds_get(INKTAG_NVDS_TAG_EPD_CONFIG, &length, &record) != NVDS_OK ||
        length != sizeof(record) || record.magic != INKTAG_NVDS_CONFIG_MAGIC ||
        record.version != INKTAG_NVDS_CONFIG_VERSION || record.checksum != config_checksum(&record)) return;
    memcpy(cfg, &record.config, sizeof(*cfg));
}

void epd_config_write(epd_config_t *cfg) {
    inktag_nvds_config_t record = {
        .magic = INKTAG_NVDS_CONFIG_MAGIC,
        .version = INKTAG_NVDS_CONFIG_VERSION,
        .config = *cfg,
    };
    record.checksum = config_checksum(&record);
    (void)inktag_om_nvds_put(INKTAG_NVDS_TAG_EPD_CONFIG,
                             sizeof(record), &record);
}

static bool time_record_read(inktag_nvds_time_t *record) {
    nvds_tag_len_t length = sizeof(*record);
    if (!record || nvds_get(INKTAG_NVDS_TAG_TIME, &length, record) != NVDS_OK)
        return false;
    return length == sizeof(*record) && record->magic == INKTAG_NVDS_TIME_MAGIC &&
           record->version == INKTAG_NVDS_TIME_VERSION &&
           record->checksum == time_checksum(record) &&
           record->timestamp >= INKTAG_TIME_MIN_VALID &&
           record->timestamp <= INKTAG_TIME_MAX_VALID &&
           record->timezone_minutes >= -840 && record->timezone_minutes <= 840;
}

bool epd_time_state_read(uint32_t *timestamp, int16_t *timezone_minutes) {
    inktag_nvds_time_t record;
    uint32_t restored_timestamp;
    bool anchor_armed;
    if (!timestamp || !timezone_minutes || !time_record_read(&record)) return false;
    /* The saved offset is valid independently of clock continuity. */
    *timezone_minutes = record.timezone_minutes;
    anchor_armed = drv_pmu_retention_reg_get() == INKTAG_TIME_REBOOT_ANCHOR_MARKER;
    if (anchor_armed) drv_pmu_retention_reg_set(0u);
    /* No retained anchor (power loss or WDT) means the NVDS value is stale.
     * The 32-bit PMU counter also wraps after ~36 h; the 4 h bound covers
     * ordinary controlled OTA/reset, not an arbitrarily long Recovery stay. */
    if (!inktag_wall_clock_restore_bounded(
            anchor_armed, record.timestamp, record.reboot_anchor_tick,
            drv_pmu_timer_cnt_get(), INKTAG_BOARD_TIMEBASE_TICKS_PER_SECOND,
            INKTAG_TIME_REBOOT_MAX_SECONDS, INKTAG_TIME_MAX_VALID,
            &restored_timestamp)) return false;
    *timestamp = restored_timestamp;
    return true;
}

bool epd_time_read(uint32_t *timestamp) {
    int16_t timezone_minutes;
    return epd_time_state_read(timestamp, &timezone_minutes);
}

bool epd_time_state_write(uint32_t timestamp, int16_t timezone_minutes) {
    inktag_nvds_time_t record = {
        .magic = INKTAG_NVDS_TIME_MAGIC,
        .version = INKTAG_NVDS_TIME_VERSION,
        .timestamp = timestamp,
        .timezone_minutes = timezone_minutes,
        .reboot_anchor_tick = drv_pmu_timer_cnt_get(),
    };
    if (timestamp < INKTAG_TIME_MIN_VALID || timestamp > INKTAG_TIME_MAX_VALID ||
        timezone_minutes < -840 || timezone_minutes > 840) return false;
    record.checksum = time_checksum(&record);
    if (inktag_om_nvds_put(INKTAG_NVDS_TAG_TIME,
                           sizeof(record), &record) != NVDS_OK) return false;
    drv_pmu_retention_reg_set(0u);
    return true;
}

bool epd_time_state_write_reboot_anchor(uint32_t timestamp,
                                        int16_t timezone_minutes) {
    if (!epd_time_state_write(timestamp, timezone_minutes)) return false;
    drv_pmu_retention_reg_set(INKTAG_TIME_REBOOT_ANCHOR_MARKER);
    return true;
}

bool epd_time_write(uint32_t timestamp) {
    int16_t timezone_minutes = 0;
    inktag_nvds_time_t record;
    /* Preserve the saved timezone even when its wall-clock value is stale. */
    if (time_record_read(&record)) timezone_minutes = record.timezone_minutes;
    return epd_time_state_write(timestamp, timezone_minutes);
}

void epd_device_settings_default(epd_device_settings_t *settings) {
    if (!settings) return;
    memset(settings, 0, sizeof(*settings));
    settings->version = EPD_DEVICE_SETTINGS_VERSION;
    inktag_button_bindings_default(&settings->button);
}

bool epd_device_name_valid(const uint8_t *name, uint8_t length) {
    return inktag_ble_device_name_valid_shared(name, length);
}

bool epd_device_label_valid(const uint8_t *label, uint8_t length) {
    if (!label || length > EPD_DEVICE_LABEL_MAX_BYTES) return false;
    uint8_t i = 0u;
    while (i < length) {
        uint8_t c = label[i++];
        if (c < 0x20u || c == 0x7Fu) return false;
        if (c < 0x80u) continue;
        uint8_t continuation = c >= 0xC2u && c <= 0xDFu ? 1u :
                               c >= 0xE0u && c <= 0xEFu ? 2u :
                               c >= 0xF0u && c <= 0xF4u ? 3u : 0xFFu;
        if (continuation == 0xFFu || continuation > length - i) return false;
        uint8_t first = label[i];
        if ((c == 0xE0u && first < 0xA0u) || (c == 0xEDu && first >= 0xA0u) ||
            (c == 0xF0u && first < 0x90u) || (c == 0xF4u && first >= 0x90u)) return false;
        while (continuation-- > 0u) if ((label[i++] & 0xC0u) != 0x80u) return false;
    }
    return true;
}

bool epd_device_settings_read(epd_device_settings_t *settings) {
    char name[INKTAG_OM_DEVICE_NAME_MAX_BYTES + 1u];
    char label[INKTAG_OM_DEVICE_LABEL_MAX_BYTES + 1u];
    if (!settings) return false;
    epd_device_settings_default(settings);
    if (inktag_om_device_name_read(name)) {
        settings->name_length = (uint8_t)strlen(name);
        memcpy(settings->name, name, settings->name_length);
    }
    if (inktag_om_device_label_read(label)) {
        settings->label_length = (uint8_t)strlen(label);
        memcpy(settings->label, label, settings->label_length);
    }
    inktag_om_device_settings_get_button_bindings(&settings->button);
    return settings->name_length != 0u || settings->label_length != 0u;
}

bool epd_device_settings_write(const epd_device_settings_t *settings) {
    const char *error = NULL;
    if (!settings || settings->version != EPD_DEVICE_SETTINGS_VERSION ||
        !epd_device_label_valid(settings->label, settings->label_length)) return false;
    if (!inktag_om_device_label_write(settings->label, settings->label_length, &error)) return false;
    return true;
}

void epd_device_settings_clear(void) {
    (void)inktag_om_device_settings_reset_user();
}

bool epd_screen_config_read(epd_screen_config_t *screen) {
    epd_screen_record_t record;
    nvds_tag_len_t length = sizeof(record);
    if (screen == NULL ||
        nvds_get(INKTAG_NVDS_TAG_SCREEN_CONFIG, &length, &record) != NVDS_OK ||
        length != sizeof(record)) return false;
    return epd_screen_record_decode_exact(&record, screen) == EPD_SCREEN_STATUS_OK;
}

bool inktag_om_screen_config_is_missing(void) {
    epd_screen_record_t record;
    nvds_tag_len_t length=sizeof(record);
    return nvds_get(INKTAG_NVDS_TAG_SCREEN_CONFIG,&length,&record)==NVDS_TAG_NOT_DEFINED;
}

bool epd_screen_config_write(const epd_screen_config_t *screen) {
    epd_screen_record_t record,actual;
    epd_screen_t resolved;
    nvds_tag_len_t length=sizeof(actual);
    if (!screen || epd_screen_resolve(screen,&resolved)!=EPD_SCREEN_STATUS_OK ||
        resolved.config.panel_recipe!=screen->panel_recipe ||
        epd_screen_record_encode(screen,&record)!=EPD_SCREEN_STATUS_OK)return false;
    /* False after write is uncertain, never a rollback or replay request. */
    return inktag_om_nvds_put(INKTAG_NVDS_TAG_SCREEN_CONFIG,sizeof(record),&record)==NVDS_OK &&
           nvds_get(INKTAG_NVDS_TAG_SCREEN_CONFIG,&length,&actual)==NVDS_OK &&
           length==sizeof(actual) && !memcmp(&record,&actual,sizeof(record));
}

void epd_screen_config_clear(void) {
    (void)inktag_om_nvds_del(INKTAG_NVDS_TAG_SCREEN_CONFIG);
}

bool epd_display_state_read(epd_display_state_t *state) {
    inktag_nvds_display_state_t record;
    epd_display_state_t decoded;
    nvds_tag_len_t length = sizeof(record);
    if (state == NULL ||
        nvds_get(INKTAG_NVDS_TAG_DISPLAY_STATE, &length, &record) != NVDS_OK ||
        length != sizeof(record) || record.magic != INKTAG_NVDS_DISPLAY_STATE_MAGIC ||
        record.version != INKTAG_NVDS_DISPLAY_STATE_VERSION ||
        record.checksum != display_state_checksum(&record)) return false;
    memcpy(&decoded, record.state, sizeof(decoded));
    if (!display_state_valid(&decoded)) return false;
    *state = decoded;
    return true;
}

bool epd_display_state_write(const epd_display_state_t *state) {
    inktag_nvds_display_state_t record;
    if (!display_state_valid(state)) return false;
    memset(&record, 0, sizeof(record));
    record.magic = INKTAG_NVDS_DISPLAY_STATE_MAGIC;
    record.version = INKTAG_NVDS_DISPLAY_STATE_VERSION;
    memcpy(record.state, state, sizeof(*state));
    record.checksum = display_state_checksum(&record);
    return inktag_om_nvds_put(INKTAG_NVDS_TAG_DISPLAY_STATE,
                              sizeof(record), &record) == NVDS_OK;
}

void epd_display_state_clear(void) {
    (void)inktag_om_nvds_del(INKTAG_NVDS_TAG_DISPLAY_STATE);
}

void epd_config_clear(epd_config_t *cfg) {
    (void)inktag_om_nvds_del(INKTAG_NVDS_TAG_EPD_CONFIG);
    memset(cfg, 0xFF, sizeof(*cfg));
}

bool epd_config_empty(epd_config_t *cfg) {
    for (uint8_t i = 0; i < EPD_CONFIG_SIZE; i++) {
        if (((uint8_t *)cfg)[i] != 0xFF) return false;
    }
    return true;
}

bool epd_user_settings_clear_and_verify(epd_config_t *current_config) {
    /* RESET_SETTINGS owns preferences only. Page/QR/NFC content, activation,
     * Find, health and factory/Recovery bytes must not enter this tag list. */
    const nvds_tag_id_t tags[] = {
        INKTAG_NVDS_TAG_EPD_CONFIG,
        INKTAG_NVDS_TAG_SCREEN_CONFIG,
        INKTAG_NVDS_TAG_TIME,
    };
    for (uint8_t i = 0u; i < sizeof(tags) / sizeof(tags[0]); ++i) {
        if (!inktag_om_nvds_delete_verified(tags[i])) return false;
    }
    if (!inktag_om_device_settings_reset_user()) return false;
    epd_display_state_t retained;
    epd_display_state_t readback;
    if (epd_display_state_read(&retained)) {
        /* Mode/source are content (ID 4); mirrored user flags are preferences
         * (ID 7). Preserve firmware safety flags and every content field. */
        retained.reserved &= (uint16_t)~(EPD_DISPLAY_STATE_USER_FLAGS_MASK |
            EPD_DISPLAY_STATE_PRO_FLAGS_MASK | EPD_DISPLAY_STATE_WEEK_START_MASK);
        if (!epd_display_state_write(&retained) ||
            !epd_display_state_read(&readback) ||
            memcmp(&retained, &readback, sizeof(retained)) != 0) return false;
    }
    if (current_config != NULL) memset(current_config, 0xFF, sizeof(*current_config));
    return true;
}
