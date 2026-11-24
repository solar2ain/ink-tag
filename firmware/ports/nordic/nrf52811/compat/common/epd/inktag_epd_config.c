#include "inktag_epd_config.h"

#include <stddef.h>
#include <string.h>
#include "inktag_text.h"

#include "sdk_config.h"
#include "inktag_board.h"

/* This predicate concerns the wiring projection only, not storage absence,
 * record validity or entitlement. Configuration callers retain their backend's
 * read-error contract. The 13-byte field order is a persistent ABI. */
_Static_assert(sizeof(epd_config_t) == 13u, "EPD wiring projection ABI");


#define EPD_DISPLAY_STATE_VERSION 1u

static bool epd_display_state_valid(const epd_display_state_t* state) {
    if (state == NULL || state->mode > EPD_DISPLAY_STATE_DEVICE_INFO) return false;
    return state->mode != EPD_DISPLAY_STATE_SLIDESHOW ||
           (state->interval_seconds >= 30u && state->interval_seconds <= 86400u);
}

#if defined(INKTAG_DEVICE_SETTINGS_ENABLED) && INKTAG_DEVICE_SETTINGS_ENABLED
static bool device_settings_utf8_valid(const uint8_t* text, uint8_t length) {
    return length <= EPD_DEVICE_LABEL_MAX_BYTES &&
           inktag_text_utf8_valid(text, length, 0u);
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
    inktag_button_bindings_default(&settings->button);
}

static bool epd_device_settings_valid(const epd_device_settings_t* settings) {
    if (settings == NULL || settings->version != EPD_DEVICE_SETTINGS_VERSION ||
        settings->name_length > EPD_DEVICE_NAME_MAX_BYTES ||
        settings->label_length > EPD_DEVICE_LABEL_MAX_BYTES ||
        (settings->name_length > 0u &&
         !epd_device_name_valid(settings->name, settings->name_length)) ||
        !epd_device_label_valid(settings->label, settings->label_length)) return false;
    /* Old custom button bytes are opaque compatibility data in InkTag.
     * Validate identity only; reads and label writes preserve them verbatim. */
    return true;
}
#endif

#if !defined(INKTAG_NRF_COMPACT_STORE) && !FDS_ENABLED && !defined(INKTAG_GOODIX)
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
#endif

#if defined(INKTAG_NRF_COMPACT_STORE)

#include "inktag_nrf_native_v1.inc"

#elif FDS_ENABLED

#include "app_scheduler.h"
#include "fds.h"
#include "nordic_common.h"
#include "nrf_delay.h"
#include "nrf_log.h"

#define CONFIG_FILE_ID 0x0000
#define CONFIG_REC_KEY 0x0001
#define TIME_FILE_ID 0x4954
#define TIME_REC_KEY 0x0002
#define SCREEN_FILE_ID 0x4953
#define SCREEN_REC_KEY 0x0003
#define DISPLAY_STATE_FILE_ID 0x4955
#define DISPLAY_STATE_REC_KEY 0x0004
#define DEVICE_INFO_FILE_ID 0x4957
#define DEVICE_INFO_REC_KEY 0x0005
#if defined(INKTAG_DEVICE_SETTINGS_ENABLED) && INKTAG_DEVICE_SETTINGS_ENABLED
#define DEVICE_SETTINGS_FILE_ID 0x4956
#define DEVICE_SETTINGS_REC_KEY 0x0006
#define DEVICE_SETTINGS_RECORD_MAGIC 0x31565344UL
#endif
#define TIME_RECORD_MAGIC 0x494E4B54UL
#define DISPLAY_STATE_RECORD_MAGIC 0x53504445UL
#define DEVICE_INFO_RECORD_MAGIC 0x31525157UL
#define TIME_MIN_VALID 1704067200UL
#define TIME_MAX_VALID 4102444800UL

typedef struct {
    uint32_t magic;
    uint32_t timestamp;
} epd_time_record_t;

#define EPD_TIME_RECORD_VERSION 2u
typedef struct {
    uint32_t magic;
    uint16_t version;
    int16_t timezone_minutes;
    uint32_t timestamp;
    uint32_t checksum;
} epd_time_state_record_t;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t reserved[3];
    epd_display_state_t state;
} epd_display_state_record_t;

#if defined(INKTAG_DEVICE_SETTINGS_ENABLED) && INKTAG_DEVICE_SETTINGS_ENABLED
typedef struct {
    uint32_t magic;
    epd_device_settings_t settings;
    uint32_t checksum;
} epd_device_settings_record_t;
#endif

static volatile bool m_fds_ready = false;
static bool m_fds_init_started = false;
static volatile bool m_time_write_pending = false;
static volatile bool m_display_state_write_pending = false;
static bool m_display_state_write_queued = false;
static epd_time_state_record_t m_time_write_record;
static epd_screen_record_t m_screen_write_record;
static volatile ret_code_t m_screen_write_error = NRF_SUCCESS;
static epd_display_state_record_t m_display_state_write_record;
static epd_display_state_t m_display_state_queued;
#if defined(INKTAG_DEVICE_SETTINGS_ENABLED) && INKTAG_DEVICE_SETTINGS_ENABLED
static volatile bool m_device_settings_write_pending = false;
static epd_device_settings_record_t m_device_settings_write_record;
#endif

