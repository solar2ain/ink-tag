#include "inktag_lifecycle_pending.h"
#include <string.h>

static const uint8_t factory[INKTAG_LIFECYCLE_PENDING_BYTES] =
    INKTAG_SETTINGS_OWNER_RESET_PENDING_MAGIC;
static const uint8_t content[INKTAG_LIFECYCLE_PENDING_BYTES] =
    INKTAG_LIFECYCLE_CONTENT_PENDING_MAGIC;

inktag_lifecycle_pending_scope_t inktag_lifecycle_pending_decode(
    const uint8_t *wire, uint16_t length) {
    if (!wire || length != INKTAG_LIFECYCLE_PENDING_BYTES)
        return INKTAG_LIFECYCLE_PENDING_INVALID;
    if (!memcmp(wire, factory, sizeof(factory)))
        return INKTAG_LIFECYCLE_PENDING_FACTORY_RESET;
    if (!memcmp(wire, content, sizeof(content)))
        return INKTAG_LIFECYCLE_PENDING_CLEAR_USER_DATA;
    return INKTAG_LIFECYCLE_PENDING_INVALID;
}

bool inktag_lifecycle_pending_encode(inktag_lifecycle_pending_scope_t scope,
    uint8_t *wire, uint16_t capacity) {
    if (!wire || capacity < INKTAG_LIFECYCLE_PENDING_BYTES) return false;
    const uint8_t *source = scope == INKTAG_LIFECYCLE_PENDING_FACTORY_RESET ?
        factory : scope == INKTAG_LIFECYCLE_PENDING_CLEAR_USER_DATA ? content : NULL;
    if (!source) {
        memset(wire, 0, INKTAG_LIFECYCLE_PENDING_BYTES);
        return false;
    }
    memcpy(wire, source, INKTAG_LIFECYCLE_PENDING_BYTES);
    return true;
}

static bool io_valid(const inktag_lifecycle_pending_io_t *io) {
    return io && io->read && io->write && io->remove && io->lock &&
        io->clear && io->verify;
}

static inktag_lifecycle_pending_read_t load(
    const inktag_lifecycle_pending_io_t *io,
    inktag_lifecycle_pending_scope_t *scope) {
    uint8_t wire[INKTAG_LIFECYCLE_PENDING_BYTES] = {0};
    uint16_t length = 0u;
    inktag_lifecycle_pending_read_t result = io->read(io->context, wire, &length);
    *scope = result == INKTAG_LIFECYCLE_PENDING_READ_FOUND ?
        inktag_lifecycle_pending_decode(wire, length) :
        INKTAG_LIFECYCLE_PENDING_INVALID;
    return result;
}

static inktag_lifecycle_pending_result_t finish(
    const inktag_lifecycle_pending_io_t *io,
    inktag_lifecycle_pending_scope_t scope) {
    inktag_lifecycle_pending_scope_t remaining;
    io->lock(io->context, scope);
    if (scope == INKTAG_LIFECYCLE_PENDING_INVALID ||
        !io->clear(io->context, scope) ||
        !io->verify(io->context, scope))
        return INKTAG_LIFECYCLE_PENDING_RESULT_ERROR;
    /* A lost delete ACK can still mean that the marker is absent. Only the
     * subsequent supported-store read decides completion. */
    (void)io->remove(io->context);
    return load(io, &remaining) == INKTAG_LIFECYCLE_PENDING_READ_MISSING ?
        INKTAG_LIFECYCLE_PENDING_RESULT_DONE :
        INKTAG_LIFECYCLE_PENDING_RESULT_ERROR;
}

inktag_lifecycle_pending_result_t inktag_lifecycle_pending_resume(
    const inktag_lifecycle_pending_io_t *io) {
    inktag_lifecycle_pending_scope_t scope;
    inktag_lifecycle_pending_read_t result;
    if (!io_valid(io)) return INKTAG_LIFECYCLE_PENDING_RESULT_ERROR;
    result = load(io, &scope);
    if (result == INKTAG_LIFECYCLE_PENDING_READ_MISSING)
        return INKTAG_LIFECYCLE_PENDING_RESULT_NONE;
    if (result != INKTAG_LIFECYCLE_PENDING_READ_FOUND) {
        io->lock(io->context, INKTAG_LIFECYCLE_PENDING_INVALID);
        return INKTAG_LIFECYCLE_PENDING_RESULT_ERROR;
    }
    return finish(io, scope);
}

inktag_lifecycle_pending_result_t inktag_lifecycle_pending_begin(
    const inktag_lifecycle_pending_io_t *io,
    inktag_lifecycle_pending_scope_t scope) {
    inktag_lifecycle_pending_scope_t saved;
    inktag_lifecycle_pending_read_t result;
    uint8_t wire[INKTAG_LIFECYCLE_PENDING_BYTES];
    if (!io_valid(io) || !inktag_lifecycle_pending_encode(
            scope, wire, sizeof(wire)))
        return INKTAG_LIFECYCLE_PENDING_RESULT_ERROR;
    result = load(io, &saved);
    if (result == INKTAG_LIFECYCLE_PENDING_READ_ERROR) {
        io->lock(io->context, INKTAG_LIFECYCLE_PENDING_INVALID);
        return INKTAG_LIFECYCLE_PENDING_RESULT_ERROR;
    }
    if (result == INKTAG_LIFECYCLE_PENDING_READ_FOUND) {
        if (saved != scope) {
            io->lock(io->context, saved);
            return saved == INKTAG_LIFECYCLE_PENDING_INVALID ?
                INKTAG_LIFECYCLE_PENDING_RESULT_ERROR :
                INKTAG_LIFECYCLE_PENDING_RESULT_CONFLICT;
        }
        return finish(io, scope);
    }
    if (result != INKTAG_LIFECYCLE_PENDING_READ_MISSING) {
        io->lock(io->context, INKTAG_LIFECYCLE_PENDING_INVALID);
        return INKTAG_LIFECYCLE_PENDING_RESULT_ERROR;
    }
    /* Publish-before-delete. Whether the write ACK was lost or not, trust
     * only a fresh read of the complete matching durable marker. */
    (void)io->write(io->context, wire);
    result = load(io, &saved);
    if (result != INKTAG_LIFECYCLE_PENDING_READ_FOUND) {
        io->lock(io->context, INKTAG_LIFECYCLE_PENDING_INVALID);
        return INKTAG_LIFECYCLE_PENDING_RESULT_ERROR;
    }
    if (saved != scope) {
        io->lock(io->context, saved);
        return INKTAG_LIFECYCLE_PENDING_RESULT_ERROR;
    }
    return finish(io, scope);
}
