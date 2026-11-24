#include "inktag_nrf_config_store.h"

#include <stddef.h>
#include <string.h>

#include "inktag_board.h"
#include "inktag_protocol_core.h"
#include "inktag_nrf_pin_epoch.h"
#include "nrf_error.h"
#if defined(INKTAG_BOARD_EXTERNAL_FLASH_SETTINGS_START)
#include "inktag_nrf_external_flash.h"
#else
#include "nrf_sdh.h"
#include "nrf_sdh_soc.h"
#include "nrf_soc.h"
#endif

#if defined(INKTAG_NRF_PROTOCOL_V2)
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
#define CONFIG_PAGE_MAGIC 0x31505349u /* ISP1 */
#define CONFIG_RECORD_MAGIC 0x31525349u /* ISR1 */
#define CONFIG_RECORD_COMMIT 0x31435349u /* ISC1 */
#if INKTAG_NRF_STREAMING_CONFIG_STORE
/* Page (4096) minus page header (16), record header (12), commit (4).
 * This is a storage bound, independent of the chunk buffer in RAM. */
#define CONFIG_MAX_PAYLOAD 4064u
#define CONFIG_IO_CHUNK 128u
#else
#define CONFIG_MAX_PAYLOAD 420u
#endif
#define BOOT_DISPLAY_MAGIC 0x3153424Eu /* NBS1 */
#define BOOT_DISPLAY_VERSION 1u
#define BOOT_DISPLAY_FIRST_REQUEST 0x01u
#define DFU_SETTINGS_VERSION_OFFSET 0x04u
#define DFU_SETTINGS_BANK0_SIZE_OFFSET 0x18u
#define DFU_SETTINGS_BANK0_CRC_OFFSET 0x1Cu
#define DFU_SETTINGS_BANK0_CODE_OFFSET 0x20u
#define DFU_BANK_VALID_APP 0x01u

#if !defined(INKTAG_BOARD_DFU_SETTINGS_START)
#error "Nordic boot display policy requires the exact DFU settings address"
#endif

typedef struct {
    uint32_t magic;
    uint32_t generation;
    uint32_t generation_inverse;
    uint32_t reserved;
} config_page_header_t;

typedef struct {
    uint32_t magic;
    uint16_t type;
    uint16_t length;
    uint32_t checksum;
} config_record_header_t;

typedef struct {
    uint32_t address;
    uint16_t length;
    bool present;
} config_record_index_t;

#if !INKTAG_NRF_STREAMING_CONFIG_STORE
typedef struct {
    config_record_header_t header;
    uint8_t payload[CONFIG_MAX_PAYLOAD];
    uint32_t commit;
} config_write_buffer_t;
#endif

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t flags;
    uint16_t reserved;
    uint32_t app_size;
    uint32_t app_crc;
} boot_display_record_t;

static config_record_index_t s_records[INKTAG_NRF_CONFIG_RECORD_MAX + 1u];
#if INKTAG_NRF_STREAMING_CONFIG_STORE
static uint8_t s_io_buffer[CONFIG_IO_CHUNK] __attribute__((aligned(4)));
#else
static config_write_buffer_t s_write_buffer __attribute__((aligned(4)));
static uint8_t s_copy_buffer[CONFIG_MAX_PAYLOAD] __attribute__((aligned(4)));
#endif
static bool s_initialized;
static bool s_tail_clean;
static bool s_blank;
#if !CONFIG_STORE_EXTERNAL
static volatile bool s_flash_pending;
static volatile ret_code_t s_flash_result;
#endif
static uint32_t s_active_page;
static uint32_t s_generation;
static uint16_t s_write_offset;
static uint32_t s_last_error = NRF_SUCCESS;

_Static_assert(sizeof(config_page_header_t) == 16u, "config page header size");
_Static_assert(sizeof(config_record_header_t) == 12u, "config record header size");
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

static uint32_t checksum_update(uint32_t checksum, const void *data, uint16_t length) {
    const uint8_t *bytes = (const uint8_t *)data;
    while (length-- > 0u) {
        checksum ^= *bytes++;
        checksum *= 16777619u;
    }
    return checksum;
}

static uint32_t record_checksum(uint16_t type, const void *data, uint16_t length) {
    uint32_t checksum = 2166136261u;
    checksum = checksum_update(checksum, &type, sizeof(type));
    checksum = checksum_update(checksum, &length, sizeof(length));
    return checksum_update(checksum, data, length);
}