static bool start_display_state_write(const epd_display_state_t* state);
static void run_display_state_write(void* p_event_data, uint16_t event_size);

static void fds_evt_handler(fds_evt_t const* const p_fds_evt) {
    bool write_finished = p_fds_evt->id == FDS_EVT_WRITE ||
                          p_fds_evt->id == FDS_EVT_UPDATE;
    NRF_LOG_DEBUG("fds evt: id=%d result=%d\n", p_fds_evt->id, p_fds_evt->result);
    if (p_fds_evt->id == FDS_EVT_INIT) {
        m_fds_ready = p_fds_evt->result == NRF_SUCCESS;
    } else if ((p_fds_evt->id == FDS_EVT_WRITE || p_fds_evt->id == FDS_EVT_UPDATE) &&
               p_fds_evt->write.file_id == TIME_FILE_ID && p_fds_evt->write.record_key == TIME_REC_KEY) {
        m_time_write_pending = false;
    } else if ((p_fds_evt->id == FDS_EVT_WRITE || p_fds_evt->id == FDS_EVT_UPDATE) &&
               p_fds_evt->write.file_id == DISPLAY_STATE_FILE_ID &&
               p_fds_evt->write.record_key == DISPLAY_STATE_REC_KEY) {
        if (p_fds_evt->result != NRF_SUCCESS && !m_display_state_write_queued) {
            m_display_state_queued = m_display_state_write_record.state;
            m_display_state_write_queued = true;
        }
        m_display_state_write_pending = false;
#if defined(INKTAG_DEVICE_SETTINGS_ENABLED) && INKTAG_DEVICE_SETTINGS_ENABLED
    } else if ((p_fds_evt->id == FDS_EVT_WRITE || p_fds_evt->id == FDS_EVT_UPDATE) &&
               p_fds_evt->write.file_id == DEVICE_SETTINGS_FILE_ID &&
               p_fds_evt->write.record_key == DEVICE_SETTINGS_REC_KEY) {
        m_device_settings_write_pending = false;
#endif
    }
    if ((write_finished || p_fds_evt->id == FDS_EVT_GC) &&
        m_display_state_write_queued && !m_display_state_write_pending) {
        (void)app_sched_event_put(NULL, 0, run_display_state_write);
    }
}

static void run_fds_gc(void* p_event_data, uint16_t event_size) {
    NRF_LOG_DEBUG("run garbage collection (fds_gc)\n");
    fds_gc();
}

static bool __attribute__((noinline)) open_record(
    uint16_t file_id, uint16_t key, fds_record_desc_t* descriptor,
    fds_flash_record_t* view, uint32_t* bytes) {
    fds_find_token_t search = {0};
    if (fds_record_find(file_id, key, descriptor, &search) != NRF_SUCCESS) {
        if (file_id == CONFIG_FILE_ID && key == CONFIG_REC_KEY)
            NRF_LOG_DEBUG("epd_config_load: record not found\n");
        return false;
    }
    if (fds_record_open(descriptor, view) != NRF_SUCCESS) {
        if (file_id == CONFIG_FILE_ID && key == CONFIG_REC_KEY)
            NRF_LOG_ERROR("epd_config_load: record open failed!");
        return false;
    }
#ifdef INKTAG_NRF_SDK17
    *bytes = sizeof(uint32_t) * view->p_header->length_words;
#else
    *bytes = sizeof(uint32_t) * view->p_header->tl.length_words;
#endif
    return true;
}

static bool __attribute__((noinline)) fds_read_bytes(
    uint16_t file_id, uint16_t key, void* output, uint16_t capacity,
    uint16_t minimum) {
    fds_record_desc_t descriptor;
    fds_flash_record_t view;
    uint32_t available;
    if (!open_record(file_id, key, &descriptor, &view, &available)) return false;
    bool accepted = available >= minimum;
    if (accepted) {
        size_t copied = available < capacity ? available : capacity;
        memcpy(output, view.p_data, copied);
    }
    (void)fds_record_close(&descriptor);
    return accepted;
}

static bool read_record(uint16_t file_id, uint16_t key, void* data, uint16_t size) {
    return fds_read_bytes(file_id, key, data, size, size);
}

static ret_code_t __attribute__((noinline)) fds_submit_bytes(
    uint16_t file_id, uint16_t key, const void* data, uint16_t bytes) {
    fds_record_desc_t descriptor;
    fds_find_token_t search = {0};
    fds_record_t submission;
    submission.file_id = file_id;
    submission.key = key;
#ifdef INKTAG_NRF_SDK17
    submission.data.p_data = data;
    submission.data.length_words = BYTES_TO_WORDS(bytes);
#else
    fds_record_chunk_t chunk = {.p_data = data, .length_words = BYTES_TO_WORDS(bytes)};
    submission.data.num_chunks = 1;
    submission.data.p_chunks = &chunk;
#endif
    bool exists = fds_record_find(file_id, key, &descriptor, &search) == NRF_SUCCESS;
    ret_code_t result = exists ? fds_record_update(&descriptor, &submission) :
                                fds_record_write(&descriptor, &submission);
    if (result == FDS_ERR_NO_SPACE_IN_FLASH)
        (void)app_sched_event_put(NULL, 0, run_fds_gc);
    return result;
}

