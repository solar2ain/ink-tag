#ifndef INKTAG_PUBLIC_CACHE_RETIREMENT_H
#define INKTAG_PUBLIC_CACHE_RETIREMENT_H

#include <stdbool.h>
#include <stdint.h>

/* Frozen FKR2/FKC2 storage codec from 67f0da33. No legacy runtime reader. */
#define INKTAG_PUBLIC_CACHE_RECORDS 16u
#define INKTAG_PUBLIC_CACHE_MAX_BYTES 472u
#define INKTAG_PUBLIC_CACHE_AUTHORITY_BYTES 73u

typedef enum {
    INKTAG_PUBLIC_CACHE_ABSENT,
    INKTAG_PUBLIC_CACHE_PRESENT,
    INKTAG_PUBLIC_CACHE_IO
} inktag_public_cache_read_t;
typedef enum {
    INKTAG_PUBLIC_CACHE_OK,
    INKTAG_PUBLIC_CACHE_REFUSED,
    INKTAG_PUBLIC_CACHE_INVALID,
    INKTAG_PUBLIC_CACHE_IO_ERROR
} inktag_public_cache_result_t;
typedef struct {
    void *context;
    /* Both admission and authority are checked again before every delete. */
    bool (*allowed)(void *context);
    bool (*authority)(void *context, uint8_t value[INKTAG_PUBLIC_CACHE_AUTHORITY_BYTES]);
    inktag_public_cache_read_t (*read)(void *context, uint8_t record,
                                      uint8_t *value, uint16_t *length);
    void (*remove)(void *context, uint8_t record);
} inktag_public_cache_io_t;

/* Caller-owned scratch; ports may lend an idle display buffer. Keep the
 * bounded plan off constrained MCU stacks, without a new persistent store. */
typedef struct {
    uint32_t generation, upload, checksum, keys_crc;
    uint16_t length, received;
    uint8_t state, keys, mask, counts[2];
} inktag_public_cache_shape_t;
typedef struct inktag_public_cache_workspace {
    inktag_public_cache_shape_t plan[INKTAG_PUBLIC_CACHE_RECORDS], shape;
    uint8_t value[INKTAG_PUBLIC_CACHE_MAX_BYTES];
    uint8_t authority[INKTAG_PUBLIC_CACHE_AUTHORITY_BYTES];
    uint8_t current[INKTAG_PUBLIC_CACHE_AUTHORITY_BYTES];
} inktag_public_cache_workspace_t;

/* All sixteen records are classified before the first deletion. Public cache
 * only: supported logical deletion/readback, never physical secret scrubbing.
 * Interrupted retries must start here again, not reuse a partially stale plan. */
inktag_public_cache_result_t inktag_public_cache_retire(
    const inktag_public_cache_io_t *io, inktag_public_cache_workspace_t *workspace,
    uint8_t *removed);

#endif