static bool current_record_type_valid(uint16_t type) {
    /* Preserve opaque old journal bytes during compaction, but expose only
     * records actually implemented by this port to readers and writers. */
    return (type >= INKTAG_NRF_CONFIG_BOARD &&
            type <= INKTAG_NRF_CONFIG_DEVICE_SETTINGS) ||
           (type >= INKTAG_NRF_CONFIG_HEALTH &&
            type <= INKTAG_NRF_CONFIG_BOOT_DISPLAY) ||
           type == INKTAG_NRF_CONFIG_CONTENT_CLEAR_PENDING;
}

static uint16_t record_size(uint16_t length) {
    return (uint16_t)(sizeof(config_record_header_t) + ((length + 3u) & ~3u) +
                      sizeof(uint32_t));
}

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

static bool page_header_valid(const config_page_header_t *header) {
    return header->magic == CONFIG_PAGE_MAGIC && header->reserved == UINT32_MAX &&
           header->generation_inverse == ~header->generation;
}

/* All bytes, not just an erased header, must establish a virgin namespace.
 * IO failure and retired/nonempty formats never grant formatting authority. */
static bool media_erased(void) {
    uint8_t bytes[32];
    for (uint32_t at = CONFIG_STORE_START; at < CONFIG_STORE_END; at += sizeof(bytes)) {
        if (!flash_read(at, bytes, sizeof(bytes))) return false;
        for (unsigned i = 0u; i < sizeof(bytes); ++i) {
            if (bytes[i] != 255u) {
                s_last_error = NRF_ERROR_INVALID_DATA;
                return false;
            }
        }
    }
    return true;
}

static bool generation_newer(uint32_t candidate, uint32_t current) {
    return (int32_t)(candidate - current) > 0;
}

#if INKTAG_NRF_STREAMING_CONFIG_STORE
#include "inktag_nrf_config_stream.inc"
#endif

static bool scan_page(uint32_t page, bool build_index, uint16_t *write_offset) {
    uint16_t offset = sizeof(config_page_header_t);
    config_record_header_t header;
    uint32_t commit;
    uint32_t checksum;
    uint16_t padded;

    if (build_index) memset(s_records, 0, sizeof(s_records));
    s_tail_clean = true;
    while (offset + sizeof(header) + sizeof(commit) <= CONFIG_STORE_PAGE_SIZE) {
        if (!flash_read(page + offset, &header, sizeof(header))) return false;
        if (header.magic == 0xFFFFFFFFu) {
            if (header.type != 0xFFFFu || header.length != 0xFFFFu ||
                header.checksum != 0xFFFFFFFFu) s_tail_clean = false;
            break;
        }
        if (header.magic != CONFIG_RECORD_MAGIC ||
            header.type == 0u || header.type > INKTAG_NRF_CONFIG_RECORD_MAX ||
            header.length > CONFIG_MAX_PAYLOAD) {
            s_tail_clean = false;
            break;
        }
        padded = (uint16_t)((header.length + 3u) & ~3u);
        if ((uint32_t)offset + sizeof(header) + padded + sizeof(commit) >
            CONFIG_STORE_PAGE_SIZE) {
            s_tail_clean = false;
            break;
        }
        if (!flash_read(page + offset + sizeof(header) + padded,
                        &commit, sizeof(commit))) return false;
        if (commit != CONFIG_RECORD_COMMIT) {
            s_tail_clean = false;
            break;
        }
        if (header.length > 0u) {
#if INKTAG_NRF_STREAMING_CONFIG_STORE
            if (!stream_checksum(page + offset + sizeof(header), header.type,
                                 header.length, &checksum)) return false;
#else
            if (!flash_read(page + offset + sizeof(header),
                            s_write_buffer.payload, header.length)) return false;
            checksum = record_checksum(header.type, s_write_buffer.payload, header.length);
#endif
            if (checksum != header.checksum) {
                s_tail_clean = false;
                break;
            }
        } else if (header.checksum != record_checksum(header.type, NULL, 0u)) {
            s_tail_clean = false;
            break;
        }
        /* Preserve bounded stable-ID records that this InkTag port cannot use.
         * Cleanup must not discard opaque Pro/Find or retired marker data. */
        if (build_index) {
            s_records[header.type].address = page + offset + sizeof(header);
            s_records[header.type].length = header.length;
            s_records[header.type].present = header.length > 0u;
        }
        offset = (uint16_t)(offset + record_size(header.length));
    }
    *write_offset = offset;
    return true;
}