static ret_code_t __attribute__((noinline)) delete_record(uint16_t file_id, uint16_t key) {
    fds_record_desc_t desc;
    fds_find_token_t token = {0};
    ret_code_t result = fds_record_find(file_id, key, &desc, &token);
    return result == NRF_SUCCESS ? fds_record_delete(&desc) : result;
}

void epd_config_init(epd_config_t* cfg) {
    (void)cfg;
    if (m_fds_init_started) return;
    m_fds_init_started = true;
    ret_code_t result = fds_register(fds_evt_handler);
    if (result == NRF_SUCCESS) {
        result = fds_init();
        if (result != NRF_SUCCESS)
            NRF_LOG_ERROR("fds_init failed, code=%d\n", result);
    } else {
        NRF_LOG_ERROR("fds_register failed, code=%d\n", result);
    }
    if (result != NRF_SUCCESS) {
        m_fds_init_started = false;
        return;
    }
    uint16_t remaining = 3000u;
    while (!m_fds_ready && remaining != 0u) {
        --remaining;
        nrf_delay_ms(1);
    }
    run_fds_gc(NULL, 0);
}

void epd_config_read(epd_config_t* cfg) {
    memset(cfg, UINT8_MAX, sizeof(*cfg));
    (void)fds_read_bytes(CONFIG_FILE_ID, CONFIG_REC_KEY, cfg, sizeof(*cfg), 0u);
}

void epd_config_write(epd_config_t* cfg) {
    ret_code_t result = fds_submit_bytes(CONFIG_FILE_ID, CONFIG_REC_KEY,
                                          cfg, sizeof(*cfg));
    if (result != NRF_SUCCESS)
        NRF_LOG_ERROR("epd_config_save: record write/update failed, code=%d\n", result);
}

void epd_config_clear(epd_config_t* cfg) {
    /* Deleting FDS configuration never changes the caller's RAM projection. */
    (void)cfg;
    ret_code_t result = delete_record(CONFIG_FILE_ID, CONFIG_REC_KEY);
    if (result == FDS_ERR_NOT_FOUND)
        NRF_LOG_DEBUG("epd_config_clear: record not found\n");
    else if (result != NRF_SUCCESS)
        NRF_LOG_ERROR("fds_record_delete failed, code=%d\n", result);
}


bool epd_time_read(uint32_t* timestamp) {
    return epd_time_state_read(timestamp, NULL);
}

bool epd_time_write(uint32_t timestamp) {
    int16_t timezone_minutes = 0;
    uint32_t previous_timestamp = 0u;
    (void)epd_time_state_read(&previous_timestamp, &timezone_minutes);
    return epd_time_state_write(timestamp, timezone_minutes);
}

bool epd_time_state_read(uint32_t* timestamp, int16_t* timezone_minutes) {
    epd_time_state_record_t state;
    bool valid;
    if (!m_fds_ready || timestamp == NULL) return false;
    if (read_record(TIME_FILE_ID, TIME_REC_KEY, &state, sizeof(state))) {
        valid = state.magic == TIME_RECORD_MAGIC &&
                state.version == EPD_TIME_RECORD_VERSION &&
                state.timezone_minutes >= -840 && state.timezone_minutes <= 840 &&
                state.timestamp >= TIME_MIN_VALID && state.timestamp <= TIME_MAX_VALID &&
                state.checksum == epd_record_checksum(
                    (const uint8_t*)&state,
                    offsetof(epd_time_state_record_t, checksum));
        if (valid) {
            *timestamp = state.timestamp;
            if (timezone_minutes != NULL)
                *timezone_minutes = state.timezone_minutes;
            return true;
        }
    }
    return false;
}

bool epd_time_state_write(uint32_t timestamp, int16_t timezone_minutes) {
    ret_code_t ret;
    if (!m_fds_ready || m_time_write_pending || timestamp < TIME_MIN_VALID ||
        timestamp > TIME_MAX_VALID || timezone_minutes < -840 ||
        timezone_minutes > 840) return false;
    memset(&m_time_write_record, 0, sizeof(m_time_write_record));
    m_time_write_record.magic = TIME_RECORD_MAGIC;
    m_time_write_record.version = EPD_TIME_RECORD_VERSION;
    m_time_write_record.timezone_minutes = timezone_minutes;
    m_time_write_record.timestamp = timestamp;
    m_time_write_record.checksum = epd_record_checksum(
        (const uint8_t*)&m_time_write_record,
        offsetof(epd_time_state_record_t, checksum));
    ret = fds_submit_bytes(TIME_FILE_ID, TIME_REC_KEY,
                              &m_time_write_record, sizeof(m_time_write_record));
    if (ret == NRF_SUCCESS) {
        m_time_write_pending = true;
        return true;
    }
    NRF_LOG_ERROR("epd_time_state_write failed, code=%d\n", ret);
    return false;
}

bool epd_screen_config_read(epd_screen_config_t* screen) {
    epd_screen_record_t record;

    if (screen == NULL || !m_fds_ready) return false;
    if (!read_record(SCREEN_FILE_ID, SCREEN_REC_KEY, &record, sizeof(record))) return false;
    return epd_screen_record_decode(&record, screen) == EPD_SCREEN_STATUS_OK;
}

