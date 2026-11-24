#ifndef INKTAG_LIFECYCLE_PENDING_H
#define INKTAG_LIFECYCLE_PENDING_H

#include <stdbool.h>
#include <stdint.h>
#include "../storage/inktag_settings_records.h"

/* Discriminators in the existing protected ID32, not a new record or journal.
 * Only the store can prove absence. NULL, short, unknown and IO-derived input
 * must never be interpreted as an empty marker or as a factory-reset request. */
#define INKTAG_LIFECYCLE_PENDING_RECORD INKTAG_SETTINGS_RECORD_OWNER_RESET_PENDING
#define INKTAG_LIFECYCLE_PENDING_BYTES INKTAG_SETTINGS_OWNER_RESET_PENDING_SIZE
#define INKTAG_LIFECYCLE_CONTENT_PENDING_MAGIC \
    {0x49u, 0x4bu, 0x54u, 0x43u, 0x55u, 0x44u, 0x31u, 0xa5u}

typedef enum {
    INKTAG_LIFECYCLE_PENDING_INVALID = 0,
    INKTAG_LIFECYCLE_PENDING_FACTORY_RESET = 1,
    INKTAG_LIFECYCLE_PENDING_CLEAR_USER_DATA = 2,
} inktag_lifecycle_pending_scope_t;

inktag_lifecycle_pending_scope_t inktag_lifecycle_pending_decode(
    const uint8_t *wire, uint16_t length);
bool inktag_lifecycle_pending_encode(inktag_lifecycle_pending_scope_t scope,
    uint8_t *wire, uint16_t capacity);

/* The port owns protected ID32 storage and the exact data backends. Missing
 * must be distinguished from IO failure; FOUND reports the actual record
 * length and copies at most INKTAG_LIFECYCLE_PENDING_BYTES bytes. */
typedef enum {
    INKTAG_LIFECYCLE_PENDING_READ_ERROR = 0,
    INKTAG_LIFECYCLE_PENDING_READ_MISSING,
    INKTAG_LIFECYCLE_PENDING_READ_FOUND,
} inktag_lifecycle_pending_read_t;

typedef struct {
    void *context;
    inktag_lifecycle_pending_read_t (*read)(void *context,
        uint8_t wire[INKTAG_LIFECYCLE_PENDING_BYTES], uint16_t *length);
    bool (*write)(void *context,
        const uint8_t wire[INKTAG_LIFECYCLE_PENDING_BYTES]);
    bool (*remove)(void *context);
    /* INVALID quarantines an unknown marker; no clear is then attempted. */
    void (*lock)(void *context, inktag_lifecycle_pending_scope_t scope);
    bool (*clear)(void *context, inktag_lifecycle_pending_scope_t scope);
    bool (*verify)(void *context, inktag_lifecycle_pending_scope_t scope);
} inktag_lifecycle_pending_io_t;

typedef enum {
    INKTAG_LIFECYCLE_PENDING_RESULT_ERROR = 0,
    INKTAG_LIFECYCLE_PENDING_RESULT_DONE,
    INKTAG_LIFECYCLE_PENDING_RESULT_NONE,
    INKTAG_LIFECYCLE_PENDING_RESULT_CONFLICT,
} inktag_lifecycle_pending_result_t;

/* BEGIN is only for an externally admitted command: the caller must already
 * enforce password/session, CONFIRM, busy and fresh power checks. It publishes
 * and reloads ID32 before clearing. RESUME is for boot/fault continuation of
 * an already committed marker; call before content restore or mutation. Both
 * keep the marker on any incomplete scope, and never turn one scope into the
 * other. The port must keep pending/unknown content locked on an ERROR. */
inktag_lifecycle_pending_result_t inktag_lifecycle_pending_begin(
    const inktag_lifecycle_pending_io_t *io,
    inktag_lifecycle_pending_scope_t scope);
inktag_lifecycle_pending_result_t inktag_lifecycle_pending_resume(
    const inktag_lifecycle_pending_io_t *io);

#endif
