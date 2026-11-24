#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "inktag_lifecycle_pending.h"

typedef struct {
    uint8_t wire[INKTAG_LIFECYCLE_PENDING_BYTES];
    uint16_t length;
    bool present;
    bool read_error;
    bool write_drop;
    bool write_lost_ack;
    bool write_corrupt;
    bool remove_drop;
    bool remove_lost_ack;
    bool clear_fail;
    bool verify_fail;
    bool cleared;
    unsigned writes, clears, verifies, removes, locks;
    inktag_lifecycle_pending_scope_t locked;
} fake_store_t;

static inktag_lifecycle_pending_read_t fake_read(
    void *context, uint8_t wire[INKTAG_LIFECYCLE_PENDING_BYTES],
    uint16_t *length) {
    fake_store_t *store = context;
    if (store->read_error) return INKTAG_LIFECYCLE_PENDING_READ_ERROR;
    if (!store->present) return INKTAG_LIFECYCLE_PENDING_READ_MISSING;
    *length = store->length;
    memcpy(wire, store->wire, store->length < INKTAG_LIFECYCLE_PENDING_BYTES ?
           store->length : INKTAG_LIFECYCLE_PENDING_BYTES);
    return INKTAG_LIFECYCLE_PENDING_READ_FOUND;
}

static bool fake_write(void *context,
    const uint8_t wire[INKTAG_LIFECYCLE_PENDING_BYTES]) {
    fake_store_t *store = context;
    ++store->writes;
    if (store->write_drop) return false;
    memcpy(store->wire, wire, sizeof(store->wire));
    if (store->write_corrupt) store->wire[0] ^= 1u;
    store->length = INKTAG_LIFECYCLE_PENDING_BYTES;
    store->present = true;
    return !store->write_lost_ack;
}

static bool fake_remove(void *context) {
    fake_store_t *store = context;
    ++store->removes;
    if (store->remove_drop) return false;
    store->present = false;
    return !store->remove_lost_ack;
}

static void fake_lock(void *context, inktag_lifecycle_pending_scope_t scope) {
    fake_store_t *store = context;
    store->locked = scope;
    ++store->locks;
}

static bool fake_clear(void *context, inktag_lifecycle_pending_scope_t scope) {
    fake_store_t *store = context;
    ++store->clears;
    if (!store->present || store->locked != scope || store->clear_fail)
        return false;
    store->cleared = true;
    return true;
}

static bool fake_verify(void *context, inktag_lifecycle_pending_scope_t scope) {
    fake_store_t *store = context;
    ++store->verifies;
    return store->present && store->cleared && store->locked == scope &&
        !store->verify_fail;
}

static inktag_lifecycle_pending_io_t fake_io(fake_store_t *store) {
    inktag_lifecycle_pending_io_t io = {
        store, fake_read, fake_write, fake_remove,
        fake_lock, fake_clear, fake_verify,
    };
    return io;
}