bool epd_screen_config_write(const epd_screen_config_t* screen) {
    ret_code_t result;

    if (!m_fds_ready) {
        m_screen_write_error = FDS_ERR_NOT_INITIALIZED;
        return false;
    }
    if (epd_screen_record_encode(screen, &m_screen_write_record) != EPD_SCREEN_STATUS_OK) {
        m_screen_write_error = FDS_ERR_INVALID_ARG;
        return false;
    }
    result = fds_submit_bytes(SCREEN_FILE_ID, SCREEN_REC_KEY,
                              &m_screen_write_record, sizeof(m_screen_write_record));
    m_screen_write_error = result;
    return result == NRF_SUCCESS;
}

uint32_t epd_screen_config_last_write_error(void) {
    return m_screen_write_error;
}

void epd_screen_config_clear(void) {
    (void)delete_record(SCREEN_FILE_ID, SCREEN_REC_KEY);
}

bool epd_display_state_read(epd_display_state_t* state) {
    epd_display_state_record_t record;

    if (state == NULL || !m_fds_ready) return false;
    if (!read_record(DISPLAY_STATE_FILE_ID, DISPLAY_STATE_REC_KEY,
                     &record, sizeof(record))) return false;
    if (record.magic != DISPLAY_STATE_RECORD_MAGIC || record.version != EPD_DISPLAY_STATE_VERSION ||
        !epd_display_state_valid(&record.state)) return false;
    *state = record.state;
    return true;
}

static bool start_display_state_write(const epd_display_state_t* state) {
    ret_code_t result;

    if (!m_fds_ready || !epd_display_state_valid(state)) return false;
    if (m_display_state_write_pending) {
        m_display_state_queued = *state;
        m_display_state_write_queued = true;
        return true;
    }
    memset(&m_display_state_write_record, 0, sizeof(m_display_state_write_record));
    m_display_state_write_record.magic = DISPLAY_STATE_RECORD_MAGIC;
    m_display_state_write_record.version = EPD_DISPLAY_STATE_VERSION;
    m_display_state_write_record.state = *state;
    result = fds_submit_bytes(DISPLAY_STATE_FILE_ID, DISPLAY_STATE_REC_KEY,
                              &m_display_state_write_record, sizeof(m_display_state_write_record));
    if (result == NRF_SUCCESS) {
        m_display_state_write_pending = true;
        return true;
    }
    if (result == FDS_ERR_NO_SPACE_IN_FLASH || result == FDS_ERR_BUSY ||
        result == FDS_ERR_NO_SPACE_IN_QUEUES) {
        m_display_state_queued = *state;
        m_display_state_write_queued = true;
        return true;
    }
    return false;
}

static void run_display_state_write(void* p_event_data, uint16_t event_size) {
    epd_display_state_t state;
    (void)p_event_data;
    (void)event_size;
    if (m_display_state_write_pending || !m_display_state_write_queued) return;
    state = m_display_state_queued;
    m_display_state_write_queued = false;
    if (!start_display_state_write(&state)) {
        m_display_state_queued = state;
        m_display_state_write_queued = true;
    }
}

bool epd_display_state_write(const epd_display_state_t* state) {
    return start_display_state_write(state);
}

void epd_display_state_clear(void) {
    m_display_state_write_queued = false;
    (void)delete_record(DISPLAY_STATE_FILE_ID, DISPLAY_STATE_REC_KEY);
}

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t modules;
    uint16_t length;
    uint32_t checksum;
    uint8_t data[];
} epd_device_info_qr_record_t;

bool epd_device_info_qr_read(uint8_t slot, epd_device_info_qr_t* qr) {
    fds_flash_record_t flash_record;
    fds_record_desc_t desc;
    const epd_device_info_qr_record_t* record;
    uint32_t record_len;
    if (!m_fds_ready || slot >= EPD_DEVICE_INFO_QR_SLOT_COUNT || qr == NULL) return false;
    if (!open_record(DEVICE_INFO_FILE_ID, DEVICE_INFO_REC_KEY + slot,
                     &desc, &flash_record, &record_len)) return false;
    record = (const epd_device_info_qr_record_t*)flash_record.p_data;
    bool valid = record_len >= sizeof(*record) && record->magic == DEVICE_INFO_RECORD_MAGIC &&
                 record->version == 1u &&
                 record->modules >= 21u && record->modules <= EPD_DEVICE_INFO_QR_MAX_MODULES &&
                 ((record->modules - 21u) & 3u) == 0u &&
                 record->length == ((uint16_t)record->modules * record->modules + 7u) / 8u &&
                 record->length <= EPD_DEVICE_INFO_QR_MAX_BYTES &&
                 record_len >= sizeof(*record) + record->length &&
                 epd_record_checksum(record->data, record->length) == record->checksum;
    if (valid) {
        qr->modules = record->modules;
        qr->length = record->length;
        memcpy(qr->data, record->data, record->length);
    }
    fds_record_close(&desc);
    return valid;
}

#if defined(INKTAG_DEVICE_SETTINGS_ENABLED) && INKTAG_DEVICE_SETTINGS_ENABLED
bool epd_device_settings_read(epd_device_settings_t* settings) {
    epd_device_settings_record_t record;
    if (settings == NULL || !m_fds_ready ||
        !read_record(DEVICE_SETTINGS_FILE_ID, DEVICE_SETTINGS_REC_KEY,
                     &record, sizeof(record)) ||
        record.magic != DEVICE_SETTINGS_RECORD_MAGIC ||
        record.checksum != epd_record_checksum((const uint8_t*)&record,
                                              offsetof(epd_device_settings_record_t, checksum)) ||
        !epd_device_settings_valid(&record.settings)) return false;
    *settings = record.settings;
    return true;
}

