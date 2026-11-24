#include "inktag_gr_image_store.h"
#include "inktag_gr_image_io.h"
#include "inktag_gr_flash_layout.h"
#include "inktag_gr_cooperative.h"
#include "inktag_resource_name.h"
#include <string.h>

/* Bus waits may service BLE callbacks. Defer disconnect/lease abort until the
 * common call has returned. Snapshots return BUSY, never recursively scan. */
static bool s_call_active, s_abort_pending;
static inktag_gr_image_io_result_t s_io_result = INKTAG_GR_IMAGE_IO_UNAVAILABLE;
static uint16_t s_error_sector = UINT16_MAX;

static inktag_image_store_result_t io_result(inktag_gr_image_io_result_t result) {
    switch (result) {
        case INKTAG_GR_IMAGE_IO_OK: return INKTAG_IMAGE_STORE_OK;
        case INKTAG_GR_IMAGE_IO_UNAVAILABLE: return INKTAG_IMAGE_STORE_UNSUPPORTED;
        case INKTAG_GR_IMAGE_IO_LOW_POWER: return INKTAG_IMAGE_STORE_LOW_POWER;
        case INKTAG_GR_IMAGE_IO_RESERVED: return INKTAG_IMAGE_STORE_BUSY;
        default: return INKTAG_IMAGE_STORE_IO_ERROR;
    }
}
static bool fixed_read(uint32_t address, void *data, uint16_t length) {
    inktag_gr_image_io_result_t result = inktag_gr_image_io_read(address, data, length);
    if (result != INKTAG_GR_IMAGE_IO_OK)
        s_error_sector = (uint16_t)((address - INKTAG_GR_IMAGE_STORE_START) / 4096u);
    return result == INKTAG_GR_IMAGE_IO_OK;
}
static inktag_image_store_result_t fixed_mutate(
    uint32_t address, const void *data, uint16_t length) {
    /* Do not perform the next header/magic write after a callback cancels. */
    if (s_abort_pending) return INKTAG_IMAGE_STORE_BUSY;
    inktag_gr_image_io_result_t result = data
        ? inktag_gr_image_io_program(address, data, length)
        : inktag_gr_image_io_erase(address);
    if (result != INKTAG_GR_IMAGE_IO_OK)
        s_error_sector = (uint16_t)((address - INKTAG_GR_IMAGE_STORE_START) / 4096u);
    return io_result(result);
}

#define INKTAG_FIXED_STORE_SECTORS (INKTAG_GR_IMAGE_STORE_SIZE / 4096u)
#define INKTAG_FIXED_STORE_RUNTIME_SECTORS inktag_gr_image_io_sector_count()
#define INKTAG_FIXED_STORE_CAPACITY inktag_gr_image_io_slot_count
#define INKTAG_FIXED_STORE_ADDRESS inktag_gr_image_io_slot_address
#define INKTAG_FIXED_STORE_READ fixed_read
#define INKTAG_FIXED_STORE_MUTATE fixed_mutate
/* Every mutate independently forces a fresh voltage check in the IO backend. */
#define INKTAG_FIXED_STORE_POWER_ALLOWED() inktag_gr_image_io_power_allowed()
#define INKTAG_FIXED_STORE_CONFIG_READ inktag_gr_image_store_config_read
/* SDK call-boundary guards only; all allocation/validation is the shared core. */
#define inktag_image_store_init fixed_init
#define inktag_image_store_process fixed_process
#define inktag_image_store_get fixed_get
#define inktag_image_store_status fixed_status
#define inktag_image_store_begin fixed_begin
#define inktag_image_store_append fixed_append
#define inktag_image_store_commit fixed_commit
#define inktag_image_store_read fixed_payload_read
#define inktag_image_store_delete fixed_delete
#define inktag_image_store_clear_all fixed_clear_all
#define inktag_image_store_clear_incompatible fixed_clear_incompatible
#define inktag_image_store_clear_selected fixed_clear_selected
#define inktag_image_store_abort fixed_abort
#define inktag_image_store_prepare_profile fixed_prepare_profile
#define inktag_image_store_publish_profile fixed_publish_profile
#define inktag_image_store_invalidate fixed_invalidate
#include "inktag_image_fixed_store.inc"
#undef inktag_image_store_init
#undef inktag_image_store_process
#undef inktag_image_store_get
#undef inktag_image_store_status
#undef inktag_image_store_begin
#undef inktag_image_store_append
#undef inktag_image_store_commit
#undef inktag_image_store_read
#undef inktag_image_store_delete
#undef inktag_image_store_clear_all
#undef inktag_image_store_clear_incompatible
#undef inktag_image_store_clear_selected
#undef inktag_image_store_abort
#undef inktag_image_store_prepare_profile
#undef inktag_image_store_publish_profile
#undef inktag_image_store_invalidate