static void test_coordinator(void) {
    fake_store_t store = {0};
    inktag_lifecycle_pending_io_t io = fake_io(&store);
    const inktag_lifecycle_pending_scope_t content =
        INKTAG_LIFECYCLE_PENDING_CLEAR_USER_DATA;
    assert(inktag_lifecycle_pending_resume(&io) ==
           INKTAG_LIFECYCLE_PENDING_RESULT_NONE);
    assert(inktag_lifecycle_pending_begin(&io, content) ==
           INKTAG_LIFECYCLE_PENDING_RESULT_DONE);
    assert(!store.present && store.writes == 1u && store.clears == 1u &&
           store.verifies == 1u && store.removes == 1u);

    memset(&store, 0, sizeof(store));
    store.write_lost_ack = true;
    assert(inktag_lifecycle_pending_begin(&io, content) ==
           INKTAG_LIFECYCLE_PENDING_RESULT_DONE);
    assert(!store.present && store.clears == 1u);

    memset(&store, 0, sizeof(store));
    store.write_drop = true;
    assert(inktag_lifecycle_pending_begin(&io, content) ==
           INKTAG_LIFECYCLE_PENDING_RESULT_ERROR);
    assert(!store.present && store.clears == 0u && store.locks == 1u &&
           store.locked == INKTAG_LIFECYCLE_PENDING_INVALID);

    memset(&store, 0, sizeof(store));
    store.write_corrupt = true;
    assert(inktag_lifecycle_pending_begin(&io, content) ==
           INKTAG_LIFECYCLE_PENDING_RESULT_ERROR);
    assert(store.present && store.clears == 0u &&
           store.locked == INKTAG_LIFECYCLE_PENDING_INVALID);

    memset(&store, 0, sizeof(store));
    store.read_error = true;
    assert(inktag_lifecycle_pending_begin(&io, content) ==
           INKTAG_LIFECYCLE_PENDING_RESULT_ERROR);
    assert(store.writes == 0u && store.clears == 0u && store.locks == 1u &&
           store.locked == INKTAG_LIFECYCLE_PENDING_INVALID);
    assert(inktag_lifecycle_pending_resume(&io) ==
           INKTAG_LIFECYCLE_PENDING_RESULT_ERROR);

    memset(&store, 0, sizeof(store));
    store.clear_fail = true;
    assert(inktag_lifecycle_pending_begin(&io, content) ==
           INKTAG_LIFECYCLE_PENDING_RESULT_ERROR);
    assert(store.present && store.verifies == 0u && store.removes == 0u);
    store.clear_fail = false;
    assert(inktag_lifecycle_pending_resume(&io) ==
           INKTAG_LIFECYCLE_PENDING_RESULT_DONE);
    assert(!store.present && store.writes == 1u && store.clears == 2u);

    memset(&store, 0, sizeof(store));
    store.verify_fail = true;
    assert(inktag_lifecycle_pending_begin(&io, content) ==
           INKTAG_LIFECYCLE_PENDING_RESULT_ERROR);
    assert(store.present && store.removes == 0u);
    store.verify_fail = false;
    assert(inktag_lifecycle_pending_resume(&io) ==
           INKTAG_LIFECYCLE_PENDING_RESULT_DONE);

    memset(&store, 0, sizeof(store));
    store.remove_drop = true;
    assert(inktag_lifecycle_pending_begin(&io, content) ==
           INKTAG_LIFECYCLE_PENDING_RESULT_ERROR);
    assert(store.present);
    store.remove_drop = false;
    assert(inktag_lifecycle_pending_resume(&io) ==
           INKTAG_LIFECYCLE_PENDING_RESULT_DONE);

    memset(&store, 0, sizeof(store));
    store.remove_lost_ack = true;
    assert(inktag_lifecycle_pending_begin(&io, content) ==
           INKTAG_LIFECYCLE_PENDING_RESULT_DONE);
    assert(!store.present);

    memset(&store, 0, sizeof(store));
    assert(inktag_lifecycle_pending_encode(
        INKTAG_LIFECYCLE_PENDING_FACTORY_RESET,
        store.wire, sizeof(store.wire)));
    store.present = true;
    store.length = sizeof(store.wire);
    assert(inktag_lifecycle_pending_begin(&io, content) ==
           INKTAG_LIFECYCLE_PENDING_RESULT_CONFLICT);
    assert(store.locked == INKTAG_LIFECYCLE_PENDING_FACTORY_RESET &&
           store.writes == 0u && store.clears == 0u);
    assert(inktag_lifecycle_pending_resume(&io) ==
           INKTAG_LIFECYCLE_PENDING_RESULT_DONE);

    memset(&store, 0, sizeof(store));
    memset(store.wire, 0xff, sizeof(store.wire));
    store.present = true;
    store.length = sizeof(store.wire);
    assert(inktag_lifecycle_pending_resume(&io) ==
           INKTAG_LIFECYCLE_PENDING_RESULT_ERROR);
    assert(store.locked == INKTAG_LIFECYCLE_PENDING_INVALID &&
           store.clears == 0u && store.removes == 0u);
}