bool epd_device_settings_write(const epd_device_settings_t* settings) {
    ret_code_t result;
    if (!m_fds_ready || m_device_settings_write_pending ||
        !epd_device_settings_valid(settings)) return false;
    memset(&m_device_settings_write_record, 0, sizeof(m_device_settings_write_record));
    m_device_settings_write_record.magic = DEVICE_SETTINGS_RECORD_MAGIC;
    m_device_settings_write_record.settings = *settings;
    m_device_settings_write_record.checksum = epd_record_checksum(
        (const uint8_t*)&m_device_settings_write_record,
        offsetof(epd_device_settings_record_t, checksum));
    result = fds_submit_bytes(DEVICE_SETTINGS_FILE_ID, DEVICE_SETTINGS_REC_KEY,
                              &m_device_settings_write_record, sizeof(m_device_settings_write_record));
    if (result == NRF_SUCCESS) m_device_settings_write_pending = true;
    return result == NRF_SUCCESS;
}

void epd_device_settings_clear(void) {
    m_device_settings_write_pending = false;
    (void)delete_record(DEVICE_SETTINGS_FILE_ID, DEVICE_SETTINGS_REC_KEY);
}
#endif
#elif defined(INKTAG_GOODIX)

#include "inktag_gr_health.h"
#include "inktag_gr_settings_store.h"

#if defined(INKTAG_DEVICE_SETTINGS_ENABLED) && INKTAG_DEVICE_SETTINGS_ENABLED
#define EPD_DEVICE_SETTINGS_RECORD_MAGIC 0x31565347UL /* GSV1 */
#endif
#define EPD_CONFIG_RECORD_MAGIC 0x43445045UL /* EPDC */
#define EPD_TIME_RECORD_MAGIC 0x54445045UL /* EPDT */
#define EPD_TIME_RECORD_VERSION 2u
#define EPD_DISPLAY_STATE_RECORD_MAGIC 0x53445045UL /* EPDS */
#define EPD_CONFIG_RECORD_VERSION 1u
#define EPD_CONFIG_STORAGE_SIZE 16u
#define TIME_MIN_VALID 1704067200UL
#define TIME_MAX_VALID 4102444800UL

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t config_size;
    uint8_t config[EPD_CONFIG_STORAGE_SIZE];
    uint32_t checksum;
} epd_goodix_config_record_t;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t reserved;
    int16_t timezone_minutes;
    uint32_t timestamp;
    uint32_t checksum;
} epd_goodix_time_record_t;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t reserved[3];
    epd_display_state_t state;
    uint32_t checksum;
} epd_goodix_display_state_record_t;

#if defined(INKTAG_DEVICE_SETTINGS_ENABLED) && INKTAG_DEVICE_SETTINGS_ENABLED
typedef struct {
    uint32_t magic;
    epd_device_settings_t settings;
    uint32_t checksum;
} epd_goodix_device_settings_record_t;
#endif

_Static_assert(EPD_CONFIG_SIZE <= EPD_CONFIG_STORAGE_SIZE,
               "Goodix NVDS config record is too small");

/* Native GR envelopes share a magic word and a trailing FNV word. Payload
 * validation remains local to each domain; no entitlement is decided here. */
_Static_assert(sizeof(epd_goodix_config_record_t) == 28u, "EPDC ABI");
_Static_assert(sizeof(epd_goodix_time_record_t) == 16u, "GTM1 ABI");
_Static_assert(sizeof(epd_goodix_display_state_record_t) == 20u, "EPDS ABI");
#if defined(INKTAG_DEVICE_SETTINGS_ENABLED) && INKTAG_DEVICE_SETTINGS_ENABLED
_Static_assert(sizeof(epd_goodix_device_settings_record_t) == 60u, "GSV1 ABI");
#endif

static uint32_t goodix_record_checksum(const void* data, size_t length) {
    const uint8_t* bytes = data;
    uint32_t checksum = 2166136261UL;
    while (length != 0u) {
        checksum = (checksum ^ *bytes++) * 16777619UL;
        --length;
    }
    return checksum;
}

static bool goodix_flash_mutation_allowed(void) {
    return inktag_gr_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH,
                                               true);
}

static bool __attribute__((noinline)) goodix_record_load(
    inktag_gr_settings_slot_t slot, void* record, uint16_t bytes,
    uint32_t expected_magic) {
    uint16_t actual = bytes;
    uint32_t magic, checksum;
    const uint8_t* payload = record;
    if (inktag_gr_settings_get(slot, &actual, record) !=
            INKTAG_GR_SETTINGS_READ_OK || actual != bytes) return false;
    memcpy(&magic, payload, sizeof(magic));
    memcpy(&checksum, payload + bytes - sizeof(checksum), sizeof(checksum));
    return magic == expected_magic &&
           checksum == goodix_record_checksum(payload, bytes - sizeof(checksum));
}

static bool __attribute__((noinline)) goodix_record_store(
    inktag_gr_settings_slot_t slot, void* record, uint16_t bytes) {
    uint32_t checksum = goodix_record_checksum(record, bytes - sizeof(checksum));
    memcpy((uint8_t*)record + bytes - sizeof(checksum), &checksum, sizeof(checksum));
    return goodix_flash_mutation_allowed() &&
           inktag_gr_settings_put(slot, bytes, record);
}