static inktag_image_store_result_t call_end(inktag_image_store_result_t result) {
    s_call_active = false;
    if (s_abort_pending) {
        s_abort_pending = false;
        fixed_abort();
        inktag_image_store_event_t ignored;
        (void)inktag_image_store_take_event(&ignored);
        result = INKTAG_IMAGE_STORE_BUSY;
    }
    return result;
}
#define STORE_CALL(expression) do { \
    if (s_call_active) return INKTAG_IMAGE_STORE_BUSY; \
    if (s_io_result != INKTAG_GR_IMAGE_IO_OK) return io_result(s_io_result); \
    s_call_active = true; \
    return call_end(expression); \
} while (0)

bool inktag_image_store_init(void) {
    if (s_call_active || inktag_image_store_busy()) return false;
    s_abort_pending = false; s_error_sector = UINT16_MAX;
    s_call_active = true;
    s_io_result = inktag_gr_image_io_open();
    bool ok = fixed_init();
    return call_end(ok ? INKTAG_IMAGE_STORE_OK : io_result(s_io_result)) ==
               INKTAG_IMAGE_STORE_OK && ok;
}
void inktag_image_store_process(void) {
    if (s_call_active) return;
    s_call_active = true;
    fixed_process();
    (void)call_end(INKTAG_IMAGE_STORE_OK);
}
inktag_image_store_result_t inktag_image_store_get(uint8_t id, inktag_image_store_header_t *h) {
    STORE_CALL(fixed_get(id, h));
}
inktag_image_store_result_t inktag_image_store_prepare_profile(
    const epd_screen_config_t *screen, inktag_image_store_profile_view_t *view) {
    STORE_CALL(fixed_prepare_profile(screen, view));
}
void inktag_image_store_publish_profile(const inktag_image_store_profile_view_t *view) {
    fixed_publish_profile(view);
}
void inktag_image_store_invalidate(void) { fixed_invalidate(); }
inktag_image_store_result_t inktag_image_store_status(
    const epd_screen_config_t *screen, uint32_t payload, inktag_image_store_status_t *out) {
    STORE_CALL(fixed_status(screen, payload, out));
}
inktag_image_store_result_t inktag_image_store_begin(
    const epd_screen_config_t *screen, uint8_t format, uint8_t segments,
    uint8_t black, uint8_t type, const uint32_t sizes[2], uint32_t payload,
    uint32_t checksum, uint32_t created, const char *name, uint8_t *id) {
    STORE_CALL(fixed_begin(screen, format, segments, black, type, sizes,
                           payload, checksum, created, name, id));
}
inktag_image_store_result_t inktag_image_store_append(const uint8_t *data, uint16_t length) {
    STORE_CALL(fixed_append(data, length));
}
inktag_image_store_result_t inktag_image_store_commit(void) { STORE_CALL(fixed_commit()); }
inktag_image_store_result_t inktag_image_store_read(
    uint8_t id, uint32_t offset, uint8_t *data, uint16_t length) {
    STORE_CALL(fixed_payload_read(id, offset, data, length));
}
inktag_image_store_result_t inktag_image_store_delete(uint8_t id) { STORE_CALL(fixed_delete(id)); }
inktag_image_store_result_t inktag_image_store_clear_all(void) { STORE_CALL(fixed_clear_all()); }
inktag_image_store_result_t inktag_image_store_clear_incompatible(const epd_screen_config_t *s) {
    STORE_CALL(fixed_clear_incompatible(s));
}
inktag_image_store_result_t inktag_image_store_clear_selected(
    const uint8_t *ids, uint8_t count, bool incompatible_operation) {
    STORE_CALL(fixed_clear_selected(ids, count, incompatible_operation));
}
void inktag_image_store_abort(void) {
    if (s_call_active) { s_abort_pending = true; return; }
    fixed_abort();
    inktag_image_store_event_t ignored;
    (void)inktag_image_store_take_event(&ignored);
}

