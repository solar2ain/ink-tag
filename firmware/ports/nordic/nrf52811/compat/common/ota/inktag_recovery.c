#include "inktag_recovery.h"

#include <stddef.h>
#include <string.h>

#define RECOVERY_RECORD_MAGIC 0x52544B49u /* "IKTR" */
#define RECOVERY_RECORD_SIZE 32u
#define RECOVERY_EPOCH_METADATA 2u

typedef struct {
    uint32_t magic;
    uint32_t layout_signature;
    uint32_t generation;
    uint32_t sequence;
    uint8_t state;
    uint8_t metadata_format;
    uint16_t reserved;
    uint32_t confirmed_epoch;
    uint32_t reserved_tail;
    uint32_t crc32;
} recovery_record_t;

typedef struct {
    bool have_latest;
    recovery_record_t latest;
    uint8_t latest_page;
    uint32_t next_address[2];
    /* Zero means legacy/no floor; otherwise value is confirmed_epoch + 1. */
    uint32_t encoded_confirmed_epoch;
} recovery_log_t;

typedef char recovery_record_size_must_be_32[
    sizeof(recovery_record_t) == RECOVERY_RECORD_SIZE ? 1 : -1];

static uint32_t recovery_crc32(const void *data, uint32_t length) {
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

static bool io_valid(const inktag_recovery_t *recovery) {
    return recovery && recovery->io.read && recovery->io.write &&
           recovery->io.erase &&
           recovery->layout.sector_size >= RECOVERY_RECORD_SIZE &&
           recovery->layout.sector_size % RECOVERY_RECORD_SIZE == 0u &&
           recovery->layout.control_a != recovery->layout.control_b &&
           recovery->layout.control_a % recovery->layout.sector_size == 0u &&
           recovery->layout.control_b % recovery->layout.sector_size == 0u;
}

static bool erased(const recovery_record_t *record) {
    const uint32_t *words = (const uint32_t *)record;
    for (uint8_t i = 0; i < sizeof(*record) / sizeof(*words); i++) {
        if (words[i] != 0xFFFFFFFFu) return false;
    }
    return true;
}

static bool valid(const inktag_recovery_t *recovery,
                  const recovery_record_t *record) {
    return record->magic == RECOVERY_RECORD_MAGIC &&
           record->layout_signature == recovery->layout.layout_signature &&
           record->state >= INKTAG_RECOVERY_STATE_ARMED &&
           record->state <= INKTAG_RECOVERY_STATE_REQUIRED &&
           record->crc32 == recovery_crc32(record, offsetof(recovery_record_t, crc32));
}

static bool newer(const recovery_record_t *left,
                  const recovery_record_t *right) {
    int32_t generation_delta = (int32_t)(left->generation - right->generation);
    return generation_delta > 0 ||
           (generation_delta == 0 &&
            (int32_t)(left->sequence - right->sequence) > 0);
}

static uint32_t page_address(const inktag_recovery_t *recovery, uint8_t page) {
    return page == 0u ? recovery->layout.control_a : recovery->layout.control_b;
}

static bool scan_log(inktag_recovery_t *recovery, recovery_log_t *log) {
    memset(log, 0, sizeof(*log));
    for (uint8_t page = 0; page < 2u; page++) {
        const uint32_t start = page_address(recovery, page);
        const uint32_t end = start + recovery->layout.sector_size;
        bool found_empty = false;
        for (uint32_t address = start; address < end;
             address += RECOVERY_RECORD_SIZE) {
            recovery_record_t candidate;
            if (!recovery->io.read(recovery->io.context, address, &candidate,
                                   sizeof(candidate))) {
                return false;
            }
            if (erased(&candidate)) {
                if (!found_empty) log->next_address[page] = address;
                found_empty = true;
            } else if (valid(recovery, &candidate)) {
                if (candidate.metadata_format == RECOVERY_EPOCH_METADATA &&
                    candidate.confirmed_epoch + 1u >
                        log->encoded_confirmed_epoch) {
                    log->encoded_confirmed_epoch =
                        candidate.confirmed_epoch + 1u;
                }
                if (!log->have_latest || newer(&candidate, &log->latest)) {
                    log->have_latest = true;
                    log->latest = candidate;
                    log->latest_page = page;
                }
            }
        }
    }
    return true;
}

static inktag_recovery_result_t append(inktag_recovery_t *recovery,
                                       recovery_log_t *log,
                                       uint32_t generation,
                                       uint32_t sequence,
                                       inktag_recovery_state_t state,
                                       uint32_t encoded_confirmed_epoch) {
    uint8_t page = log->have_latest ? log->latest_page : 0u;
    uint32_t address = log->next_address[page];
    if (!address) {
        page = log->have_latest ? (uint8_t)(log->latest_page ^ 1u) : 0u;
        address = page_address(recovery, page);
        if (!recovery->io.erase(recovery->io.context, address,
                                recovery->layout.sector_size)) {
            return INKTAG_RECOVERY_IO_ERROR;
        }
    } else if (!log->have_latest && address != page_address(recovery, page)) {
        address = page_address(recovery, page);
        if (!recovery->io.erase(recovery->io.context, address,
                                recovery->layout.sector_size)) {
            return INKTAG_RECOVERY_IO_ERROR;
        }
    }

    recovery_record_t record;
    memset(&record, 0xFF, sizeof(record));
    record.magic = RECOVERY_RECORD_MAGIC;
    record.layout_signature = recovery->layout.layout_signature;
    record.generation = generation;
    record.sequence = sequence;
    record.state = (uint8_t)state;
    if (encoded_confirmed_epoch != 0u) {
        record.metadata_format = RECOVERY_EPOCH_METADATA;
        record.confirmed_epoch = encoded_confirmed_epoch - 1u;
    }
    record.crc32 = recovery_crc32(&record, offsetof(recovery_record_t, crc32));
    if (!recovery->io.write(recovery->io.context, address, &record,
                            sizeof(record))) {
        return INKTAG_RECOVERY_IO_ERROR;
    }
    recovery_record_t verify;
    if (!recovery->io.read(recovery->io.context, address, &verify,
                           sizeof(verify)) ||
        memcmp(&record, &verify, sizeof(record)) != 0) {
        return INKTAG_RECOVERY_VERIFY_ERROR;
    }
    return INKTAG_RECOVERY_OK;
}

inktag_recovery_result_t inktag_recovery_arm(inktag_recovery_t *recovery) {
    if (!io_valid(recovery)) return INKTAG_RECOVERY_BAD_LAYOUT;
    recovery_log_t log;
    if (!scan_log(recovery, &log)) return INKTAG_RECOVERY_IO_ERROR;
    uint32_t generation = log.have_latest ? log.latest.generation + 1u : 1u;
    return append(recovery, &log, generation, 0u,
                  INKTAG_RECOVERY_STATE_ARMED, 0u);
}

inktag_recovery_result_t inktag_recovery_arm_compatibility_epoch(
    inktag_recovery_t *recovery, uint32_t compatibility_epoch) {
    if (!io_valid(recovery) || compatibility_epoch >
            INKTAG_RECOVERY_COMPATIBILITY_EPOCH_MAX) {
        return INKTAG_RECOVERY_BAD_LAYOUT;
    }
    recovery_log_t log;
    if (!scan_log(recovery, &log)) return INKTAG_RECOVERY_IO_ERROR;
    if (log.encoded_confirmed_epoch != 0u &&
        compatibility_epoch + 1u < log.encoded_confirmed_epoch) {
        return INKTAG_RECOVERY_ROLLBACK_ERROR;
    }
    uint32_t generation = log.have_latest ? log.latest.generation + 1u : 1u;
    return append(recovery, &log, generation, 0u,
                  INKTAG_RECOVERY_STATE_ARMED,
                  log.encoded_confirmed_epoch);
}

inktag_recovery_result_t inktag_recovery_boot(inktag_recovery_t *recovery) {
    if (!io_valid(recovery)) return INKTAG_RECOVERY_BAD_LAYOUT;
    recovery_log_t log;
    if (!scan_log(recovery, &log)) return INKTAG_RECOVERY_IO_ERROR;
    if (!log.have_latest) return INKTAG_RECOVERY_NOT_PENDING;
    if (log.latest.state == INKTAG_RECOVERY_STATE_ARMED) {
        return append(recovery, &log, log.latest.generation,
                      log.latest.sequence + 1u,
                      INKTAG_RECOVERY_STATE_TRIAL_STARTED,
                      log.encoded_confirmed_epoch);
    }
    if (log.latest.state == INKTAG_RECOVERY_STATE_TRIAL_STARTED) {
        inktag_recovery_result_t result = append(
            recovery, &log, log.latest.generation,
            log.latest.sequence + 1u, INKTAG_RECOVERY_STATE_REQUIRED,
            log.encoded_confirmed_epoch);
        return result == INKTAG_RECOVERY_OK ? INKTAG_RECOVERY_ENTER : result;
    }
    return log.latest.state == INKTAG_RECOVERY_STATE_REQUIRED
               ? INKTAG_RECOVERY_ENTER
               : INKTAG_RECOVERY_OK;
}

inktag_recovery_result_t inktag_recovery_confirm(inktag_recovery_t *recovery) {
    if (!io_valid(recovery)) return INKTAG_RECOVERY_BAD_LAYOUT;
    recovery_log_t log;
    if (!scan_log(recovery, &log)) return INKTAG_RECOVERY_IO_ERROR;
    if (!log.have_latest) return INKTAG_RECOVERY_NOT_PENDING;
    if (log.latest.state == INKTAG_RECOVERY_STATE_CONFIRMED) {
        return INKTAG_RECOVERY_OK;
    }
    if (log.latest.state != INKTAG_RECOVERY_STATE_TRIAL_STARTED) {
        return INKTAG_RECOVERY_ENTER;
    }
    return append(recovery, &log, log.latest.generation,
                  log.latest.sequence + 1u, INKTAG_RECOVERY_STATE_CONFIRMED,
                  log.encoded_confirmed_epoch);
}

inktag_recovery_result_t inktag_recovery_confirm_compatibility_epoch(
    inktag_recovery_t *recovery, uint32_t compatibility_epoch) {
    if (!io_valid(recovery) || compatibility_epoch >
            INKTAG_RECOVERY_COMPATIBILITY_EPOCH_MAX) {
        return INKTAG_RECOVERY_BAD_LAYOUT;
    }
    recovery_log_t log;
    if (!scan_log(recovery, &log)) return INKTAG_RECOVERY_IO_ERROR;
    if (!log.have_latest) return INKTAG_RECOVERY_NOT_PENDING;
    if (log.latest.state == INKTAG_RECOVERY_STATE_CONFIRMED) {
        return INKTAG_RECOVERY_OK;
    }
    if (log.latest.state != INKTAG_RECOVERY_STATE_TRIAL_STARTED) {
        return INKTAG_RECOVERY_ENTER;
    }
    if (log.encoded_confirmed_epoch != 0u &&
        compatibility_epoch + 1u < log.encoded_confirmed_epoch) {
        return INKTAG_RECOVERY_ROLLBACK_ERROR;
    }
    return append(recovery, &log, log.latest.generation,
                  log.latest.sequence + 1u, INKTAG_RECOVERY_STATE_CONFIRMED,
                  compatibility_epoch + 1u);
}

inktag_recovery_result_t inktag_recovery_status(
    inktag_recovery_t *recovery, inktag_recovery_state_t *state,
    uint32_t *generation) {
    if (!io_valid(recovery)) return INKTAG_RECOVERY_BAD_LAYOUT;
    recovery_log_t log;
    if (!scan_log(recovery, &log)) return INKTAG_RECOVERY_IO_ERROR;
    if (!log.have_latest) {
        if (state) *state = INKTAG_RECOVERY_STATE_NONE;
        if (generation) *generation = 0u;
        return INKTAG_RECOVERY_NOT_PENDING;
    }
    if (state) *state = (inktag_recovery_state_t)log.latest.state;
    if (generation) *generation = log.latest.generation;
    return log.latest.state == INKTAG_RECOVERY_STATE_REQUIRED
               ? INKTAG_RECOVERY_ENTER
               : INKTAG_RECOVERY_OK;
}