/* The result denotes mutation admission, not a durable deletion receipt.
 * Config clear historically resets its RAM projection after admission even
 * if the backend reports deletion failure. Its void API retains that rule. */
static bool goodix_record_clear(inktag_gr_settings_slot_t slot) {
    if (!goodix_flash_mutation_allowed()) return false;
    (void)inktag_gr_settings_delete(slot);
    return true;
}

void epd_config_init(epd_config_t* cfg) {
    /* The platform initializes the Settings backend before this API is used. */
    (void)cfg;
}

void epd_config_read(epd_config_t* cfg) {
    epd_goodix_config_record_t record;
    if (cfg == NULL) return;
    memset(cfg, UINT8_MAX, sizeof(*cfg));
    if (goodix_record_load(INKTAG_GR_SETTINGS_SLOT_EPD_CONFIG, &record,
                           sizeof(record), EPD_CONFIG_RECORD_MAGIC) &&
        record.version == EPD_CONFIG_RECORD_VERSION &&
        record.config_size == EPD_CONFIG_SIZE)
        memcpy(cfg, record.config, sizeof(*cfg));
}

void epd_config_write(epd_config_t* cfg) {
    epd_goodix_config_record_t record;
    if (cfg == NULL) return;
    memset(&record, 0, sizeof(record));
    record.magic = EPD_CONFIG_RECORD_MAGIC;
    record.version = EPD_CONFIG_RECORD_VERSION;
    record.config_size = EPD_CONFIG_SIZE;
    memcpy(record.config, cfg, sizeof(*cfg));
    (void)goodix_record_store(INKTAG_GR_SETTINGS_SLOT_EPD_CONFIG,
                              &record, sizeof(record));
}

void epd_config_clear(epd_config_t* cfg) {
    if (goodix_record_clear(INKTAG_GR_SETTINGS_SLOT_EPD_CONFIG) && cfg != NULL)
        memset(cfg, UINT8_MAX, sizeof(*cfg));
}

bool epd_time_state_read(uint32_t* timestamp, int16_t* timezone_minutes) {
    epd_goodix_time_record_t record;
    uint16_t length = sizeof(record);

    if (timestamp == NULL ||
        inktag_gr_settings_get(INKTAG_GR_SETTINGS_SLOT_TIME, &length,
                               (uint8_t*)&record) !=
            INKTAG_GR_SETTINGS_READ_OK) {
        return false;
    }
    if (length != sizeof(record) || record.magic != EPD_TIME_RECORD_MAGIC ||
        record.version != EPD_TIME_RECORD_VERSION ||
        record.timezone_minutes < -840 || record.timezone_minutes > 840 ||
        record.timestamp < TIME_MIN_VALID || record.timestamp > TIME_MAX_VALID ||
        record.checksum != goodix_record_checksum(
            &record, offsetof(epd_goodix_time_record_t, checksum))) return false;
    *timestamp = record.timestamp;
    if (timezone_minutes != NULL) *timezone_minutes = record.timezone_minutes;
    return true;
}

bool epd_time_state_write(uint32_t timestamp, int16_t timezone_minutes) {
    epd_goodix_time_record_t record;

    if (timestamp < TIME_MIN_VALID || timestamp > TIME_MAX_VALID ||
        timezone_minutes < -840 || timezone_minutes > 840) return false;
    memset(&record, 0, sizeof(record));
    record.magic = EPD_TIME_RECORD_MAGIC;
    record.version = EPD_TIME_RECORD_VERSION;
    record.timezone_minutes = timezone_minutes;
    record.timestamp = timestamp;
    record.checksum = goodix_record_checksum(&record, offsetof(epd_goodix_time_record_t, checksum));
    if (!goodix_flash_mutation_allowed()) return false;
    return inktag_gr_settings_put(INKTAG_GR_SETTINGS_SLOT_TIME,
                                  sizeof(record), (const uint8_t*)&record);
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
    uint16_t length = sizeof(record);
    if (screen == NULL ||
        inktag_gr_settings_get(INKTAG_GR_SETTINGS_SLOT_SCREEN_PROFILE, &length,
                               (uint8_t*)&record) !=
            INKTAG_GR_SETTINGS_READ_OK ||
        length != sizeof(record)) return false;
    return epd_screen_record_decode(&record, screen) == EPD_SCREEN_STATUS_OK;
}

bool epd_screen_config_write(const epd_screen_config_t* screen) {
    epd_screen_record_t record;
    if (epd_screen_record_encode(screen, &record) != EPD_SCREEN_STATUS_OK) return false;
    if (!goodix_flash_mutation_allowed()) return false;
    return inktag_gr_settings_put(INKTAG_GR_SETTINGS_SLOT_SCREEN_PROFILE,
                                  sizeof(record), (const uint8_t*)&record);
}

void epd_screen_config_clear(void) {
    (void)goodix_record_clear(INKTAG_GR_SETTINGS_SLOT_SCREEN_PROFILE);
}

bool epd_display_state_read(epd_display_state_t* state) {
    epd_goodix_display_state_record_t record;
    if (state == NULL ||
        !goodix_record_load(INKTAG_GR_SETTINGS_SLOT_DISPLAY_STATE, &record,
                            sizeof(record), EPD_DISPLAY_STATE_RECORD_MAGIC) ||
        record.version != EPD_DISPLAY_STATE_VERSION ||
        !epd_display_state_valid(&record.state)) return false;
    *state = record.state;
    return true;
}