inktag_image_store_result_t inktag_gr_image_store_wait(
    inktag_image_store_result_t admission, inktag_image_store_event_type_t expected,
    uint16_t *removed_out) {
    if (admission != INKTAG_IMAGE_STORE_OK) {
        /* Commit publishes a terminal event even when header programming or
         * readback fails. Release it before the next command; admission errors
         * from begin/clear do not own any completion event. */
        if (expected == INKTAG_IMAGE_STORE_EVENT_COMMIT) {
            inktag_image_store_event_t ignored;
            (void)inktag_image_store_take_event(&ignored);
        }
        return admission;
    }
    for (unsigned n = 0; n <= INKTAG_FIXED_STORE_SECTORS + 1u; ++n) {
        inktag_image_store_event_t completion;
        if (inktag_image_store_take_event(&completion)) {
            if (completion.type != expected) return INKTAG_IMAGE_STORE_BUSY;
            if (removed_out) *removed_out = completion.removed;
            return completion.result;
        }
        if (!inktag_image_store_busy()) return INKTAG_IMAGE_STORE_BUSY;
        inktag_image_store_process();
    }
    inktag_image_store_abort();
    return INKTAG_IMAGE_STORE_IO_ERROR;
}
inktag_image_store_result_t inktag_image_store_begin_replace(
    const epd_screen_config_t *s, uint8_t f, uint8_t segments, uint8_t black,
    uint8_t type, const uint32_t sizes[2], uint32_t payload, uint32_t checksum,
    uint32_t created, const char *name, uint8_t requested, uint8_t *id) {
    if (requested != UINT8_MAX && !inktag_image_store_replace_slot(requested))
        return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
    return inktag_gr_image_store_wait(inktag_image_store_begin(s, f, segments,
        black, type, sizes, payload, checksum, created, name, id),
        INKTAG_IMAGE_STORE_EVENT_BEGIN, NULL);
}
inktag_image_store_result_t inktag_image_store_next(
    uint8_t after, uint8_t *id, inktag_image_store_header_t *h) {
    if (!id || !h) return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
    uint8_t bitmap[32];
    inktag_image_store_result_t result = inktag_image_store_occupancy(bitmap, sizeof(bitmap));
    if (result != INKTAG_IMAGE_STORE_OK) return result;
    uint16_t count = inktag_image_store_fixed_slot_count();
    for (uint16_t n = after == UINT8_MAX ? 0u : (uint16_t)after + 1u; n < count; ++n) {
        if (!(bitmap[n >> 3] & (1u << (n & 7u)))) continue;
        result = inktag_image_store_get((uint8_t)n, h);
        if (result == INKTAG_IMAGE_STORE_NOT_FOUND) continue;
        if (result == INKTAG_IMAGE_STORE_OK) *id = (uint8_t)n;
        return result;
    }
    return INKTAG_IMAGE_STORE_NOT_FOUND;
}
bool inktag_image_store_resolve_name(const uint8_t *text, uint16_t length, uint8_t *slot) {
    if (!text || !slot || !length || length > 35u) return false;
    uint8_t after = UINT8_MAX, id;
    inktag_image_store_header_t h;
    while (inktag_image_store_next(after, &id, &h) == INKTAG_IMAGE_STORE_OK) {
        if (inktag_resource_name_matches(text, length,
                (const uint8_t *)h.name, strlen(h.name))) { *slot = id; return true; }
        after = id;
    }
    return false;
}

void inktag_image_store_error_context(uint16_t *sector) {
    if (sector) *sector = s_error_sector;
}
