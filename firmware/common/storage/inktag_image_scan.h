#ifndef INKTAG_IMAGE_SCAN_H
#define INKTAG_IMAGE_SCAN_H

#include "inktag_image_index.h"

#define INKTAG_IMAGE_SCAN_CHUNK 128u
#define INKTAG_IMAGE_SCAN_SECTOR 4096u

typedef enum {
    INKTAG_IMAGE_SCAN_PENDING, INKTAG_IMAGE_SCAN_OK,
    INKTAG_IMAGE_SCAN_INVALID, INKTAG_IMAGE_SCAN_IO_ERROR,
    INKTAG_IMAGE_SCAN_UNAVAILABLE, INKTAG_IMAGE_SCAN_EMPTY,
    INKTAG_IMAGE_SCAN_RESERVED
} inktag_image_scan_result_t;

typedef struct {
    uint32_t offset, bytes, crc;
    uint16_t sectors;
    uint8_t id;
} inktag_image_scan_record_t;

typedef struct {
    void *context;
    uint16_t header_bytes;
    inktag_image_scan_result_t (*span)(void *, uint16_t, uint16_t);
    /* One bounded header read; EMPTY proves only its prefix all-FF.
     * A record probe validates the format/header/profile and full span. */
    inktag_image_scan_result_t (*probe)(void *, uint16_t, inktag_image_scan_record_t *);
    bool (*read)(void *, uint32_t, uint8_t *, uint16_t);
} inktag_image_scan_backend_t;

typedef struct {
    uint8_t *wire, *used, *dirty;
    uint16_t length, limit, sector, slot;
    uint32_t offset, remaining, crc;
    inktag_image_scan_record_t record;
    inktag_image_scan_result_t result;
    uint8_t phase;
    bool authority;
} inktag_image_scan_t;

/* The caller owns a private candidate and two ceil(limit/8) byte bitmaps.
 * No pending/failed candidate may be exposed or used to authorize writes.
 * authority=true preserves the existing map and ignores orphan records;
 * otherwise every unreserved byte must be empty or belong to one valid record.
 * Each step performs at most ONE probe/read, each read <=128 bytes.
 * No persistence, erase, timers or client transaction state exists here. */
inktag_image_scan_result_t inktag_image_scan_begin(
    inktag_image_scan_t *, uint8_t *wire, uint16_t capacity, uint16_t length,
    uint16_t limit, bool authority, uint8_t *used, uint8_t *dirty,
    uint16_t bitmap_capacity);
inktag_image_scan_result_t inktag_image_scan_step(
    inktag_image_scan_t *, const inktag_image_scan_backend_t *);

#endif
