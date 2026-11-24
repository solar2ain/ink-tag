/* DA storage API v2: only consumed settings operations. */
#include "inktag_settings_journal.h"

#include <stddef.h>
#include <string.h>

#define SETTINGS_PAGE_MAGIC 0x31505349u /* ISP1 */
#define SETTINGS_RECORD_MAGIC 0x31525349u /* ISR1 */
#define SETTINGS_RECORD_COMMIT 0x31435349u /* ISC1 */
#define SETTINGS_COPY_CHUNK 32u

typedef struct {
    uint32_t magic;
    uint32_t generation;
    uint32_t generation_inverse;
    uint32_t reserved;
} settings_page_header_t;

typedef struct {
    uint32_t magic;
    uint16_t type;
    uint16_t length;
    uint32_t checksum;
} settings_record_header_t;

typedef struct {
    uint32_t address;
    uint16_t length;
    bool present;
} settings_record_location_t;

typedef struct {
    uint32_t address;
    uint32_t checksum;
    uint16_t length;
} settings_record_index_t;

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(settings_page_header_t) == 16u,
               "settings page header size");
_Static_assert(sizeof(settings_record_header_t) == 12u,
               "settings record header size");
#else
typedef char settings_page_header_size_must_be_16[
    sizeof(settings_page_header_t) == 16u ? 1 : -1];
typedef char settings_record_header_size_must_be_12[
    sizeof(settings_record_header_t) == 12u ? 1 : -1];
#endif
static uint32_t page_address(const inktag_settings_journal_t *store,
                             uint8_t page) {
    return store->io.start_address +
           (uint32_t)page * INKTAG_SETTINGS_JOURNAL_PAGE_SIZE;
}

static bool io_valid(const inktag_settings_journal_io_t *io) {
    return io != NULL && io->read != NULL && io->write != NULL &&
           io->erase != NULL && io->allowed_records != 0u &&
           (io->start_address &
            (INKTAG_SETTINGS_JOURNAL_PAGE_SIZE - 1u)) == 0u;
}

static uint32_t checksum_update(uint32_t checksum, const void *data,
                                uint16_t length) {
    const uint8_t *bytes = (const uint8_t *)data;
    while (length-- > 0u) {
        checksum ^= *bytes++;
        checksum *= 16777619u;
    }
    return checksum;
}

static uint32_t checksum_begin(uint16_t type, uint16_t length) {
    uint32_t checksum = 2166136261u;
    checksum = checksum_update(checksum, &type, sizeof(type));
    return checksum_update(checksum, &length, sizeof(length));
}

static uint16_t record_size(uint16_t length) {
    return (uint16_t)(sizeof(settings_record_header_t) +
                      ((length + 3u) & ~3u) + sizeof(uint32_t));
}

static bool record_allowed(const inktag_settings_journal_t *store,
                           uint16_t record_id) {
    return record_id >= INKTAG_SETTINGS_RECORD_BOARD_PROFILE &&
           record_id <= INKTAG_SETTINGS_RECORD_MAX &&
           (store->io.allowed_records &
            INKTAG_SETTINGS_RECORD_BIT(record_id)) != 0u;
}

static bool record_type_valid(uint16_t record_id, uint16_t length) {
    return record_id >= INKTAG_SETTINGS_RECORD_BOARD_PROFILE &&
           record_id <= INKTAG_SETTINGS_RECORD_MAX &&
           length <= INKTAG_SETTINGS_JOURNAL_MAX_PAYLOAD;
}

typedef enum {
    SETTINGS_RANGE_IO_ERROR,
    SETTINGS_RANGE_DIRTY,
    SETTINGS_RANGE_ERASED,
} settings_range_result_t;

static settings_range_result_t range_erased(
    inktag_settings_journal_t *store, uint32_t address, uint16_t length) {
    uint8_t bytes[SETTINGS_COPY_CHUNK];
    while (length != 0u) {
        uint16_t chunk = length > sizeof(bytes) ? sizeof(bytes) : length;
        uint16_t i;
        if (!store->io.read(store->io.context, address, bytes, chunk))
            return SETTINGS_RANGE_IO_ERROR;
        for (i = 0u; i < chunk; ++i)
            if (bytes[i] != 0xFFu) return SETTINGS_RANGE_DIRTY;
        address += chunk;
        length = (uint16_t)(length - chunk);
    }
    return SETTINGS_RANGE_ERASED;
}