static bool write_page_header(uint32_t page, uint32_t generation, bool commit) {
    config_page_header_t header;
    memset(&header, 0xFF, sizeof(header));
    header.generation = generation;
    header.generation_inverse = ~generation;
    if (!flash_write(page + sizeof(header.magic), &header.generation,
                     sizeof(header) - sizeof(header.magic))) return false;
    if (!commit) return true;
    header.magic = CONFIG_PAGE_MAGIC;
    return flash_write(page, &header.magic, sizeof(header.magic));
}

static bool append_to_page(uint32_t page, uint16_t *offset, uint16_t type,
                           const void *data, uint16_t length) {
#if INKTAG_NRF_STREAMING_CONFIG_STORE
    return stream_append(page, offset, type, data, length, 0u);
#else
    uint16_t padded = (uint16_t)((length + 3u) & ~3u);
    uint16_t body_length = (uint16_t)(sizeof(config_record_header_t) + padded);
    uint32_t address = page + *offset;

    if (length > CONFIG_MAX_PAYLOAD ||
        (uint32_t)*offset + body_length + sizeof(uint32_t) > CONFIG_STORE_PAGE_SIZE)
        return false;
    memset(&s_write_buffer, 0xFF, body_length + sizeof(uint32_t));
    s_write_buffer.header.magic = CONFIG_RECORD_MAGIC;
    s_write_buffer.header.type = type;
    s_write_buffer.header.length = length;
    s_write_buffer.header.checksum = record_checksum(type, data, length);
    if (length > 0u) memcpy(s_write_buffer.payload, data, length);
    if (!flash_write(address, &s_write_buffer, body_length)) {
        s_tail_clean = false;
        return false;
    }
    s_write_buffer.commit = CONFIG_RECORD_COMMIT;
    if (!flash_write(address + body_length, &s_write_buffer.commit,
                     sizeof(s_write_buffer.commit))) {
        s_tail_clean = false;
        return false;
    }
    *offset = (uint16_t)(*offset + body_length + sizeof(uint32_t));
    return true;
#endif
}

#if !INKTAG_NRF_STREAMING_CONFIG_STORE
static bool compact_store(void) {
    uint32_t old_page = s_active_page;
    uint32_t target_page = old_page == CONFIG_STORE_START ?
                           CONFIG_STORE_START + CONFIG_STORE_PAGE_SIZE :
                           CONFIG_STORE_START;
    uint32_t next_generation = s_generation + 1u;
    uint16_t target_offset = sizeof(config_page_header_t);
    uint8_t type;

    if (!flash_erase(target_page) ||
        !write_page_header(target_page, next_generation, false)) return false;
    for (type = 1u; type <= INKTAG_NRF_CONFIG_RECORD_MAX; ++type) {
        config_record_index_t record = s_records[type];
        if (!record.present) continue;
        if (!flash_read(record.address, s_copy_buffer, record.length) ||
            !append_to_page(target_page, &target_offset, type,
                            s_copy_buffer, record.length)) return false;
    }
    if (!write_page_header(target_page, next_generation, true)) return false;
    s_active_page = target_page;
    s_generation = next_generation;
    if (!scan_page(s_active_page, true, &s_write_offset)) return false;
    /* The new page is committed before the old page is erased. A failed old
     * page erase is harmless; the next compaction retries it first. */
    (void)flash_erase(old_page);
    return true;
}

static bool ensure_space(uint16_t length) {
    uint16_t required = record_size(length);
    if (s_tail_clean && (uint32_t)s_write_offset + required <= CONFIG_STORE_PAGE_SIZE)
        return true;
    if (!compact_store()) return false;
    return (uint32_t)s_write_offset + required <= CONFIG_STORE_PAGE_SIZE;
}
#else
#include "inktag_nrf_config_stream_commit.inc"
#endif

