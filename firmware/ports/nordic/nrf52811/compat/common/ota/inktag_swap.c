#include "inktag_swap.h"

#include <stddef.h>
#include <string.h>

#if defined(INKTAG_SWAP_EXECUTE_FROM_RAM) && defined(__GNUC__)
#ifndef INKTAG_SWAP_RAM_SECTION
#if defined(INKTAG_ONMICRO)
#define INKTAG_SWAP_RAM_SECTION ".ram_code"
#else
#define INKTAG_SWAP_RAM_SECTION "RAM_CODE"
#endif
#endif
#define SWAP_RAM_CODE __attribute__((section(INKTAG_SWAP_RAM_SECTION)))
#else
#define SWAP_RAM_CODE
#endif

#define SWAP_HEADER_MAGIC 0x53575449u /* "ITWS" */
#define SWAP_RECORD_MAGIC 0x52575449u /* "ITWR" */
#define SWAP_FORMAT_VERSION 1u
#define SWAP_HEADER_SIZE 256u
#define SWAP_RECORD_SIZE 20u
#define SWAP_COPY_CHUNK 256u

typedef enum {
    SWAP_PHASE_SAVE_A = 0,
    SWAP_PHASE_COPY_B_TO_A = 1,
    SWAP_PHASE_COPY_SCRATCH_TO_B = 2,
    SWAP_PHASE_FINALIZE = 3,
} swap_phase_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t generation;
    uint8_t trial;
    uint8_t reserved0[3];
    uint32_t layout_signature;
    uint32_t slot_a;
    uint32_t slot_b;
    uint32_t slot_size;
    uint32_t sector_size;
    uint32_t scratch;
    uint8_t active_metadata[INKTAG_SWAP_METADATA_SIZE];
    uint8_t candidate_metadata[INKTAG_SWAP_METADATA_SIZE];
    uint8_t reserved1[84];
    uint32_t crc32;
} swap_header_t;

typedef struct {
    uint32_t magic;
    uint32_t generation;
    uint16_t sequence;
    uint16_t sector;
    uint8_t phase;
    uint8_t state;
    uint8_t reserved[2];
    uint32_t crc32;
} swap_record_t;

typedef struct {
    bool valid;
    uint8_t control_index;
    uint32_t control_address;
    swap_header_t header;
    swap_record_t record;
    uint32_t next_record_address;
} swap_transaction_t;

typedef char swap_header_size_must_be_256[(sizeof(swap_header_t) == SWAP_HEADER_SIZE) ? 1 : -1];
typedef char swap_record_size_must_be_20[(sizeof(swap_record_t) == SWAP_RECORD_SIZE) ? 1 : -1];