static bool payload_checksum(inktag_settings_journal_t *store,
                             uint16_t type, uint16_t length,
                             uint32_t address, uint32_t *checksum_out) {
    uint8_t bytes[SETTINGS_COPY_CHUNK];
    uint16_t remaining = length;
    uint32_t checksum = checksum_begin(type, length);
    while (remaining != 0u) {
        uint16_t chunk = remaining > sizeof(bytes) ? sizeof(bytes) : remaining;
        if (!store->io.read(store->io.context, address, bytes, chunk))
            return false;
        checksum = checksum_update(checksum, bytes, chunk);
        address += chunk;
        remaining = (uint16_t)(remaining - chunk);
    }
    *checksum_out = checksum;
    return true;
}

static bool committed_page_header_valid(const settings_page_header_t *header) {
    return header->magic == SETTINGS_PAGE_MAGIC &&
           header->generation_inverse == ~header->generation &&
           header->reserved == 0xFFFFFFFFu;
}

static bool page_header_valid(inktag_settings_journal_t *store, uint8_t page,
                              settings_page_header_t *header) {
    return store->io.read(store->io.context, page_address(store, page),
                          header, sizeof(*header)) &&
           committed_page_header_valid(header);
}

static bool generation_newer(uint32_t candidate, uint32_t current) {
    return (int32_t)(candidate - current) > 0;
}

static bool scan_page_indexed(inktag_settings_journal_t *store, uint8_t page,
                      uint16_t wanted_type,
                      settings_record_location_t *location,
                      uint16_t *write_offset, bool *tail_clean,
                      settings_record_index_t *latest_records) {
    uint32_t base = page_address(store, page);
    uint16_t offset = sizeof(settings_page_header_t);
    settings_record_header_t header;
    settings_record_location_t found = {0u, 0u, false};
    bool clean = true;
    if (latest_records != NULL)
        memset(latest_records, 0,
               sizeof(*latest_records) * INKTAG_SETTINGS_RECORD_MAX);

    while ((uint32_t)offset + sizeof(header) + sizeof(uint32_t) <=
           INKTAG_SETTINGS_JOURNAL_PAGE_SIZE) {
        uint16_t padded;
        uint32_t commit;
        uint32_t checksum;
        if (!store->io.read(store->io.context, base + offset,
                            &header, sizeof(header))) return false;
        if (header.magic == 0xFFFFFFFFu) {
            settings_range_result_t result = range_erased(
                store, base + offset,
                (uint16_t)(INKTAG_SETTINGS_JOURNAL_PAGE_SIZE - offset));
            if (result == SETTINGS_RANGE_IO_ERROR) return false;
            clean = result == SETTINGS_RANGE_ERASED;
            break;
        }
        if (header.magic != SETTINGS_RECORD_MAGIC ||
            !record_type_valid(header.type, header.length)) {
            clean = false;
            break;
        }
        padded = (uint16_t)((header.length + 3u) & ~3u);
        if ((uint32_t)offset + sizeof(header) + padded + sizeof(commit) >
            INKTAG_SETTINGS_JOURNAL_PAGE_SIZE) {
            clean = false;
            break;
        }
        if (!store->io.read(store->io.context,
                            base + offset + sizeof(header) + padded,
                            &commit, sizeof(commit)) ||
            !payload_checksum(store, header.type, header.length,
                              base + offset + sizeof(header), &checksum))
            return false;
        if (commit != SETTINGS_RECORD_COMMIT || checksum != header.checksum) {
            clean = false;
            break;
        }
        if (header.type == wanted_type) {
            found.address = base + offset + sizeof(header);
            found.length = header.length;
            found.present = header.length != 0u;
        }
        if (latest_records != NULL) {
            settings_record_index_t *entry = &latest_records[header.type - 1u];
            entry->address = base + offset + sizeof(header);
            entry->length = header.length;
            entry->checksum = checksum;
        }
        offset = (uint16_t)(offset + record_size(header.length));
    }
    if (location != NULL) *location = found;
    if (write_offset != NULL) *write_offset = offset;
    if (tail_clean != NULL) *tail_clean = clean;
    return true;
}

static bool scan_page(inktag_settings_journal_t *store, uint8_t page,
                      uint16_t wanted_type,
                      settings_record_location_t *location,
                      uint16_t *write_offset, bool *tail_clean) {
    return scan_page_indexed(store, page, wanted_type, location,
                              write_offset, tail_clean, NULL);
}