int main(void) {
    const uint8_t markers[2][8] = {
        INKTAG_SETTINGS_OWNER_RESET_PENDING_MAGIC,
        INKTAG_LIFECYCLE_CONTENT_PENDING_MAGIC,
    };
    uint8_t wire[9];
    assert(INKTAG_LIFECYCLE_PENDING_RECORD == 32);
    assert(INKTAG_SETTINGS_RECORD_MAX == 32);
    assert(inktag_settings_record_lifecycle(32) == INKTAG_SETTINGS_LIFECYCLE_PROTECTED);
    for (unsigned i = 0; i < 2; ++i) {
        inktag_lifecycle_pending_scope_t scope = i ?
            INKTAG_LIFECYCLE_PENDING_CLEAR_USER_DATA : INKTAG_LIFECYCLE_PENDING_FACTORY_RESET;
        memset(wire, 0xcc, sizeof(wire));
        assert(inktag_lifecycle_pending_encode(scope, wire, sizeof(wire)));
        assert(!memcmp(wire, markers[i], 8) && wire[8] == 0xcc);
        assert(inktag_lifecycle_pending_decode(wire, 8) == scope);
        for (unsigned n = 0; n < 8; ++n) {
            assert(inktag_lifecycle_pending_decode(wire, n) == INKTAG_LIFECYCLE_PENDING_INVALID);
            uint8_t torn[8] = {0};
            memcpy(torn, markers[i], n);
            assert(inktag_lifecycle_pending_decode(torn, 8) == INKTAG_LIFECYCLE_PENDING_INVALID);
            memset(torn, 0xff, sizeof(torn));
            memcpy(torn, markers[i], n);
            assert(inktag_lifecycle_pending_decode(torn, 8) == INKTAG_LIFECYCLE_PENDING_INVALID);
        }
        for (unsigned byte = 0; byte < 8; ++byte) for (unsigned bit = 0; bit < 8; ++bit) {
            uint8_t damaged[8];
            memcpy(damaged, markers[i], 8); damaged[byte] ^= (uint8_t)(1u << bit);
            assert(inktag_lifecycle_pending_decode(damaged, 8) == INKTAG_LIFECYCLE_PENDING_INVALID);
        }
        assert(inktag_lifecycle_pending_decode(wire, 9) == INKTAG_LIFECYCLE_PENDING_INVALID);
    }
    assert(inktag_lifecycle_pending_decode(NULL, 8) == INKTAG_LIFECYCLE_PENDING_INVALID);
    assert(inktag_lifecycle_pending_decode(NULL, 0) == INKTAG_LIFECYCLE_PENDING_INVALID);
    assert(!inktag_lifecycle_pending_encode(INKTAG_LIFECYCLE_PENDING_FACTORY_RESET, NULL, 8));
    memset(wire, 0xcc, sizeof(wire));
    assert(!inktag_lifecycle_pending_encode(INKTAG_LIFECYCLE_PENDING_FACTORY_RESET, wire, 7));
    for (unsigned i = 0; i < sizeof(wire); ++i) assert(wire[i] == 0xcc);
    assert(!inktag_lifecycle_pending_encode((inktag_lifecycle_pending_scope_t)99, wire, 9));
    for (unsigned i = 0; i < 8; ++i) assert(!wire[i]);
    assert(wire[8] == 0xcc);
    assert(inktag_settings_record_disposition(32, INKTAG_SETTINGS_CLEAR_USER_DATA) == INKTAG_SETTINGS_RESET_KEEP);
    assert(inktag_settings_record_disposition(32, INKTAG_SETTINGS_CLEAR_OWNER_FACTORY_RESET) == INKTAG_SETTINGS_RESET_KEEP);
    test_coordinator();
    puts("ID32 lifecycle scope codec and begin/resume fault ordering passed");
    return 0;
}