bool inktag_nrf_config_store_init(void) {
    config_page_header_t first;
    config_page_header_t second;
    bool first_valid;
    bool second_valid;

    if (s_initialized) {
        s_last_error = NRF_SUCCESS;
        return true;
    }
    if (!flash_read(CONFIG_STORE_START, &first, sizeof(first)) ||
        !flash_read(CONFIG_STORE_START + CONFIG_STORE_PAGE_SIZE, &second, sizeof(second)))
        return false;
    first_valid = page_header_valid(&first);
    second_valid = page_header_valid(&second);
    if (first_valid && second_valid &&
        (first.generation == second.generation ||
         second.generation - first.generation == UINT32_C(0x80000000))) {
        /* Serial ordering is undefined here. Neither page may grant missing
         * records/defaults or formatting authority. Normal wrap is ordered. */
        s_last_error = NRF_ERROR_INVALID_DATA;
        return false;
    }
    s_blank = false;
    if (!first_valid && !second_valid) {
        if (!media_erased()) return false;
        /* Safe bare base: RAM defaults and configuration access only. The
         * first explicit mutation may create a page; boot never does. */
        s_blank = true;
        s_active_page = CONFIG_STORE_START;
        s_generation = 0u;
        s_write_offset = sizeof(config_page_header_t);
        s_tail_clean = true;
        memset(s_records, 0, sizeof(s_records));
        s_initialized = true;
        s_last_error = NRF_SUCCESS;
        return true;
    } else if (!second_valid ||
               (first_valid && !generation_newer(second.generation, first.generation))) {
        s_active_page = CONFIG_STORE_START;
        s_generation = first.generation;
    } else {
        s_active_page = CONFIG_STORE_START + CONFIG_STORE_PAGE_SIZE;
        s_generation = second.generation;
    }
    if (!scan_page(s_active_page, true, &s_write_offset)) return false;
    s_initialized = true;
    s_last_error = NRF_SUCCESS;
    return true;
}

inktag_settings_journal_read_result_t inktag_nrf_config_store_read_result(
    inktag_nrf_config_record_type_t type, void *data, uint16_t capacity,
    uint16_t *length) {
    config_record_index_t *record;
    if (!s_initialized || !current_record_type_valid(type) ||
        data == NULL || length == NULL) {
        s_last_error = NRF_ERROR_INVALID_PARAM;
        return INKTAG_SETTINGS_JOURNAL_READ_ERROR;
    }
    record = &s_records[type];
    if (!record->present) {
        *length = 0u;
        if (!s_tail_clean) {
            s_last_error = NRF_ERROR_INVALID_DATA;
            return INKTAG_SETTINGS_JOURNAL_READ_ERROR;
        }
        s_last_error = NRF_ERROR_NOT_FOUND;
        return INKTAG_SETTINGS_JOURNAL_READ_MISSING;
    }
    if (record->length > capacity) {
        s_last_error = NRF_ERROR_DATA_SIZE;
        return INKTAG_SETTINGS_JOURNAL_READ_ERROR;
    }
    if (!flash_read(record->address, data, record->length))
        return INKTAG_SETTINGS_JOURNAL_READ_ERROR;
    *length = record->length;
    s_last_error = NRF_SUCCESS;
    return INKTAG_SETTINGS_JOURNAL_READ_FOUND;
}

bool inktag_nrf_config_store_read(inktag_nrf_config_record_type_t type,
                                  void *data, uint16_t capacity,
                                  uint16_t *length) {
    return inktag_nrf_config_store_read_result(type, data, capacity, length) ==
        INKTAG_SETTINGS_JOURNAL_READ_FOUND;
}

bool inktag_nrf_config_store_read_part(inktag_nrf_config_record_type_t type,
    uint16_t offset, void *data, uint16_t length, uint16_t *record_length) {
    if (!s_initialized || !current_record_type_valid(type) || !data ||
        !length || !record_length) {
        s_last_error = NRF_ERROR_INVALID_PARAM;
        return false;
    }
    const config_record_index_t *record = &s_records[type];
    if (!record->present) {
        s_last_error = s_tail_clean ? NRF_ERROR_NOT_FOUND : NRF_ERROR_INVALID_DATA;
        return false;
    }
    if (offset > record->length || length > record->length - offset) {
        s_last_error = NRF_ERROR_DATA_SIZE;
        return false;
    }
    *record_length = record->length;
    return flash_read(record->address + offset, data, length);
}