static bool write_page_header(inktag_settings_journal_t *store, uint8_t page,
                              uint32_t generation, bool commit) {
    settings_page_header_t header;
    uint32_t address = page_address(store, page);
    memset(&header, 0xFF, sizeof(header));
    header.generation = generation;
    header.generation_inverse = ~generation;
    if (!commit) {
        return store->io.write(
            store->io.context, address + sizeof(header.magic),
            &header.generation, sizeof(header) - sizeof(header.magic));
    }
    header.magic = SETTINGS_PAGE_MAGIC;
    return store->io.write(store->io.context, address, &header.magic,
                           sizeof(header.magic));
}

/* RAM append and compaction copy share the same bounded record writer. A
 * Flash source is revalidated before the first mutation, exactly as before;
 * header, padded 32-byte chunks and commit retain their original IO order. */
static bool __attribute__((noinline)) append_record(
    inktag_settings_journal_t *store, uint8_t page,
    uint16_t *offset, uint16_t type, const void *data, uint16_t length,
    const settings_record_index_t *source) {
    settings_record_header_t header;
    uint8_t bytes[SETTINGS_COPY_CHUNK];
    uint32_t commit = SETTINGS_RECORD_COMMIT;
    uint32_t source_address = source != NULL ? source->address : 0u;
    uint32_t address = page_address(store, page) + *offset;
    uint16_t body = (uint16_t)(sizeof(header) + ((length + 3u) & ~3u));
    uint16_t remaining = length;
    const uint8_t *memory = (const uint8_t *)data;

    if (source != NULL) {
        if (!payload_checksum(store, type, length, source_address,
                              &header.checksum) ||
            header.checksum != source->checksum) return false;
    } else {
        if (!record_type_valid(type, length) ||
            (length != 0u && data == NULL)) return false;
        header.checksum = checksum_update(checksum_begin(type, length),
                                          data, length);
    }
    if ((uint32_t)*offset + body + sizeof(commit) >
        INKTAG_SETTINGS_JOURNAL_PAGE_SIZE) return false;
    header.magic = SETTINGS_RECORD_MAGIC;
    header.type = type;
    header.length = length;
    if (!store->io.write(store->io.context, address, &header, sizeof(header)))
        return false;
    address += sizeof(header);
    while (remaining != 0u) {
        uint16_t payload = remaining > sizeof(bytes) ? sizeof(bytes) : remaining;
        uint16_t programmed = (uint16_t)((payload + 3u) & ~3u);
        memset(bytes, 0xFF, programmed);
        if (source != NULL) {
            if (!store->io.read(store->io.context, source_address, bytes, payload))
                return false;
            source_address += payload;
        } else {
            memcpy(bytes, memory, payload);
            memory += payload;
        }
        if (!store->io.write(store->io.context, address, bytes, programmed))
            return false;
        address += programmed;
        remaining = (uint16_t)(remaining - payload);
    }
    if (!store->io.write(store->io.context, address, &commit, sizeof(commit)))
        return false;
    *offset = (uint16_t)(*offset + body + sizeof(commit));
    return true;
}

static bool append_memory(inktag_settings_journal_t *store, uint8_t page,
                          uint16_t *offset, uint16_t type,
                          const void *data, uint16_t length) {
    return append_record(store, page, offset, type, data, length, NULL);
}

static bool append_copy(inktag_settings_journal_t *store, uint8_t target_page,
                        uint16_t *target_offset, uint16_t type,
                        const settings_record_index_t *source) {
    return append_record(store, target_page, target_offset, type,
                          NULL, source->length, source);
}