bool epd_display_state_write(const epd_display_state_t* state) {
    epd_goodix_display_state_record_t record;
    if (!epd_display_state_valid(state)) return false;
    memset(&record, 0, sizeof(record));
    record.magic = EPD_DISPLAY_STATE_RECORD_MAGIC;
    record.version = EPD_DISPLAY_STATE_VERSION;
    record.state = *state;
    return goodix_record_store(INKTAG_GR_SETTINGS_SLOT_DISPLAY_STATE,
                               &record, sizeof(record));
}

void epd_display_state_clear(void) {
    (void)goodix_record_clear(INKTAG_GR_SETTINGS_SLOT_DISPLAY_STATE);
}

#if defined(INKTAG_DEVICE_SETTINGS_ENABLED) && INKTAG_DEVICE_SETTINGS_ENABLED
bool epd_device_settings_read(epd_device_settings_t* settings) {
    epd_goodix_device_settings_record_t record;
    if (settings == NULL ||
        !goodix_record_load(INKTAG_GR_SETTINGS_SLOT_DEVICE_IDENTITY, &record,
                            sizeof(record), EPD_DEVICE_SETTINGS_RECORD_MAGIC) ||
        !epd_device_settings_valid(&record.settings)) return false;
    *settings = record.settings;
    return true;
}

bool epd_device_settings_write(const epd_device_settings_t* settings) {
    epd_goodix_device_settings_record_t record;
    if (!epd_device_settings_valid(settings)) return false;
    memset(&record, 0, sizeof(record));
    record.magic = EPD_DEVICE_SETTINGS_RECORD_MAGIC;
    record.settings = *settings;
    return goodix_record_store(INKTAG_GR_SETTINGS_SLOT_DEVICE_IDENTITY,
                               &record, sizeof(record));
}

void epd_device_settings_clear(void) {
    (void)goodix_record_clear(INKTAG_GR_SETTINGS_SLOT_DEVICE_IDENTITY);
}
#endif


#elif defined(INKTAG_DIALOG)

#include "inktag_display_settings.h"
#include "inktag_da_health.h"
#include "inktag_page_cycle.h"
#include "inktag_protocol_core.h"
#include "inktag_settings_journal.h"
#include "ke_mem.h"
#include "spi_flash.h"

#define DIALOG_TIME_RECORD_MAGIC 0x494E4B54u /* INKT */
#define DIALOG_TIME_RECORD_VERSION 2u
#define DIALOG_DEVICE_INFO_MAGIC 0x31525157u /* WQR1 */
#define DIALOG_DEVICE_IDENTITY_MAGIC 0x31544944u /* DIT1 */
#define DIALOG_TIME_MIN_VALID 1704067200UL
#define DIALOG_TIME_MAX_VALID 4102444800UL

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

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t modules;
    uint16_t length;
    uint32_t checksum;
    uint8_t data[EPD_DEVICE_INFO_QR_MAX_BYTES];
} epd_dialog_device_info_qr_record_t;

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
_Static_assert(sizeof(epd_dialog_device_info_qr_record_t) ==
                   INKTAG_SETTINGS_JOURNAL_MAX_PAYLOAD,
               "Dialog QR record exceeds the Settings Journal bound");

static inktag_settings_journal_t m_settings_store;

/* The ROM receives only RWIP_HEAP_NON_RET_SIZE_JT bytes from this array.
 * The DA application materializer enlarges the backing allocation so the
 * storage/image paths can reuse it after BLE initialization. */
extern uint32_t rwip_heap_non_ret[];

static epd_dialog_device_info_qr_record_t* dialog_qr_record(void) {
    return (epd_dialog_device_info_qr_record_t*)(void*)rwip_heap_non_ret;
}

static bool dialog_flash_begin(void) {
    return spi_flash_release_from_power_down() == SPI_FLASH_ERR_OK;
}

static void dialog_flash_end(void) { (void)spi_flash_power_down(); }

static bool dialog_store_read(void* context, uint32_t address, void* data,
                              uint16_t length) {
    uint32_t actual = 0;
    (void)context;
    return spi_flash_read_data((uint8_t*)data, address, length, &actual) == SPI_FLASH_ERR_OK &&
           actual == length;
}

/* These wrappers sit immediately above the physical erase/write calls. The
 * command handler also checks policy at entry, but background journal writes
 * and a voltage drop between validation and I/O must still fail closed. */
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
    INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_TIME) |
        INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_SCREEN_PROFILE) |
        INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_DISPLAY_STATE) |
        INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_DEVICE_INFO_QR) |
        INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_DEVICE_IDENTITY),
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
    if (settings == NULL || !dialog_store_begin()) return false;
    epd_device_settings_default(settings);
    ok = inktag_settings_journal_read(
             &m_settings_store, INKTAG_SETTINGS_RECORD_DEVICE_IDENTITY,
             &identity, sizeof(identity), &length) &&
         length == sizeof(identity) &&
         identity.magic == DIALOG_DEVICE_IDENTITY_MAGIC &&
         identity.version == EPD_DEVICE_SETTINGS_VERSION &&
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
     * persisted. Only the common identity record's InkTag device label applies. */
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

