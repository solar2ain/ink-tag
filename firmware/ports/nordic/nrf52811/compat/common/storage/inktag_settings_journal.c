#include "inktag_settings_journal.h"

#include <stddef.h>
#include <string.h>

#define SETTINGS_PAGE_MAGIC 0x31505349u /* ISP1 */
#define SETTINGS_RECORD_MAGIC 0x31525349u /* ISR1 */
#define SETTINGS_RECORD_COMMIT 0x31435349u /* ISC1 */
#define SETTINGS_COPY_CHUNK 32u

/* These private headers are copied into local objects, never dereferenced
 * directly from arbitrary Flash addresses. All members are naturally aligned
 * in the existing 16/12-byte format. Retain that guarantee even when a vendor
 * builds with -fpack-struct; otherwise TC32 emits bytewise word reconstruction.
 * This changes neither the on-media format nor any public journal object. */
typedef struct __attribute__((aligned(4))) {
    uint32_t magic;
    uint32_t generation;
    uint32_t generation_inverse;
    uint32_t reserved;
} settings_page_header_t;

typedef struct __attribute__((aligned(4))) {
    uint32_t magic;
    uint16_t type;
    uint16_t length;
    uint32_t checksum;
} settings_record_header_t;

typedef struct __attribute__((aligned(4))) {
    uint32_t address;
    uint16_t length;
    bool present;
} settings_record_location_t;

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

static bool staged_page_header_valid(
    inktag_settings_journal_t *store, uint8_t page,
    settings_page_header_t *header) {
    return store->io.read(store->io.context, page_address(store, page),
                          header, sizeof(*header)) &&
           header->magic == 0xFFFFFFFFu &&
           header->generation_inverse == ~header->generation &&
           header->reserved == 0xFFFFFFFFu;
}

static bool generation_newer(uint32_t candidate, uint32_t current) {
    return (int32_t)(candidate - current) > 0;
}

static bool scan_page(inktag_settings_journal_t *store, uint8_t page,
                      uint16_t wanted_type,
                      settings_record_location_t *location,
                      uint16_t *write_offset, bool *tail_clean) {
    uint32_t base = page_address(store, page);
    uint16_t offset = sizeof(settings_page_header_t);
    settings_record_header_t header;
    settings_record_location_t found = {0u, 0u, false};
    bool clean = true;

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
        offset = (uint16_t)(offset + record_size(header.length));
    }
    if (location != NULL) *location = found;
    if (write_offset != NULL) *write_offset = offset;
    if (tail_clean != NULL) *tail_clean = clean;
    return true;
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

static bool write_padded_payload(inktag_settings_journal_t *store,
                                 uint32_t address, const uint8_t *data,
                                 uint16_t length) {
    uint8_t bytes[SETTINGS_COPY_CHUNK];
    uint16_t remaining = length;
    while (remaining != 0u) {
        uint16_t payload = remaining > sizeof(bytes) ? sizeof(bytes) : remaining;
        uint16_t programmed = (uint16_t)((payload + 3u) & ~3u);
        memset(bytes, 0xFF, programmed);
        memcpy(bytes, data, payload);
        if (!store->io.write(store->io.context, address, bytes, programmed))
            return false;
        address += programmed;
        data += payload;
        remaining = (uint16_t)(remaining - payload);
    }
    return true;
}

static bool append_memory(inktag_settings_journal_t *store, uint8_t page,
                          uint16_t *offset, uint16_t type,
                          const void *data, uint16_t length) {
    settings_record_header_t header;
    uint32_t commit = SETTINGS_RECORD_COMMIT;
    uint32_t address = page_address(store, page) + *offset;
    uint16_t body = (uint16_t)(sizeof(header) + ((length + 3u) & ~3u));

    if (!record_type_valid(type, length) ||
        (length != 0u && data == NULL) ||
        (uint32_t)*offset + body + sizeof(commit) >
            INKTAG_SETTINGS_JOURNAL_PAGE_SIZE) return false;
    header.magic = SETTINGS_RECORD_MAGIC;
    header.type = type;
    header.length = length;
    header.checksum = checksum_update(checksum_begin(type, length), data, length);
    if (!store->io.write(store->io.context, address, &header, sizeof(header)) ||
        (length != 0u && !write_padded_payload(
            store, address + sizeof(header), (const uint8_t *)data, length)) ||
        !store->io.write(store->io.context, address + body,
                         &commit, sizeof(commit))) return false;
    *offset = (uint16_t)(*offset + body + sizeof(commit));
    return true;
}