static bool compact_store_mask_or_replace(inktag_settings_journal_t *store,
                                          uint32_t clear_mask,
                                          uint16_t replacement_id,
                                          const void *replacement,
                                          uint16_t replacement_length) {
    uint8_t old_page = store->active_page;
    uint8_t target_page = (uint8_t)(1u - old_page);
    uint32_t generation = store->generation + 1u;
    uint16_t target_offset = sizeof(settings_page_header_t);
    uint16_t checked_offset;
    bool checked_clean;
    uint16_t type;
    settings_record_index_t latest_records[INKTAG_SETTINGS_RECORD_MAX];
    /* Validate the old committed prefix once and keep a bounded local index.
     * Revalidating the whole page for each of 32 IDs
     * stalls SDK6 message servicing long enough to exhaust its BLE heap.
     * Keep every stable ID, including IDs outside this firmware's write
     * allowlist; zero length still removes the preceding logical value.
     * No scheduler callback or nested Flash access is introduced here. */
    if (!scan_page_indexed(store, old_page, 0u, NULL, NULL, NULL,
                           latest_records) ||
        !store->io.erase(store->io.context, page_address(store, target_page)) ||
        !write_page_header(store, target_page, generation, false)) return false;
    for (type = 1u; type <= INKTAG_SETTINGS_RECORD_MAX; ++type) {
        const settings_record_index_t *source = &latest_records[type - 1u];
        /* Scan/copy every bounded record type, not only the capabilities this
         * firmware can write. A temporary downgrade must not silently delete
         * a newer record. Lifecycle deletion remains owned by the common
         * registry and therefore never follows a board-local mask. */
        if (type == replacement_id) {
            if (!append_memory(store, target_page, &target_offset, type,
                               replacement, replacement_length)) return false;
        } else if (source->length != 0u &&
                   (clear_mask & INKTAG_SETTINGS_RECORD_BIT(type)) == 0u &&
                   !append_copy(store, target_page, &target_offset, type,
                                source)) return false;
    }
    /* Validate the staged generation before writing the page magic: until
     * that final word, a power loss still selects the complete old page. */
    if (!scan_page(store, target_page, 0u, NULL,
                   &checked_offset, &checked_clean) || !checked_clean ||
        checked_offset != target_offset ||
        !write_page_header(store, target_page, generation, true)) return false;

    store->active_page = target_page;
    store->generation = generation;
    store->write_offset = target_offset;
    store->tail_clean = true;
    /* The complete new generation is authoritative before this best-effort
     * erase. If voltage falls here, the older valid generation is harmless
     * and is retried as the next compaction target. */
    if (replacement_id != 0u)
        return store->io.erase(store->io.context,
                               page_address(store, old_page));
    (void)store->io.erase(store->io.context, page_address(store, old_page));
    return true;
}

static bool compact_store_mask(inktag_settings_journal_t *store,
                               uint32_t clear_mask) {
    return compact_store_mask_or_replace(store, clear_mask, 0u, NULL, 0u);
}




bool inktag_settings_journal_put_record_atomic(
    inktag_settings_journal_t *store, uint16_t record_id,
    const void *data, uint16_t length) {
    bool clean;
    settings_page_header_t header;
    if (store == NULL || !store->initialized || !store->tail_clean ||
        !record_allowed(store, record_id) || data == NULL || length == 0u ||
        length > INKTAG_SETTINGS_JOURNAL_MAX_PAYLOAD ||
        !page_header_valid(store, store->active_page, &header) ||
        header.generation != store->generation ||
        !scan_page(store, store->active_page, 0u, NULL, NULL, &clean) ||
        !clean) return false;
    /* Unlike an append, a torn first insertion leaves a clean old generation
     * proving absence. Existing replace callers still require a prior record. */
    return compact_store_mask_or_replace(store, 0u, record_id, data, length);
}

static bool compact_store(inktag_settings_journal_t *store,
                          inktag_settings_clear_action_t clear_action) {
    uint32_t clear_mask = clear_action == INKTAG_SETTINGS_CLEAR_RESET_SETTINGS ?
        INKTAG_SETTINGS_RESET_SETTINGS_MASK :
        clear_action == INKTAG_SETTINGS_CLEAR_USER_DATA ?
            INKTAG_SETTINGS_CLEAR_USER_DATA_MASK :
        clear_action == INKTAG_SETTINGS_CLEAR_FACTORY_RESET ?
            INKTAG_SETTINGS_FACTORY_RESET_MASK :
        clear_action == INKTAG_SETTINGS_CLEAR_OWNER_FACTORY_RESET ?
            INKTAG_SETTINGS_OWNER_FACTORY_RESET_MASK : 0u;
    return compact_store_mask(store, clear_mask);
}

static bool ensure_space(inktag_settings_journal_t *store, uint16_t length) {
    uint16_t required = record_size(length);
    if (store->tail_clean &&
        (uint32_t)store->write_offset + required <=
            INKTAG_SETTINGS_JOURNAL_PAGE_SIZE) return true;
    if (!compact_store(store, (inktag_settings_clear_action_t)0)) return false;
    return (uint32_t)store->write_offset + required <=
           INKTAG_SETTINGS_JOURNAL_PAGE_SIZE;
}

