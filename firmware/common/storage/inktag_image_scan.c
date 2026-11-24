#include "inktag_image_scan.h"
#include "inktag_protocol_core.h"
#include <string.h>

enum { SELECT, HEADER, PAYLOAD, BLANK, FREE };
static bool bit(const uint8_t *bits, uint16_t sector) {
    return (bits[sector >> 3u] & (1u << (sector & 7u))) != 0u;
}
static void mark(uint8_t *bits, uint16_t sector) {
    bits[sector >> 3u] |= (uint8_t)(1u << (sector & 7u));
}
static bool all_ff(const uint8_t *bytes, uint16_t length) {
    for (uint16_t i = 0; i < length; ++i) if (bytes[i] != 255u) return false;
    return true;
}
static inktag_image_scan_result_t finish(inktag_image_scan_t *job,
                                        inktag_image_scan_result_t result) {
    job->result = result;
    return result;
}
static inktag_image_scan_result_t failure(inktag_image_scan_result_t result) {
    return result == INKTAG_IMAGE_SCAN_IO_ERROR ||
           result == INKTAG_IMAGE_SCAN_UNAVAILABLE ? result : INKTAG_IMAGE_SCAN_INVALID;
}
inktag_image_scan_result_t inktag_image_scan_begin(
    inktag_image_scan_t *job, uint8_t *wire, uint16_t capacity, uint16_t length,
    uint16_t limit, bool authority, uint8_t *used, uint8_t *dirty,
    uint16_t bitmap_capacity) {
    if (!job) return INKTAG_IMAGE_SCAN_INVALID;
    memset(job, 0, sizeof(*job));
    job->result = INKTAG_IMAGE_SCAN_INVALID;
    if (!wire || !used || !dirty || !limit || limit > INKTAG_IMAGE_INDEX_UNUSED ||
        bitmap_capacity < (limit + 7u) / 8u || length > capacity)
        return job->result;
    if (authority) {
        if (!inktag_image_index_validate(wire, length, 255u, limit)) return job->result;
    } else if (inktag_image_index_reset(wire, capacity, 255u, limit, &length) != INKTAG_IMAGE_INDEX_OK)
        return job->result;
    memset(used, 0, (limit + 7u) / 8u);
    memset(dirty, 0, (limit + 7u) / 8u);
    job->wire = wire; job->used = used; job->dirty = dirty;
    job->length = length; job->limit = limit; job->authority = authority;
    return finish(job, INKTAG_IMAGE_SCAN_PENDING);
}
inktag_image_scan_result_t inktag_image_scan_step(
    inktag_image_scan_t *job, const inktag_image_scan_backend_t *io) {
    if (!job) return INKTAG_IMAGE_SCAN_INVALID;
    if (job->result != INKTAG_IMAGE_SCAN_PENDING) return job->result;
    if (!io || !io->probe || !io->span || !io->read ||
        !io->header_bytes || io->header_bytes > INKTAG_IMAGE_SCAN_CHUNK)
        return finish(job, INKTAG_IMAGE_SCAN_INVALID);
    if (job->phase == SELECT) {
        if (job->authority) {
            while (job->slot < 255u && inktag_image_index_get(job->wire, job->length,
                    (uint8_t)job->slot) == INKTAG_IMAGE_INDEX_UNUSED) ++job->slot;
            if (job->slot == 255u) { job->sector = 0; job->phase = FREE; }
            else {
                job->sector = inktag_image_index_get(job->wire, job->length, (uint8_t)job->slot);
                job->phase = HEADER;
            }
        } else {
            while (job->sector < job->limit) {
                inktag_image_scan_result_t span = io->span(io->context, job->sector, 1u);
                if (span != INKTAG_IMAGE_SCAN_RESERVED) {
                    if (span != INKTAG_IMAGE_SCAN_OK) return finish(job, failure(span));
                    break;
                }
                ++job->sector;
            }
            if (job->sector == job->limit) return finish(job, INKTAG_IMAGE_SCAN_OK);
            job->phase = HEADER;
        }
    }
    if (job->phase == FREE) {
        while (job->sector < job->limit) {
            if (bit(job->used, job->sector)) { ++job->sector; continue; }
            inktag_image_scan_result_t span = io->span(io->context, job->sector, 1u);
            if (span == INKTAG_IMAGE_SCAN_RESERVED) { ++job->sector; continue; }
            if (span != INKTAG_IMAGE_SCAN_OK) return finish(job, failure(span));
            break;
        }
        if (job->sector == job->limit) return finish(job, INKTAG_IMAGE_SCAN_OK);
        job->offset = (uint32_t)job->sector * INKTAG_IMAGE_SCAN_SECTOR;
        job->remaining = INKTAG_IMAGE_SCAN_SECTOR;
        job->phase = BLANK;
    }
    if (job->phase == HEADER) {
        inktag_image_scan_result_t probed = io->probe(io->context, job->sector, &job->record);
        if (probed == INKTAG_IMAGE_SCAN_EMPTY && !job->authority) {
            job->offset = (uint32_t)job->sector * INKTAG_IMAGE_SCAN_SECTOR + io->header_bytes;
            job->remaining = INKTAG_IMAGE_SCAN_SECTOR - io->header_bytes;
            job->phase = BLANK;
            return job->result;
        }
        if (probed != INKTAG_IMAGE_SCAN_OK)
            return finish(job, failure(probed));
        inktag_image_scan_record_t *record = &job->record;
        uint32_t first = (uint32_t)job->sector * INKTAG_IMAGE_SCAN_SECTOR;
        if (!record->sectors || record->sectors > job->limit - job->sector ||
            record->id == 255u || !record->bytes || record->offset < first + io->header_bytes ||
            record->offset > first + (uint32_t)record->sectors * INKTAG_IMAGE_SCAN_SECTOR ||
            record->bytes > first + (uint32_t)record->sectors * INKTAG_IMAGE_SCAN_SECTOR - record->offset ||
            (job->authority ? record->id != job->slot :
             inktag_image_index_get(job->wire, job->length, record->id) != INKTAG_IMAGE_INDEX_UNUSED))
            return finish(job, INKTAG_IMAGE_SCAN_INVALID);
        inktag_image_scan_result_t span = io->span(io->context, job->sector, record->sectors);
        if (span != INKTAG_IMAGE_SCAN_OK) return finish(job, failure(span));
        for (uint16_t sector = job->sector; sector < job->sector + record->sectors; ++sector) {
            if (bit(job->used, sector)) return finish(job, INKTAG_IMAGE_SCAN_INVALID);
            mark(job->used, sector);
        }
        job->offset = record->offset; job->remaining = record->bytes;
        job->crc = 0xffffffffu; job->phase = PAYLOAD;
        return job->result;
    }
    uint8_t chunk[INKTAG_IMAGE_SCAN_CHUNK];
    uint16_t count = job->remaining > sizeof(chunk) ? sizeof(chunk) : (uint16_t)job->remaining;
    if (!count || !io->read(io->context, job->offset, chunk, count))
        return finish(job, INKTAG_IMAGE_SCAN_IO_ERROR);
    job->offset += count; job->remaining -= count;
    if (job->phase == PAYLOAD) {
        job->crc = inktag_crc32_update(job->crc, chunk, count);
        if (job->remaining) return job->result;
        if ((job->crc ^ 0xffffffffu) != job->record.crc)
            return finish(job, INKTAG_IMAGE_SCAN_INVALID);
        if (!job->authority && inktag_image_index_set(job->wire, &job->length,
                job->record.id, job->sector) != INKTAG_IMAGE_INDEX_OK)
            return finish(job, INKTAG_IMAGE_SCAN_INVALID);
        if (job->authority) ++job->slot;
        else job->sector += job->record.sectors;
        job->phase = SELECT;
    } else {
        if (!all_ff(chunk, count)) {
            if (!job->authority) return finish(job, INKTAG_IMAGE_SCAN_INVALID);
            mark(job->dirty, job->sector); job->remaining = 0;
        }
        if (!job->remaining) { ++job->sector; job->phase = job->authority ? FREE : SELECT; }
    }
    return job->result;
}