bool inktag_nrf_config_store_read_matches(inktag_nrf_config_record_type_t type,
                                         const void *data,uint16_t length) {
#if INKTAG_NRF_STREAMING_CONFIG_STORE
    uint8_t *scratch=s_io_buffer;const uint16_t capacity=sizeof(s_io_buffer);
#else
    uint8_t *scratch=s_write_buffer.payload;const uint16_t capacity=sizeof(s_write_buffer.payload);
#endif
    uint16_t actual=0u;
    if(!data || !length || length>capacity)return false;
    /* Reuse the authoritative read-only scanner: it rereads page ordering,
     * record headers, complete checksums and commit words. Never verify only
     * through the optimistic index left by an acknowledged program operation. */
    s_initialized=false;
    bool ok=inktag_nrf_config_store_init() && s_tail_clean &&
        inktag_nrf_config_store_read_part(type,0u,scratch,length,&actual) &&
        actual==length && !memcmp(scratch,data,length);
    /* A lost/corrupt metadata write can leave payload bytes readable through
     * the optimistic RAM index. Do not acknowledge it or use that index again. */
    if(!ok){s_initialized=false;s_last_error=NRF_ERROR_INVALID_DATA;}
    return ok;
}

static bool begin_mutation(void) {
    if (!s_initialized) return false;
    if (!s_blank) return true;
    if (!write_page_header(CONFIG_STORE_START, 1u, true)) {
        s_initialized = false;
        return false;
    }
    s_blank = false;
    s_generation = 1u;
    return true;
}

bool inktag_nrf_config_store_write(inktag_nrf_config_record_type_t type,
                                   const void *data, uint16_t length) {
    uint32_t record_address;
    if (!s_initialized || !current_record_type_valid(type) ||
        data == NULL || length == 0u || length > CONFIG_MAX_PAYLOAD) {
        s_last_error = NRF_ERROR_INVALID_PARAM;
        return false;
    }
    if (!begin_mutation()) return false;
#if INKTAG_NRF_STREAMING_CONFIG_STORE
    if (!s_tail_clean || (uint32_t)s_write_offset + record_size(length) > CONFIG_STORE_PAGE_SIZE)
        return stream_compact_update(type, data, length);
#else
    if (!ensure_space(length)) return false;
#endif
    record_address = s_active_page + s_write_offset + sizeof(config_record_header_t);
    if (!append_to_page(s_active_page, &s_write_offset, type, data, length)) {
#if INKTAG_NRF_STREAMING_CONFIG_STORE
        s_initialized = false;
#endif
        return false;
    }
    s_records[type].address = record_address;
    s_records[type].length = length;
    s_records[type].present = true;
    s_last_error = NRF_SUCCESS;
    return true;
}

bool inktag_nrf_config_store_delete(inktag_nrf_config_record_type_t type) {
    if (!s_initialized || !current_record_type_valid(type)) {
        s_last_error = NRF_ERROR_INVALID_PARAM;
        return false;
    }
    if (!begin_mutation()) return false;
#if INKTAG_NRF_STREAMING_CONFIG_STORE
    if (!s_tail_clean || (uint32_t)s_write_offset + record_size(0u) > CONFIG_STORE_PAGE_SIZE)
        return stream_compact_update(type, NULL, 0u);
    if (
#else
    if (!ensure_space(0u) ||
#endif
        !append_to_page(s_active_page, &s_write_offset, type, NULL, 0u)) {
#if INKTAG_NRF_STREAMING_CONFIG_STORE
        s_initialized = false;
#endif
        return false;
    }
    memset(&s_records[type], 0, sizeof(s_records[type]));
    s_last_error = NRF_SUCCESS;
    return true;
}

uint32_t inktag_nrf_config_store_last_error(void) { return s_last_error; }

bool inktag_nrf_config_store_cleanup_display(uint32_t mask,
                                             const void *data, uint16_t length) {
#if INKTAG_NRF_STREAMING_CONFIG_STORE
    const uint32_t allowed = INKTAG_SETTINGS_RESET_SETTINGS_V2_MASK |
                             INKTAG_SETTINGS_CLEAR_IMAGES_MASK;
    if (!s_initialized || !data || !length || length > CONFIG_MAX_PAYLOAD ||
        !mask || (mask & ~allowed)) {
        s_last_error = NRF_ERROR_INVALID_PARAM;
        return false;
    }
    if (!begin_mutation()) return false;
    return stream_compact_change(mask, INKTAG_NRF_CONFIG_DISPLAY_STATE, data, length);
#else
    (void)mask; (void)data; (void)length;
    s_last_error = NRF_ERROR_NOT_SUPPORTED;
    return false;
#endif
}

bool inktag_nrf_config_store_clear_records(uint32_t mask) {
#if INKTAG_NRF_STREAMING_CONFIG_STORE
    const uint32_t allowed = INKTAG_SETTINGS_RESET_SETTINGS_MASK |
        INKTAG_SETTINGS_CLEAR_USER_DATA_MASK |
        INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_OWNER_RESET_PENDING);
    if (!s_initialized || !mask || (mask & ~allowed)) {
        s_last_error = NRF_ERROR_INVALID_PARAM;
        return false;
    }
    if (!begin_mutation()) return false;
    return stream_compact_change(mask, 0u, NULL, 0u);
#else
    (void)mask;
    s_last_error = NRF_ERROR_NOT_SUPPORTED;
    return false;
#endif
}