static uint32_t swap_crc32(const void *data, uint32_t length) {
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;
    while (length-- > 0u) {
        crc ^= *bytes++;
        for (uint8_t bit = 0; bit < 8u; bit++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

static void feed_watchdog(inktag_swap_t *swap) {
    if (swap->io.feed_watchdog) swap->io.feed_watchdog(swap->io.context);
}

static bool io_valid(const inktag_swap_t *swap) {
    if (!swap || !swap->io.read || !swap->io.write || !swap->io.erase) return false;
    const inktag_swap_layout_t *layout = &swap->layout;
    if (!layout->sector_size || layout->sector_size > layout->slot_size ||
        layout->slot_size % layout->sector_size != 0u) return false;
    if (layout->slot_a % layout->sector_size || layout->slot_b % layout->sector_size ||
        layout->control_a % layout->sector_size || layout->control_b % layout->sector_size ||
        layout->scratch % layout->sector_size) return false;
    if (layout->control_a == layout->control_b || layout->control_a == layout->scratch ||
        layout->control_b == layout->scratch || layout->slot_a == layout->slot_b) return false;
    return layout->sector_size >= SWAP_HEADER_SIZE + SWAP_RECORD_SIZE;
}

static bool is_erased(const uint8_t *data, uint32_t length) {
    while (length-- > 0u) {
        if (*data++ != 0xFFu) return false;
    }
    return true;
}

static bool header_valid(const inktag_swap_t *swap, const swap_header_t *header) {
    return header->magic == SWAP_HEADER_MAGIC &&
           header->version == SWAP_FORMAT_VERSION &&
           header->header_size == SWAP_HEADER_SIZE &&
           header->layout_signature == swap->layout.layout_signature &&
           header->slot_a == swap->layout.slot_a &&
           header->slot_b == swap->layout.slot_b &&
           header->slot_size == swap->layout.slot_size &&
           header->sector_size == swap->layout.sector_size &&
           header->scratch == swap->layout.scratch &&
           header->crc32 == swap_crc32(header, offsetof(swap_header_t, crc32));
}

static bool record_valid(const swap_header_t *header, const swap_record_t *record) {
    return record->magic == SWAP_RECORD_MAGIC &&
           record->generation == header->generation &&
           record->state >= INKTAG_SWAP_STATE_SWAPPING &&
           record->state <= INKTAG_SWAP_STATE_CONFIRMED &&
           record->phase <= SWAP_PHASE_FINALIZE &&
           record->crc32 == swap_crc32(record, offsetof(swap_record_t, crc32));
}

static uint32_t control_address(const inktag_swap_t *swap, uint8_t index) {
    return index == 0u ? swap->layout.control_a : swap->layout.control_b;
}

static bool generation_newer(uint32_t left, uint32_t right) {
    return (int32_t)(left - right) > 0;
}

static bool load_transaction_at(inktag_swap_t *swap, uint8_t index,
                                swap_transaction_t *transaction) {
    memset(transaction, 0, sizeof(*transaction));
    transaction->control_index = index;
    transaction->control_address = control_address(swap, index);
    if (!swap->io.read(swap->io.context, transaction->control_address,
                       &transaction->header, sizeof(transaction->header)) ||
        !header_valid(swap, &transaction->header)) {
        return false;
    }

    transaction->valid = true;
    transaction->record.magic = SWAP_RECORD_MAGIC;
    transaction->record.generation = transaction->header.generation;
    transaction->record.sequence = 0u;
    transaction->record.sector = 0u;
    transaction->record.phase = SWAP_PHASE_SAVE_A;
    transaction->record.state = INKTAG_SWAP_STATE_SWAPPING;

    uint32_t address = transaction->control_address + SWAP_HEADER_SIZE;
    uint32_t end = transaction->control_address + swap->layout.sector_size;
    bool found_empty = false;
    while (address + sizeof(swap_record_t) <= end) {
        swap_record_t candidate;
        if (!swap->io.read(swap->io.context, address, &candidate, sizeof(candidate))) {
            return false;
        }
        if (is_erased((const uint8_t *)&candidate, sizeof(candidate))) {
            if (!found_empty) transaction->next_record_address = address;
            found_empty = true;
        } else if (record_valid(&transaction->header, &candidate) &&
                   candidate.sequence >= transaction->record.sequence) {
            transaction->record = candidate;
        }
        address += SWAP_RECORD_SIZE;
    }
    if (!found_empty) transaction->next_record_address = 0u;
    return true;
}

static bool load_latest(inktag_swap_t *swap, swap_transaction_t *transaction) {
    swap_transaction_t first;
    swap_transaction_t second;
    bool have_first = load_transaction_at(swap, 0u, &first);
    bool have_second = load_transaction_at(swap, 1u, &second);
    if (!have_first && !have_second) {
        memset(transaction, 0, sizeof(*transaction));
        return false;
    }
    if (!have_second || (have_first &&
        !generation_newer(second.header.generation, first.header.generation))) {
        *transaction = first;
    } else {
        *transaction = second;
    }
    return true;
}

SWAP_RAM_CODE static inktag_swap_result_t append_record(inktag_swap_t *swap,
                                          swap_transaction_t *transaction,
                                          uint8_t state, uint16_t sector,
                                          uint8_t phase) {
    if (!transaction->next_record_address) return INKTAG_SWAP_LOG_FULL;
    swap_record_t record;
    memset(&record, 0xFF, sizeof(record));
    record.magic = SWAP_RECORD_MAGIC;
    record.generation = transaction->header.generation;
    record.sequence = (uint16_t)(transaction->record.sequence + 1u);
    record.sector = sector;
    record.phase = phase;
    record.state = state;
    record.crc32 = swap_crc32(&record, offsetof(swap_record_t, crc32));
    if (!swap->io.write(swap->io.context, transaction->next_record_address,
                        &record, sizeof(record))) {
        return INKTAG_SWAP_IO_ERROR;
    }
    swap_record_t verify;
    if (!swap->io.read(swap->io.context, transaction->next_record_address,
                       &verify, sizeof(verify)) ||
        memcmp(&record, &verify, sizeof(record)) != 0) {
        return INKTAG_SWAP_VERIFY_ERROR;
    }
    transaction->record = record;
    transaction->next_record_address += SWAP_RECORD_SIZE;
    return INKTAG_SWAP_OK;
}

SWAP_RAM_CODE static bool copy_and_verify(inktag_swap_t *swap, uint32_t destination,
                            uint32_t source, uint32_t length) {
    uint8_t source_buffer[SWAP_COPY_CHUNK];
    uint8_t verify_buffer[SWAP_COPY_CHUNK];
    for (uint32_t offset = 0; offset < length; offset += SWAP_COPY_CHUNK) {
        uint32_t chunk = length - offset;
        if (chunk > SWAP_COPY_CHUNK) chunk = SWAP_COPY_CHUNK;
        if (!swap->io.read(swap->io.context, source + offset, source_buffer, chunk) ||
            !swap->io.write(swap->io.context, destination + offset, source_buffer, chunk) ||
            !swap->io.read(swap->io.context, destination + offset, verify_buffer, chunk) ||
            memcmp(source_buffer, verify_buffer, chunk) != 0) {
            return false;
        }
        feed_watchdog(swap);
    }
    return true;
}

SWAP_RAM_CODE static inktag_swap_result_t exchange_sector(inktag_swap_t *swap,
                                             swap_transaction_t *transaction) {
    const uint32_t sector_size = swap->layout.sector_size;
    const uint32_t offset = (uint32_t)transaction->record.sector * sector_size;
    const uint32_t a = swap->layout.slot_a + offset;
    const uint32_t b = swap->layout.slot_b + offset;
    inktag_swap_result_t result;

    switch ((swap_phase_t)transaction->record.phase) {
        case SWAP_PHASE_SAVE_A:
            if (!swap->io.erase(swap->io.context, swap->layout.scratch, sector_size) ||
                !copy_and_verify(swap, swap->layout.scratch, a, sector_size)) {
                return INKTAG_SWAP_VERIFY_ERROR;
            }
            return append_record(swap, transaction, INKTAG_SWAP_STATE_SWAPPING,
                                 transaction->record.sector, SWAP_PHASE_COPY_B_TO_A);

        case SWAP_PHASE_COPY_B_TO_A:
            if (!swap->io.erase(swap->io.context, a, sector_size) ||
                !copy_and_verify(swap, a, b, sector_size)) {
                return INKTAG_SWAP_VERIFY_ERROR;
            }
            return append_record(swap, transaction, INKTAG_SWAP_STATE_SWAPPING,
                                 transaction->record.sector,
                                 SWAP_PHASE_COPY_SCRATCH_TO_B);

        case SWAP_PHASE_COPY_SCRATCH_TO_B:
            if (!swap->io.erase(swap->io.context, b, sector_size) ||
                !copy_and_verify(swap, b, swap->layout.scratch, sector_size)) {
                return INKTAG_SWAP_VERIFY_ERROR;
            }
            if ((uint32_t)transaction->record.sector + 1u <
                swap->layout.slot_size / sector_size) {
                return append_record(swap, transaction, INKTAG_SWAP_STATE_SWAPPING,
                                     (uint16_t)(transaction->record.sector + 1u),
                                     SWAP_PHASE_SAVE_A);
            }
            result = append_record(swap, transaction, INKTAG_SWAP_STATE_SWAPPING,
                                   transaction->record.sector, SWAP_PHASE_FINALIZE);
            return result;

        case SWAP_PHASE_FINALIZE:
        default:
            return INKTAG_SWAP_OK;
    }
}

SWAP_RAM_CODE static inktag_swap_result_t finalize_transaction(inktag_swap_t *swap,
                                                  swap_transaction_t *transaction) {
    if (!swap->io.activate ||
        !swap->io.activate(swap->io.context, transaction->header.candidate_metadata)) {
        return INKTAG_SWAP_IO_ERROR;
    }
    if (swap->io.clear_pending && !swap->io.clear_pending(swap->io.context)) {
        return INKTAG_SWAP_IO_ERROR;
    }
    return append_record(swap, transaction,
                         transaction->header.trial ? INKTAG_SWAP_STATE_TEST_PENDING
                                                   : INKTAG_SWAP_STATE_CONFIRMED,
                         transaction->record.sector, SWAP_PHASE_FINALIZE);
}

SWAP_RAM_CODE static inktag_swap_result_t run_transaction(inktag_swap_t *swap,
                                            swap_transaction_t *transaction) {
    while (transaction->record.state == INKTAG_SWAP_STATE_SWAPPING) {
        if (transaction->record.phase == SWAP_PHASE_FINALIZE) {
            inktag_swap_result_t result = finalize_transaction(swap, transaction);
            return result == INKTAG_SWAP_OK ? INKTAG_SWAP_RESET_REQUIRED : result;
        }
        inktag_swap_result_t result = exchange_sector(swap, transaction);
        if (result != INKTAG_SWAP_OK) return result;
    }
    return INKTAG_SWAP_OK;
}

SWAP_RAM_CODE static inktag_swap_result_t begin_transaction(
    inktag_swap_t *swap, const swap_transaction_t *previous, bool trial,
    const uint8_t active_metadata[INKTAG_SWAP_METADATA_SIZE],
    const uint8_t candidate_metadata[INKTAG_SWAP_METADATA_SIZE],
    swap_transaction_t *transaction) {
    uint8_t index = previous && previous->valid ? (uint8_t)(previous->control_index ^ 1u) : 0u;
    uint32_t address = control_address(swap, index);
    if (!swap->io.erase(swap->io.context, address, swap->layout.sector_size)) {
        return INKTAG_SWAP_IO_ERROR;
    }

    swap_header_t header;
    memset(&header, 0xFF, sizeof(header));
    header.magic = SWAP_HEADER_MAGIC;
    header.version = SWAP_FORMAT_VERSION;
    header.header_size = SWAP_HEADER_SIZE;
    header.generation = previous && previous->valid ? previous->header.generation + 1u : 1u;
    header.trial = trial ? 1u : 0u;
    header.layout_signature = swap->layout.layout_signature;
    header.slot_a = swap->layout.slot_a;
    header.slot_b = swap->layout.slot_b;
    header.slot_size = swap->layout.slot_size;
    header.sector_size = swap->layout.sector_size;
    header.scratch = swap->layout.scratch;
    memcpy(header.active_metadata, active_metadata, INKTAG_SWAP_METADATA_SIZE);
    memcpy(header.candidate_metadata, candidate_metadata, INKTAG_SWAP_METADATA_SIZE);
    header.crc32 = swap_crc32(&header, offsetof(swap_header_t, crc32));
    if (!swap->io.write(swap->io.context, address, &header, sizeof(header))) {
        return INKTAG_SWAP_IO_ERROR;
    }
    if (!load_transaction_at(swap, index, transaction)) {
        return INKTAG_SWAP_METADATA_ERROR;
    }
    return INKTAG_SWAP_OK;
}

SWAP_RAM_CODE inktag_swap_result_t inktag_swap_install(
    inktag_swap_t *swap,
    const uint8_t active_metadata[INKTAG_SWAP_METADATA_SIZE],
    const uint8_t candidate_metadata[INKTAG_SWAP_METADATA_SIZE]) {
    if (!io_valid(swap)) return INKTAG_SWAP_BAD_LAYOUT;
    if (!active_metadata || !candidate_metadata || !swap->io.activate) {
        return INKTAG_SWAP_METADATA_ERROR;
    }
    swap_transaction_t previous;
    bool have_previous = load_latest(swap, &previous);
    if (have_previous && previous.record.state != INKTAG_SWAP_STATE_CONFIRMED) {
        return INKTAG_SWAP_BUSY;
    }
    swap_transaction_t transaction;
    inktag_swap_result_t result = begin_transaction(
        swap, have_previous ? &previous : NULL, true, active_metadata,
        candidate_metadata, &transaction);
    return result == INKTAG_SWAP_OK ? run_transaction(swap, &transaction) : result;
}

SWAP_RAM_CODE inktag_swap_result_t inktag_swap_prepare(
    inktag_swap_t *swap,
    const uint8_t active_metadata[INKTAG_SWAP_METADATA_SIZE],
    const uint8_t candidate_metadata[INKTAG_SWAP_METADATA_SIZE]) {
    if (!io_valid(swap)) return INKTAG_SWAP_BAD_LAYOUT;
    if (!active_metadata || !candidate_metadata || !swap->io.activate) {
        return INKTAG_SWAP_METADATA_ERROR;
    }
    swap_transaction_t previous;
    bool have_previous = load_latest(swap, &previous);
    if (have_previous && previous.record.state != INKTAG_SWAP_STATE_CONFIRMED) {
        return INKTAG_SWAP_BUSY;
    }
    swap_transaction_t transaction;
    inktag_swap_result_t result = begin_transaction(
        swap, have_previous ? &previous : NULL, true, active_metadata,
        candidate_metadata, &transaction);
    return result == INKTAG_SWAP_OK ? INKTAG_SWAP_RESET_REQUIRED : result;
}

SWAP_RAM_CODE inktag_swap_result_t inktag_swap_boot(inktag_swap_t *swap) {
    if (!io_valid(swap)) return INKTAG_SWAP_BAD_LAYOUT;
    swap_transaction_t transaction;
    if (!load_latest(swap, &transaction)) return INKTAG_SWAP_NOT_PENDING;
    if (transaction.record.state == INKTAG_SWAP_STATE_SWAPPING) {
        return run_transaction(swap, &transaction);
    }
    if (transaction.record.state == INKTAG_SWAP_STATE_TEST_PENDING) {
        return append_record(swap, &transaction, INKTAG_SWAP_STATE_TRIAL_STARTED,
                             transaction.record.sector, SWAP_PHASE_FINALIZE);
    }
    if (transaction.record.state == INKTAG_SWAP_STATE_TRIAL_STARTED) {
        swap_transaction_t revert;
        inktag_swap_result_t result = begin_transaction(
            swap, &transaction, false, transaction.header.candidate_metadata,
            transaction.header.active_metadata, &revert);
        return result == INKTAG_SWAP_OK ? run_transaction(swap, &revert) : result;
    }
    return INKTAG_SWAP_OK;
}

SWAP_RAM_CODE inktag_swap_result_t inktag_swap_confirm(inktag_swap_t *swap) {
    if (!io_valid(swap)) return INKTAG_SWAP_BAD_LAYOUT;
    swap_transaction_t transaction;
    if (!load_latest(swap, &transaction)) return INKTAG_SWAP_NOT_PENDING;
    if (transaction.record.state == INKTAG_SWAP_STATE_CONFIRMED) return INKTAG_SWAP_OK;
    if (transaction.record.state != INKTAG_SWAP_STATE_TRIAL_STARTED) {
        return INKTAG_SWAP_BUSY;
    }
    return append_record(swap, &transaction, INKTAG_SWAP_STATE_CONFIRMED,
                         transaction.record.sector, SWAP_PHASE_FINALIZE);
}

inktag_swap_result_t inktag_swap_status(inktag_swap_t *swap,
                                        inktag_swap_state_t *state,
                                        uint32_t *generation) {
    if (!io_valid(swap)) return INKTAG_SWAP_BAD_LAYOUT;
    swap_transaction_t transaction;
    if (!load_latest(swap, &transaction)) {
        if (state) *state = INKTAG_SWAP_STATE_NONE;
        if (generation) *generation = 0u;
        return INKTAG_SWAP_NOT_PENDING;
    }
    if (state) *state = (inktag_swap_state_t)transaction.record.state;
    if (generation) *generation = transaction.header.generation;
    return INKTAG_SWAP_OK;
}