bool inktag_settings_journal_init(
    inktag_settings_journal_t *store,
    const inktag_settings_journal_io_t *io,
    bool create_if_empty) {
    settings_page_header_t first;
    settings_page_header_t second;
    bool first_valid;
    bool second_valid;
    bool tail_clean;

    if (store == NULL || !io_valid(io)) return false;
    if (store->initialized) return true;
    memset(store, 0, sizeof(*store));
    store->io = *io;
    /* Unreadable is not invalid/absent. Selecting the other page could roll
     * back passwords, entitlement, image indexes or a reset tombstone; with
     * create_if_empty it could also erase state we never proved empty. */
    if (!store->io.read(store->io.context, page_address(store, 0u),
                        &first, sizeof(first)) ||
        !store->io.read(store->io.context, page_address(store, 1u),
                        &second, sizeof(second))) return false;
    first_valid = committed_page_header_valid(&first);
    second_valid = committed_page_header_valid(&second);
    if (!first_valid && !second_valid) {
        if (!create_if_empty ||
            !store->io.erase(store->io.context, page_address(store, 0u)) ||
            !write_page_header(store, 0u, 1u, false) ||
            !write_page_header(store, 0u, 1u, true)) return false;
        store->active_page = 0u;
        store->generation = 1u;
    } else if (!second_valid ||
               (first_valid &&
                !generation_newer(second.generation, first.generation))) {
        store->active_page = 0u;
        store->generation = first.generation;
    } else {
        store->active_page = 1u;
        store->generation = second.generation;
    }
    if (!scan_page(store, store->active_page, 0u, NULL,
                   &store->write_offset, &tail_clean)) return false;
    store->tail_clean = tail_clean;
    store->initialized = true;
    return true;
}

inktag_settings_journal_read_result_t inktag_settings_journal_read_result(
    inktag_settings_journal_t *store, uint16_t record_id,
    void *data, uint16_t capacity, uint16_t *length) {
    settings_record_location_t record;
    bool clean;
    settings_page_header_t header;
    if (store == NULL || !store->initialized || data == NULL || length == NULL ||
        !record_allowed(store, record_id) ||
        !page_header_valid(store, store->active_page, &header) ||
        header.generation != store->generation ||
        !scan_page(store, store->active_page, record_id,
                   &record, NULL, &clean)) return INKTAG_SETTINGS_JOURNAL_READ_ERROR;
    if (!record.present) return clean ?
        INKTAG_SETTINGS_JOURNAL_READ_MISSING : INKTAG_SETTINGS_JOURNAL_READ_ERROR;
    if (record.length > capacity) return INKTAG_SETTINGS_JOURNAL_READ_ERROR;
    if (!store->io.read(store->io.context, record.address,
                        data, record.length)) return INKTAG_SETTINGS_JOURNAL_READ_ERROR;
    *length = record.length;
    return INKTAG_SETTINGS_JOURNAL_READ_FOUND;
}

bool inktag_settings_journal_read(
    inktag_settings_journal_t *store, uint16_t record_id,
    void *data, uint16_t capacity, uint16_t *length) {
    return inktag_settings_journal_read_result(
        store, record_id, data, capacity, length) ==
        INKTAG_SETTINGS_JOURNAL_READ_FOUND;
}

bool inktag_settings_journal_write(
    inktag_settings_journal_t *store, uint16_t record_id,
    const void *data, uint16_t length) {
    if (store == NULL || !store->initialized || data == NULL || length == 0u ||
        !record_allowed(store, record_id) ||
        length > INKTAG_SETTINGS_JOURNAL_MAX_PAYLOAD) return false;
    if (!ensure_space(store, length) ||
        !append_memory(store, store->active_page, &store->write_offset,
                       record_id, data, length)) {
        store->tail_clean = false;
        return false;
    }
    return true;
}

bool inktag_settings_journal_delete(
    inktag_settings_journal_t *store, uint16_t record_id) {
    if (store == NULL || !store->initialized ||
        !record_allowed(store, record_id)) return false;
    if (!ensure_space(store, 0u) ||
        !append_memory(store, store->active_page, &store->write_offset,
                       record_id, NULL, 0u)) {
        store->tail_clean = false;
        return false;
    }
    return true;
}









static bool clear_scope_v2(inktag_settings_journal_t *store,
                           uint32_t mask) {
    settings_page_header_t header;
    if (store == NULL || !store->initialized || !store->tail_clean ||
        !page_header_valid(store, store->active_page, &header) ||
        header.generation != store->generation) return false;
    return compact_store_mask(store, mask);
}

bool inktag_settings_journal_clear_images(
    inktag_settings_journal_t *store) {
    return clear_scope_v2(store, INKTAG_SETTINGS_CLEAR_IMAGES_MASK);
}

bool inktag_settings_journal_clear_ordinary_settings_v2(
    inktag_settings_journal_t *store) {
    return clear_scope_v2(store, INKTAG_SETTINGS_RESET_ORDINARY_V2_MASK);
}