static bool __attribute__((noinline)) append_copy(
    inktag_settings_journal_t *store, uint8_t target_page,
    uint16_t *target_offset, uint16_t type,
    const settings_record_location_t *source) {
    settings_record_header_t header;
    uint8_t bytes[SETTINGS_COPY_CHUNK];
    uint32_t checksum;
    uint32_t source_address = source->address;
    uint32_t target_address = page_address(store, target_page) + *target_offset;
    uint16_t remaining = source->length;
    uint16_t body = (uint16_t)(sizeof(header) +
                               ((source->length + 3u) & ~3u));
    uint32_t commit = SETTINGS_RECORD_COMMIT;

    if (!payload_checksum(store, type, source->length,
                          source_address, &checksum) ||
        (uint32_t)*target_offset + body + sizeof(commit) >
            INKTAG_SETTINGS_JOURNAL_PAGE_SIZE) return false;
    header.magic = SETTINGS_RECORD_MAGIC;
    header.type = type;
    header.length = source->length;
    header.checksum = checksum;
    if (!store->io.write(store->io.context, target_address,
                         &header, sizeof(header))) return false;
    target_address += sizeof(header);
    while (remaining != 0u) {
        uint16_t payload = remaining > sizeof(bytes) ? sizeof(bytes) : remaining;
        uint16_t programmed = (uint16_t)((payload + 3u) & ~3u);
        memset(bytes, 0xFF, programmed);
        if (!store->io.read(store->io.context, source_address, bytes, payload) ||
            !store->io.write(store->io.context, target_address,
                             bytes, programmed)) return false;
        source_address += payload;
        target_address += programmed;
        remaining = (uint16_t)(remaining - payload);
    }
    if (!store->io.write(store->io.context,
                         page_address(store, target_page) + *target_offset + body,
                         &commit, sizeof(commit))) return false;
    *target_offset = (uint16_t)(*target_offset + body + sizeof(commit));
    return true;
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
    if (!store->io.erase(store->io.context, page_address(store, target_page)) ||
        !write_page_header(store, target_page, generation, false)) return false;
    for (type = 1u; type <= INKTAG_SETTINGS_RECORD_MAX; ++type) {
        settings_record_location_t source;
        if (!scan_page(store, old_page, type, &source, NULL, NULL)) return false;
        /* Scan/copy every bounded record type, not only the capabilities this
         * firmware can write. A temporary downgrade must not silently delete
         * a newer record. Lifecycle deletion remains owned by the common
         * registry and therefore never follows a board-local mask. */
        if (type == replacement_id) {
            if (!append_memory(store, target_page, &target_offset, type,
                               replacement, replacement_length)) return false;
        } else if (source.present &&
                   (clear_mask & INKTAG_SETTINGS_RECORD_BIT(type)) == 0u &&
                   !append_copy(store, target_page, &target_offset, type,
                                &source)) return false;
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

bool inktag_settings_journal_inactive_erased(
    inktag_settings_journal_t *store) {
    return store != NULL && store->initialized &&
        range_erased(store, page_address(store,
            (uint8_t)(1u - store->active_page)),
            INKTAG_SETTINGS_JOURNAL_PAGE_SIZE) == SETTINGS_RANGE_ERASED;
}

bool inktag_settings_journal_scrub_inactive(
    inktag_settings_journal_t *store) {
    uint32_t address;
    if (store == NULL || !store->initialized) return false;
    if (inktag_settings_journal_inactive_erased(store)) return true;
    address = page_address(store, (uint8_t)(1u - store->active_page));
    return store->io.erase(store->io.context, address) &&
        inktag_settings_journal_inactive_erased(store);
}

bool inktag_settings_journal_replace_record_atomic(
    inktag_settings_journal_t *store, uint16_t record_id,
    const void *data, uint16_t length) {
    settings_record_location_t old_record;
    if (store == NULL || !store->initialized || !store->tail_clean ||
        !record_allowed(store, record_id) || data == NULL || length == 0u ||
        length > INKTAG_SETTINGS_JOURNAL_MAX_PAYLOAD ||
        !scan_page(store, store->active_page, record_id, &old_record,
                   NULL, NULL) || !old_record.present) return false;
    return compact_store_mask_or_replace(store, 0u, record_id,
                                         data, length);
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
            /* A foreign/corrupt development namespace is not an empty store.
             * Only an explicit authorized reset may erase that content. */
            range_erased(store, page_address(store, 0u),
                2u * INKTAG_SETTINGS_JOURNAL_PAGE_SIZE) != SETTINGS_RANGE_ERASED ||
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

inktag_settings_journal_read_result_t inktag_settings_journal_readonly_result(
    const inktag_settings_journal_io_t *io, uint16_t record_id,
    void *data, uint16_t capacity, uint16_t *length) {
    inktag_settings_journal_t store;
    settings_page_header_t first, second;
    settings_record_location_t record;
    uint16_t offset;
    bool first_valid, second_valid, clean;
    if (!io || !io->read || !data || !length || !io->allowed_records ||
        (io->start_address & (INKTAG_SETTINGS_JOURNAL_PAGE_SIZE - 1u)) != 0u ||
        io->start_address > UINT32_MAX - INKTAG_SETTINGS_JOURNAL_SIZE)
        return INKTAG_SETTINGS_JOURNAL_READ_ERROR;
    /* scan_page/record_allowed use only io and active_page. No init flags or
     * mutable state are observed by this strictly read-only entry. */
    store.io = *io;
    if (!record_allowed(&store, record_id) ||
        !io->read(io->context, page_address(&store, 0u), &first, sizeof(first)) ||
        !io->read(io->context, page_address(&store, 1u), &second, sizeof(second)))
        return INKTAG_SETTINGS_JOURNAL_READ_ERROR;
    first_valid = committed_page_header_valid(&first);
    second_valid = committed_page_header_valid(&second);
    if ((!first_valid && !second_valid) || (first_valid && second_valid &&
        (first.generation == second.generation ||
         second.generation - first.generation == UINT32_C(0x80000000))))
        return INKTAG_SETTINGS_JOURNAL_READ_ERROR;
    store.active_page = second_valid && (!first_valid ||
        generation_newer(second.generation, first.generation)) ? 1u : 0u;
    if (!scan_page(&store, store.active_page, record_id, &record, &offset, &clean) ||
        !clean) return INKTAG_SETTINGS_JOURNAL_READ_ERROR;
    /* Fixed common candidate 05d885f: scan_page stops when fewer than 16
     * bytes remain. Check only those 4/8/12 bytes, not a second long scan. */
    if (offset > INKTAG_SETTINGS_JOURNAL_PAGE_SIZE - 16u &&
        offset < INKTAG_SETTINGS_JOURNAL_PAGE_SIZE && range_erased(&store,
        page_address(&store, store.active_page) + offset,
        (uint16_t)(INKTAG_SETTINGS_JOURNAL_PAGE_SIZE - offset)) !=
        SETTINGS_RANGE_ERASED) return INKTAG_SETTINGS_JOURNAL_READ_ERROR;
    if (!record.present) return INKTAG_SETTINGS_JOURNAL_READ_MISSING;
    if (record.length > capacity || !io->read(io->context, record.address,
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

bool inktag_settings_journal_replace_all(
    inktag_settings_journal_t *store,
    const inktag_settings_journal_record_t *records,
    uint16_t record_count) {
    inktag_settings_journal_stage_t stage;
    uint16_t target_offset = sizeof(settings_page_header_t);
    uint16_t index;

    if (store == NULL || !store->initialized ||
        (record_count != 0u && records == NULL)) return false;
    for (index = 0u; index < record_count; ++index) {
        uint16_t prior;
        if (!record_allowed(store, records[index].record_id) ||
            records[index].data == NULL || records[index].length == 0u ||
            records[index].length > INKTAG_SETTINGS_JOURNAL_MAX_PAYLOAD ||
            (uint32_t)target_offset + record_size(records[index].length) >
                INKTAG_SETTINGS_JOURNAL_PAGE_SIZE) return false;
        for (prior = 0u; prior < index; ++prior) {
            if (records[prior].record_id == records[index].record_id)
                return false;
        }
        target_offset = (uint16_t)(target_offset +
                                    record_size(records[index].length));
    }

    if (!inktag_settings_journal_stage_begin(store, &stage)) return false;
    for (index = 0u; index < record_count; ++index) {
        if (!inktag_settings_journal_stage_write(
                &stage, records[index].record_id, records[index].data,
                records[index].length)) return false;
    }
    return inktag_settings_journal_stage_commit(&stage);
}

bool inktag_settings_journal_stage_open(
    inktag_settings_journal_t *store,
    inktag_settings_journal_stage_t *stage) {
    settings_page_header_t header;
    uint8_t page;
    bool tail_clean;
    if (store == NULL || !store->initialized || stage == NULL) return false;
    memset(stage, 0, sizeof(*stage));
    page = (uint8_t)(1u - store->active_page);
    if (!staged_page_header_valid(store, page, &header) ||
        !generation_newer(header.generation, store->generation) ||
        !scan_page(store, page, 0u, NULL,
                   &stage->write_offset, &tail_clean) || !tail_clean) {
        return false;
    }
    stage->store = store;
    stage->generation = header.generation;
    stage->page = page;
    stage->initialized = true;
    stage->tail_clean = true;
    return true;
}

bool inktag_settings_journal_stage_begin(
    inktag_settings_journal_t *store,
    inktag_settings_journal_stage_t *stage) {
    uint8_t page;
    uint32_t generation;
    if (store == NULL || !store->initialized || stage == NULL) return false;
    page = (uint8_t)(1u - store->active_page);
    generation = store->generation + 1u;
    memset(stage, 0, sizeof(*stage));
    if (!store->io.erase(store->io.context, page_address(store, page)) ||
        !write_page_header(store, page, generation, false)) return false;
    stage->store = store;
    stage->generation = generation;
    stage->write_offset = sizeof(settings_page_header_t);
    stage->page = page;
    stage->initialized = true;
    stage->tail_clean = true;
    return true;
}

bool inktag_settings_journal_stage_read(
    inktag_settings_journal_stage_t *stage, uint16_t record_id,
    void *data, uint16_t capacity, uint16_t *length) {
    settings_record_location_t record;
    if (stage == NULL || !stage->initialized || !stage->tail_clean ||
        stage->store == NULL || data == NULL || length == NULL ||
        !record_allowed(stage->store, record_id) ||
        !scan_page(stage->store, stage->page, record_id,
                   &record, NULL, NULL) || !record.present ||
        record.length > capacity) return false;
    if (!stage->store->io.read(stage->store->io.context, record.address,
                               data, record.length)) return false;
    *length = record.length;
    return true;
}

bool inktag_settings_journal_stage_write(
    inktag_settings_journal_stage_t *stage, uint16_t record_id,
    const void *data, uint16_t length) {
    if (stage == NULL || !stage->initialized || !stage->tail_clean ||
        stage->store == NULL || data == NULL || length == 0u ||
        !record_allowed(stage->store, record_id) ||
        length > INKTAG_SETTINGS_JOURNAL_MAX_PAYLOAD ||
        !append_memory(stage->store, stage->page, &stage->write_offset,
                       record_id, data, length)) {
        if (stage != NULL) stage->tail_clean = false;
        return false;
    }
    return true;
}

bool inktag_settings_journal_stage_commit(
    inktag_settings_journal_stage_t *stage) {
    inktag_settings_journal_t *store;
    uint16_t checked_offset;
    bool checked_clean;
    uint8_t old_page;
    if (stage == NULL || !stage->initialized || !stage->tail_clean ||
        stage->store == NULL) return false;
    store = stage->store;
    old_page = store->active_page;
    if (stage->page != (uint8_t)(1u - old_page) ||
        !generation_newer(stage->generation, store->generation) ||
        !scan_page(store, stage->page, 0u, NULL,
                   &checked_offset, &checked_clean) || !checked_clean ||
        checked_offset != stage->write_offset ||
        !write_page_header(store, stage->page,
                           stage->generation, true)) return false;
    store->active_page = stage->page;
    store->generation = stage->generation;
    store->write_offset = stage->write_offset;
    store->tail_clean = true;
    stage->initialized = false;
    (void)store->io.erase(store->io.context, page_address(store, old_page));
    return true;
}

bool inktag_settings_journal_stage_abort(
    inktag_settings_journal_stage_t *stage) {
    if (stage == NULL || !stage->initialized || stage->store == NULL)
        return false;
    if (!stage->store->io.erase(
            stage->store->io.context,
            page_address(stage->store, stage->page))) return false;
    stage->initialized = false;
    return true;
}

bool inktag_settings_journal_clear(
    inktag_settings_journal_t *store,
    inktag_settings_clear_action_t action) {
    if (store == NULL || !store->initialized ||
        action < INKTAG_SETTINGS_CLEAR_RESET_SETTINGS ||
        action > INKTAG_SETTINGS_CLEAR_OWNER_FACTORY_RESET) return false;
    return compact_store(store, action);
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

bool inktag_settings_journal_reset_settings_v2(
    inktag_settings_journal_t *store) {
    return clear_scope_v2(store, INKTAG_SETTINGS_RESET_SETTINGS_V2_MASK);
}


bool inktag_settings_journal_empty_media_safe(
    const inktag_settings_journal_io_t *io) {
    inktag_settings_journal_t current = {0};
    if (!io_valid(io)) return false;
    current.io = *io;
    settings_range_result_t erased = range_erased(&current, io->start_address,
                                                 INKTAG_SETTINGS_JOURNAL_SIZE);
    if (erased == SETTINGS_RANGE_IO_ERROR) return false;
    if (erased == SETTINGS_RANGE_ERASED) return true;
    return inktag_settings_journal_init(&current, io, false) &&
        current.tail_clean && current.write_offset == sizeof(settings_page_header_t) &&
        inktag_settings_journal_inactive_erased(&current);
}