bool inktag_nrf_config_store_cleanup_user(uint32_t mask,
    const void *user, uint16_t user_length, const void *replay, uint16_t replay_length) {
#if INKTAG_NRF_STREAMING_CONFIG_STORE
    const uint32_t allowed = INKTAG_SETTINGS_RESET_SETTINGS_V2_MASK |
                             INKTAG_SETTINGS_CLEAR_IMAGES_MASK;
    if (!mask || (mask & ~allowed) || !user || !user_length ||
        !replay || !replay_length || user_length > CONFIG_MAX_PAYLOAD ||
        replay_length > CONFIG_MAX_PAYLOAD || !begin_mutation()) return false;
    return stream_compact_pair(mask, INKTAG_NRF_CONFIG_DEVICE_SETTINGS, user, user_length,
                               INKTAG_NRF_CONFIG_DISPLAY_STATE, replay, replay_length);
#else
    (void)mask; (void)user; (void)user_length; (void)replay; (void)replay_length;
    s_last_error = NRF_ERROR_NOT_SUPPORTED;
    return false;
#endif
}

bool inktag_nrf_pin_epoch_ready(void) {
    return inktag_nrf_pin_epoch_settings_ready(
        (const void *)(uintptr_t)INKTAG_BOARD_DFU_SETTINGS_START,92u);
}

bool inktag_nrf_boot_display_context(bool *first_display_requested,
                                     bool *ota_install_boot) {
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

#if defined(INKTAG_NRF_SDK17)
    /* This function runs only after BLE services and advertising are ready.
     * Confirm the installed candidate independently of optional display
     * records: missing/corrupt records or a failed checkpoint must not strand
     * a running App in the pending state. Recovery commits the epoch using
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

    saved_valid = inktag_nrf_config_store_read(
                      INKTAG_NRF_CONFIG_BOOT_DISPLAY, &saved,
                      sizeof(saved), &length) &&
                  length == sizeof(saved) && saved.magic == BOOT_DISPLAY_MAGIC &&
                  saved.version == BOOT_DISPLAY_VERSION && saved.reserved == 0u &&
                  (saved.flags & (uint8_t)~BOOT_DISPLAY_FIRST_REQUEST) == 0u;
    /* Only a host-seeded Full marker grants first-display authority. An
     * absent/corrupt record or inaccessible store is not provisioning. */
    if (!saved_valid) return false;

    memset(&next, 0, sizeof(next));
    next.magic = BOOT_DISPLAY_MAGIC;
    next.version = BOOT_DISPLAY_VERSION;
    next.app_size = settings[DFU_SETTINGS_BANK0_SIZE_OFFSET / sizeof(uint32_t)];
    next.app_crc = settings[DFU_SETTINGS_BANK0_CRC_OFFSET / sizeof(uint32_t)];

    *first_display_requested =
        (saved.flags & BOOT_DISPLAY_FIRST_REQUEST) != 0u;
    *ota_install_boot = saved_valid &&
        (saved.app_size != next.app_size || saved.app_crc != next.app_crc);

    /* Consume the Full marker and checkpoint a newly installed application
     * before any automatic display can be queued. If this power-gated write
     * fails, the caller suppresses automatic work and retries next boot. */
    if (*first_display_requested || *ota_install_boot) {
        if (!inktag_nrf_config_store_write(
                INKTAG_NRF_CONFIG_BOOT_DISPLAY, &next, sizeof(next))) {
            *first_display_requested = false;
            *ota_install_boot = false;
            return false;
        }
    }
    return true;
}