void epd_config_init(epd_config_t* cfg) {
    (void)cfg;
    if (dialog_store_begin()) dialog_store_end();
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
    const epd_display_state_t* state, inktag_display_settings_t* settings) {
    uint8_t week_start = 0u;
    memset(settings, 0, sizeof(*settings));
    if (state == NULL) return;
    week_start = (uint8_t)(
        (state->reserved & EPD_DISPLAY_STATE_WEEK_START_MASK) >>
        EPD_DISPLAY_STATE_WEEK_START_SHIFT);
    settings->week_start = week_start <= 1u ? week_start : 0u;
    if ((state->reserved & EPD_DISPLAY_STATE_FLAG_BOOT_REFRESH) != 0u)
        settings->flags |= INKTAG_DISPLAY_SETTINGS_FLAG_BOOT_REFRESH;
    if ((state->reserved & EPD_DISPLAY_STATE_FLAG_BRAND_HIDDEN) != 0u)
        settings->flags |= INKTAG_DISPLAY_SETTINGS_FLAG_BRAND_HIDDEN;
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

static bool dialog_display_record_read(
    epd_display_state_t* state, inktag_display_settings_t* settings) {
    epd_dialog_display_state_record_t record;
    uint16_t length = 0u;
    bool ok;
    if (!dialog_store_begin()) return false;
    ok = inktag_settings_journal_read(
             &m_settings_store, INKTAG_SETTINGS_RECORD_DISPLAY_STATE,
             &record, sizeof(record), &length) &&
         dialog_display_record_decode(&record, length, state, settings);
    dialog_store_end();
    return ok;
}

static bool dialog_display_record_write(
    const epd_display_state_t* state,
    const inktag_display_settings_t* settings) {
    epd_dialog_display_state_record_t record;
    size_t wire_length;
    bool ok;
    memset(&record, 0, sizeof(record));
    if (!epd_display_state_valid(state) || settings == NULL) return false;
    wire_length = inktag_display_settings_encode(
        settings, record.settings_wire, sizeof(record.settings_wire));
    if (wire_length == 0u || !dialog_store_begin()) return false;
    record.display_state = *state;
    record.display_state.reserved = dialog_display_settings_reserved(
        record.display_state.reserved, settings);
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

bool epd_display_state_write(const epd_display_state_t* state) {
    inktag_display_settings_t settings;
    if (state == NULL) return false;
    if (!dialog_display_record_read(NULL, &settings))
        dialog_display_settings_default(state, &settings);
    else {
        uint8_t week_start = (uint8_t)(
            (state->reserved & EPD_DISPLAY_STATE_WEEK_START_MASK) >>
            EPD_DISPLAY_STATE_WEEK_START_SHIFT);
        settings.week_start = week_start <= 1u ? week_start : 0u;
        settings.flags = 0u;
        if ((state->reserved & EPD_DISPLAY_STATE_FLAG_BOOT_REFRESH) != 0u)
            settings.flags |= INKTAG_DISPLAY_SETTINGS_FLAG_BOOT_REFRESH;
        if ((state->reserved & EPD_DISPLAY_STATE_FLAG_BRAND_HIDDEN) != 0u)
            settings.flags |= INKTAG_DISPLAY_SETTINGS_FLAG_BRAND_HIDDEN;
    }
    return dialog_display_record_write(state, &settings);
}

bool epd_dialog_display_settings_read(inktag_display_settings_t* settings) {
    if (settings == NULL) return false;
    return dialog_display_record_read(NULL, settings);
}

bool epd_dialog_display_settings_write(
    const epd_display_state_t* state,
    const inktag_display_settings_t* settings) {
    return dialog_display_record_write(state, settings);
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

static bool dialog_settings_clear(inktag_settings_clear_action_t action) {
    bool ok;
    if (!dialog_store_begin()) return false;
    ok = inktag_settings_journal_clear(&m_settings_store, action);
    dialog_store_end();
    return ok;
}

bool epd_settings_reset_and_verify(void) {
    return dialog_settings_clear(INKTAG_SETTINGS_CLEAR_RESET_SETTINGS);
}

bool epd_user_data_settings_clear_and_verify(void) {
    return dialog_settings_clear(INKTAG_SETTINGS_CLEAR_USER_DATA);
}


#else


void epd_config_init(epd_config_t* cfg) {
    (void)cfg;
}


bool epd_time_read(uint32_t* timestamp) {
    (void)timestamp;
    return false;
}

bool epd_time_write(uint32_t timestamp) {
    (void)timestamp;
    return false;
}

bool epd_screen_config_read(epd_screen_config_t* screen) {
    (void)screen;
    return false;
}

bool epd_screen_config_write(const epd_screen_config_t* screen) {
    (void)screen;
    return false;
}

void epd_screen_config_clear(void) {}

static epd_display_state_t m_display_state;
static bool m_display_state_valid;

bool epd_display_state_read(epd_display_state_t* state) {
    if (state == NULL || !m_display_state_valid) return false;
    *state = m_display_state;
    return true;
}

bool epd_display_state_write(const epd_display_state_t* state) {
    if (!epd_display_state_valid(state)) return false;
    m_display_state = *state;
    m_display_state_valid = true;
    return true;
}

void epd_display_state_clear(void) {
    memset(&m_display_state, 0xFF, sizeof(m_display_state));
    m_display_state_valid = false;
}


#endif

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
