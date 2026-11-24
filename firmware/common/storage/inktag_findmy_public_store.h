#ifndef INKTAG_FINDMY_PUBLIC_STORE_H
#define INKTAG_FINDMY_PUBLIC_STORE_H

#include "inktag_findmy_public.h"

/* Existing Settings ID31 schema. Never a second journal or private d/seed. */
#define INKTAG_FIND_PUBLIC_RECORD_BYTES 73u
typedef enum {
    INKTAG_FIND_PUBLIC_STORE_IO_ERROR = 0,
    INKTAG_FIND_PUBLIC_STORE_EMPTY,
    INKTAG_FIND_PUBLIC_STORE_PRESENT,
    INKTAG_FIND_PUBLIC_STORE_LEGACY,
    INKTAG_FIND_PUBLIC_STORE_INVALID,
    INKTAG_FIND_PUBLIC_STORE_UNSAFE,
    INKTAG_FIND_PUBLIC_STORE_CONFLICT,
} inktag_find_public_store_result_t;

typedef struct {
    void *context;
    /* EMPTY/FOUND(PRESENT)/IO_ERROR, bounded raw record; no cache-as-authority. */
    inktag_find_public_store_result_t (*read)(void *, uint8_t *, uint16_t,
                                             uint16_t *);
    bool (*put_atomic)(void *, const uint8_t *, uint16_t);
    /* Read-only evidence: retired records/regions cannot retain old material;
     * inactive secret generations must be erased. No erase on load/provision. */
    bool (*safe)(void *, bool first_provision);
} inktag_find_public_store_io_t;

/* P/K outputs optional, and untouched unless PRESENT. Legacy/corrupt/IO are
 * not EMPTY. The exact-point check is supplied by the vendor ECC backend. */
inktag_find_public_store_result_t inktag_find_public_store_load(
    const inktag_find_public_store_io_t *, inktag_findmy_public_point_valid_fn,
    uint8_t master_xy[56], uint8_t key[16]);

/* Caller has authenticated/derived the candidate BEFORE this publication.
 * NULL old fingerprint is first provision; replacement requires fresh exact
 * old fingerprint. On every write result re-read durable authority. Exact new
 * plus retirement proof is success even if final-write ACK was lost; anything
 * else is failure and must invalidate the caller's cached identity. Never
 * deletes then writes and never enables Find config as a side effect. */
inktag_find_public_store_result_t inktag_find_public_store_publish(
    const inktag_find_public_store_io_t *, inktag_findmy_public_point_valid_fn,
    const uint8_t old_fingerprint[16], const uint8_t master_xy[56],
    const uint8_t key[16]);

#endif
