/* Fixed release-v1 image-slot engine; physical allocator from nRF52811 d3186c0c.
 * Include once in a vendor translation unit after its compile-time IO bindings.
 * No heap, runtime callback table, persistent index or old-format migration.
 * Each accepted physical extent contributes floor(sectors / stride) separately.
 */
#include "inktag_image_fixed_store.h"
#include "inktag_protocol_core.h"
#include "inktag_resource_name.h"
#include <stddef.h>
#include <string.h>

#if !defined(INKTAG_FIXED_STORE_SECTORS) || !defined(INKTAG_FIXED_STORE_CAPACITY) || \
    !defined(INKTAG_FIXED_STORE_ADDRESS) || !defined(INKTAG_FIXED_STORE_READ) || \
    !defined(INKTAG_FIXED_STORE_MUTATE) || !defined(INKTAG_FIXED_STORE_POWER_ALLOWED) || \
    !defined(INKTAG_FIXED_STORE_CONFIG_READ)
#error "fixed store requires explicit compile-time physical IO/extent bindings"
#endif
#ifndef INKTAG_FIXED_STORE_SECTOR_SIZE
#define INKTAG_FIXED_STORE_SECTOR_SIZE 4096u
#endif
#define SECTORS INKTAG_FIXED_STORE_SECTORS
#ifndef INKTAG_FIXED_STORE_RUNTIME_SECTORS
#define INKTAG_FIXED_STORE_RUNTIME_SECTORS SECTORS
#endif
#define INKTAG_FIXED_STORE_MAX_SLOTS (SECTORS < 255u ? SECTORS : 255u)
#define MAGIC INKTAG_RELEASE_IMAGE_MAGIC
#define VERSION 1u
/* Ports with retention sleep place every long-lived state item in qualified RAM. */
#ifndef INKTAG_FIXED_STORE_RAM
#define INKTAG_FIXED_STORE_RAM
#endif
/* Use the existing common UTF-8/reserved-word vocabulary, not another parser.
 * Fixed-image adapters compile it with the frozen 35-byte image-name limit. */
#ifndef INKTAG_FIXED_STORE_NAME_VALID
#define INKTAG_FIXED_STORE_NAME_VALID inktag_nfc_resource_alias_valid
#endif
#ifndef INKTAG_FIXED_STORE_NAME_MATCH
#define INKTAG_FIXED_STORE_NAME_MATCH inktag_nfc_resource_alias_matches
#endif
/* File-scope C99 checks also work with the older qualified TLSR compiler. */
typedef char inktag_fixed_sector_check[INKTAG_FIXED_STORE_SECTOR_SIZE >= 96u ? 1 : -1];
typedef char inktag_fixed_extent_check[SECTORS > 0u && SECTORS <= UINT16_MAX ? 1 : -1];

static INKTAG_FIXED_STORE_RAM uint8_t valid[(INKTAG_FIXED_STORE_MAX_SLOTS + 7u) / 8u], clearing[(INKTAG_FIXED_STORE_MAX_SLOTS + 7u) / 8u];
static INKTAG_FIXED_STORE_RAM uint16_t stride, erase_next;
static INKTAG_FIXED_STORE_RAM uint8_t slots, target = UINT8_MAX, current, phase;
static INKTAG_FIXED_STORE_RAM uint16_t cursor, removed;
static INKTAG_FIXED_STORE_RAM uint32_t frame, received, running_crc;
static INKTAG_FIXED_STORE_RAM bool ready, pending;
static INKTAG_FIXED_STORE_RAM inktag_image_store_header_t capture;
static INKTAG_FIXED_STORE_RAM inktag_image_store_event_t event;
static INKTAG_FIXED_STORE_RAM inktag_image_store_result_t init_result = INKTAG_IMAGE_STORE_UNSUPPORTED;
enum { IDLE, ERASE, CAPTURE, CLEAR };
typedef char inktag_fixed_header_check[sizeof(capture) == 96 ? 1 : -1];

static bool bit(const uint8_t *map, unsigned id) { return (map[id >> 3] & (1u << (id & 7))) != 0; }
static void mark(uint8_t *map, unsigned id, bool value) {
    uint8_t mask = 1u << (id & 7);
    if (value) map[id >> 3] |= mask; else map[id >> 3] &= (uint8_t)~mask;
}
static uint32_t address(unsigned id) { return INKTAG_FIXED_STORE_ADDRESS(id, stride); }
static uint32_t crc(const void *data, uint16_t length) {
    return inktag_crc32_update(UINT32_MAX, data, length) ^ UINT32_MAX;
}
static uint32_t header_crc(const inktag_image_store_header_t *header) {
    return crc((const uint8_t *)header + 4, sizeof(*header) - 8);
}
static bool header_valid(const inktag_image_store_header_t *h) {
    epd_screen_config_t screen;
    return h->magic == MAGIC && h->version == VERSION && h->header_size == 96 &&
        h->record_sectors == stride && h->header_crc == header_crc(h) &&
        memchr(h->name, 0, sizeof(h->name)) && h->content_type <= INKTAG_IMAGE_CONTENT_MAX &&
        h->segment_count && h->segment_count <= 2 && h->payload_size &&
        h->payload_size <= stride * INKTAG_FIXED_STORE_SECTOR_SIZE - 96 &&
        h->segment_size[0] <= h->payload_size &&
        h->segment_size[1] == h->payload_size - h->segment_size[0] &&
        (h->segment_count == 2 || h->segment_size[1] == 0) &&
        epd_screen_config_decode_exact(h->screen_config, EPD_SCREEN_WIRE_SIZE, &screen) == EPD_SCREEN_STATUS_OK;
}
static void finish(inktag_image_store_event_type_t type, inktag_image_store_result_t result) {
    phase = IDLE;
    event = (inktag_image_store_event_t){type, result, current, removed}; pending = true;
}
bool inktag_image_store_init(void) {
    epd_screen_config_t configured;
    epd_screen_t screen;
    ready = false; phase = IDLE; pending = false; target = UINT8_MAX; slots = 0;
    init_result = INKTAG_IMAGE_STORE_UNSUPPORTED;
    if (!INKTAG_FIXED_STORE_CONFIG_READ(&configured)) return false;
    if (epd_screen_resolve(&configured, &screen) != EPD_SCREEN_STATUS_OK) return false;
    frame = screen.frame_bytes;
    if (!frame || frame > UINT32_MAX - 96u - (INKTAG_FIXED_STORE_SECTOR_SIZE - 1u)) return false;
    uint32_t step = (frame + 96u + INKTAG_FIXED_STORE_SECTOR_SIZE - 1u) / INKTAG_FIXED_STORE_SECTOR_SIZE;
    if (!step || step > UINT16_MAX) return false;
    stride = (uint16_t)step;
    uint32_t capacity = INKTAG_FIXED_STORE_CAPACITY(stride);
    uint16_t physical_sectors = inktag_image_store_sector_count();
    if (!physical_sectors || capacity > physical_sectors / stride) return false;
    slots = capacity > 255u ? 255u : (uint8_t)capacity;
    if (!slots) { init_result = INKTAG_IMAGE_STORE_NO_SPACE; return false; }
    memset(valid, 0, sizeof(valid));
    for (unsigned id = 0; id < slots; id++) {
        inktag_image_store_header_t header;
        if (!INKTAG_FIXED_STORE_READ(address(id), &header, sizeof(header))) {
            init_result = INKTAG_IMAGE_STORE_IO_ERROR; return false;
        }
        if (header_valid(&header)) mark(valid, id, true);
    }
    ready = true; init_result = INKTAG_IMAGE_STORE_OK; return true;
}
bool inktag_image_store_supported(void) { return ready; }
bool inktag_image_store_busy(void) { return phase != IDLE; }
bool inktag_image_store_capture_active(void) { return phase == CAPTURE || phase == ERASE; }
uint16_t inktag_image_store_sector_count(void) {
    uint32_t count = INKTAG_FIXED_STORE_RUNTIME_SECTORS;
    return count <= SECTORS ? (uint16_t)count : 0u;
}
uint16_t inktag_image_store_fixed_slot_count(void) { return ready ? slots : 0u; }
inktag_image_store_result_t inktag_image_store_occupancy(uint8_t *bitmap, uint8_t capacity) {
    if (!ready) return init_result;
    if (phase != IDLE) return INKTAG_IMAGE_STORE_BUSY;
    unsigned bytes = (slots + 7u) / 8u;
    if (!bitmap || capacity < bytes) return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
    memcpy(bitmap, valid, bytes);
    if (slots & 7u) bitmap[bytes - 1u] &= (uint8_t)((1u << (slots & 7u)) - 1u);
    return INKTAG_IMAGE_STORE_OK;
}
inktag_image_store_result_t inktag_image_store_ordinal(uint8_t id, uint8_t *ordinal) {
    if (!ready) return init_result;
    if (phase != IDLE) return INKTAG_IMAGE_STORE_BUSY;
    if (!ordinal) return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
    if (id >= slots || !bit(valid, id)) return INKTAG_IMAGE_STORE_NOT_FOUND;
    *ordinal = 0;
    for (unsigned n = 0; n < id; n++) if (bit(valid, n)) ++*ordinal;
    return INKTAG_IMAGE_STORE_OK;
}
bool inktag_image_store_take_event(inktag_image_store_event_t *out) {
    if (!out || !pending) return false;
    *out = event; pending = false; return true;
}
inktag_image_store_result_t inktag_image_store_get(uint8_t id, inktag_image_store_header_t *h) {
    if (!ready) return init_result;
    if (phase != IDLE) return INKTAG_IMAGE_STORE_BUSY;
    if (!h) return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
    if (id >= slots || !bit(valid, id)) return INKTAG_IMAGE_STORE_NOT_FOUND;
    if (!INKTAG_FIXED_STORE_READ(address(id), h, sizeof(*h))) return INKTAG_IMAGE_STORE_IO_ERROR;
    if (header_valid(h)) return INKTAG_IMAGE_STORE_OK;
    mark(valid, id, false); return INKTAG_IMAGE_STORE_NOT_FOUND;
}
static bool compatible(const inktag_image_store_header_t *h, const epd_screen_config_t *screen) {
    uint8_t wire[EPD_SCREEN_WIRE_SIZE];
    return screen && epd_screen_config_encode(screen, wire) == EPD_SCREEN_STATUS_OK &&
        epd_screen_wire_image_compatible(h->screen_config, wire);
}
inktag_image_store_result_t inktag_image_store_status(const epd_screen_config_t *screen,
    uint32_t payload, inktag_image_store_status_t *out) {
    if (!ready) return init_result;
    if (!out) return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
    if (phase != IDLE) return INKTAG_IMAGE_STORE_BUSY;
    memset(out, 0, sizeof(*out)); out->total_sectors = inktag_image_store_sector_count();
    if (payload > UINT32_MAX - 96u - (INKTAG_FIXED_STORE_SECTOR_SIZE - 1u))
        return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
    uint32_t required = (payload + 96u + INKTAG_FIXED_STORE_SECTOR_SIZE - 1u) / INKTAG_FIXED_STORE_SECTOR_SIZE;
    if (required > UINT16_MAX) return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
    out->required_sectors = (uint16_t)required;
    for (unsigned id = 0; id < slots; id++) {
        if (!bit(valid, id)) {
            out->free_sectors += stride;
            out->largest_free_run = stride; /* Fixed slots cannot pool holes into larger frames. */
        } else {
            inktag_image_store_header_t h;
            inktag_image_store_result_t result = inktag_image_store_get(id, &h);
            if (result == INKTAG_IMAGE_STORE_NOT_FOUND) {
                out->free_sectors += stride; out->largest_free_run = stride;
                continue;
            }
            if (result != INKTAG_IMAGE_STORE_OK) return result;
            out->saved_count++;
            if (compatible(&h, screen)) {
                out->compatible_count++; if (h.content_type == 0) out->playable_count++;
            } else out->incompatible_count++;
        }
    }
    return INKTAG_IMAGE_STORE_OK;
}
bool inktag_image_store_replace_slot(uint8_t id) {
    if (!ready || phase != IDLE || id >= slots) return false;
    target = id; return true;
}
inktag_image_store_result_t inktag_image_store_begin(const epd_screen_config_t *screen,
    uint8_t format, uint8_t segments, uint8_t black, uint8_t type, const uint32_t sizes[2],
    uint32_t payload, uint32_t checksum, uint32_t created, const char *name, uint8_t *id) {
    uint8_t requested = target; target = UINT8_MAX;
    if (!ready) return init_result;
    if (phase != IDLE || pending) return INKTAG_IMAGE_STORE_BUSY;
    if (!INKTAG_FIXED_STORE_POWER_ALLOWED()) return INKTAG_IMAGE_STORE_LOW_POWER;
    if (!screen || !sizes || !id || payload != frame || !segments || segments > 2 ||
        type > INKTAG_IMAGE_CONTENT_MAX || sizes[0] > payload ||
        sizes[1] != payload - sizes[0] || (segments == 1 && sizes[1])) return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
    size_t length = name ? strlen(name) : 0;
    if (length >= sizeof(capture.name) || (length && !INKTAG_FIXED_STORE_NAME_VALID((const uint8_t *)name, length)))
        return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
    for (unsigned n = 0; n < slots; n++) {
        if (requested == UINT8_MAX && !bit(valid, n)) requested = n;
        if (bit(valid, n) && length) {
            inktag_image_store_header_t h;
            inktag_image_store_result_t result = inktag_image_store_get(n, &h);
            if (result == INKTAG_IMAGE_STORE_NOT_FOUND) {
                if (requested == UINT8_MAX) requested = n;
                continue;
            }
            if (result != INKTAG_IMAGE_STORE_OK) return result;
            if (n != requested && INKTAG_FIXED_STORE_NAME_MATCH((const uint8_t *)name, length,
                (const uint8_t *)h.name, strlen(h.name))) return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
        }
    }
    if (requested == UINT8_MAX) return INKTAG_IMAGE_STORE_NO_SPACE;
    memset(&capture, 0, sizeof(capture));
    capture.magic = MAGIC; capture.version = VERSION; capture.header_size = 96; capture.record_sectors = stride;
    if (epd_screen_config_encode(screen, capture.screen_config) != EPD_SCREEN_STATUS_OK) return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
    capture.format = format; capture.segment_count = segments; capture.black_mask = black; capture.content_type = type;
    memcpy(capture.segment_size, sizes, 8); capture.payload_size = payload; capture.payload_crc = checksum; capture.created_at = created;
    if (length) memcpy(capture.name, name, length);
    current = requested; *id = current; erase_next = 0; removed = 0;
    received = 0; running_crc = UINT32_MAX; phase = ERASE;
    return INKTAG_IMAGE_STORE_OK;
}
inktag_image_store_result_t inktag_image_store_append(const uint8_t *data, uint16_t length) {
    if (phase != CAPTURE || !data || !length) return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
    if (length > capture.payload_size - received) return INKTAG_IMAGE_STORE_SIZE_MISMATCH;
    inktag_image_store_result_t result = INKTAG_FIXED_STORE_MUTATE(address(current) + 96u + received, data, length);
    if (result == INKTAG_IMAGE_STORE_OK) { running_crc = inktag_crc32_update(running_crc, data, length); received += length; }
    return result;
}
static inktag_image_store_result_t commit_header(const inktag_image_store_header_t *h) {
    uint32_t addr = address(current);
    inktag_image_store_result_t result = INKTAG_FIXED_STORE_MUTATE(addr + 4, (const uint8_t *)h + 4, 92);
    if (result == INKTAG_IMAGE_STORE_OK) result = INKTAG_FIXED_STORE_MUTATE(addr, &h->magic, 4);
    inktag_image_store_header_t check;
    if (result == INKTAG_IMAGE_STORE_OK && (!INKTAG_FIXED_STORE_READ(addr, &check, 96) || !header_valid(&check))) result = INKTAG_IMAGE_STORE_IO_ERROR;
    if (result == INKTAG_IMAGE_STORE_OK) mark(valid, current, true);
    return result;
}
inktag_image_store_result_t inktag_image_store_commit(void) {
    if (phase != CAPTURE) return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
    if (received != capture.payload_size) return INKTAG_IMAGE_STORE_SIZE_MISMATCH;
    if ((running_crc ^ UINT32_MAX) != capture.payload_crc) return INKTAG_IMAGE_STORE_CRC_MISMATCH;
    capture.header_crc = header_crc(&capture);
    inktag_image_store_result_t result = commit_header(&capture);
    finish(INKTAG_IMAGE_STORE_EVENT_COMMIT, result); return result;
}
inktag_image_store_result_t inktag_image_store_read(uint8_t id, uint32_t offset, uint8_t *data, uint16_t length) {
    inktag_image_store_header_t h;
    inktag_image_store_result_t result = inktag_image_store_get(id, &h);
    if (result != INKTAG_IMAGE_STORE_OK) return result;
    if (!data || offset > h.payload_size || length > h.payload_size - offset) return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
    return INKTAG_FIXED_STORE_READ(address(id) + 96u + offset, data, length) ? INKTAG_IMAGE_STORE_OK : INKTAG_IMAGE_STORE_IO_ERROR;
}
static inktag_image_store_result_t next(uint8_t after, const epd_screen_config_t *screen, bool image_only, uint8_t *id) {
    if (!ready) return init_result;
    if (!id) return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
    for (unsigned step = 1; step <= slots; step++) {
        unsigned n = after == UINT8_MAX ? step - 1 : (after + step) % slots;
        inktag_image_store_header_t h;
        inktag_image_store_result_t result = inktag_image_store_get(n, &h);
        if (result == INKTAG_IMAGE_STORE_NOT_FOUND) continue;
        if (result != INKTAG_IMAGE_STORE_OK) return result;
        if ((!image_only || h.content_type == 0) && compatible(&h, screen)) { *id = n; return INKTAG_IMAGE_STORE_OK; }
    }
    return INKTAG_IMAGE_STORE_NOT_FOUND;
}
inktag_image_store_result_t inktag_image_store_next_compatible(uint8_t a, const epd_screen_config_t *s, uint8_t *id) { return next(a, s, true, id); }
inktag_image_store_result_t inktag_image_store_next_page(uint8_t a, const epd_screen_config_t *s, uint8_t *id) { return next(a, s, false, id); }
static inktag_image_store_result_t clear(bool all, const epd_screen_config_t *screen, uint8_t id) {
    if (!ready) return init_result;
    if (phase != IDLE || pending) return INKTAG_IMAGE_STORE_BUSY;
    if (!INKTAG_FIXED_STORE_POWER_ALLOWED()) return INKTAG_IMAGE_STORE_LOW_POWER;
    memset(clearing, 0, sizeof(clearing)); removed = 0;
    /* An explicitly requested clear-all invalidates every head in this geometry,
     * including a magic-last commit whose readback failed before RAM publication.
     * It does not erase payloads or reinterpret retired formats. */
    for (unsigned n = 0; n < slots; n++) {
        if (all) mark(clearing, n, true);
        if (!bit(valid, n) || !(all || n == id || screen)) continue;
        inktag_image_store_header_t h;
        inktag_image_store_result_t result = inktag_image_store_get(n, &h);
        if (result == INKTAG_IMAGE_STORE_NOT_FOUND) continue;
        if (result != INKTAG_IMAGE_STORE_OK) return result;
        if (!screen || !compatible(&h, screen)) { mark(clearing, n, true); removed++; }
    }
    if (!all && !screen && !removed) return INKTAG_IMAGE_STORE_NOT_FOUND;
    event.type = all ? INKTAG_IMAGE_STORE_EVENT_CLEAR_ALL : screen ? INKTAG_IMAGE_STORE_EVENT_CLEAR_INCOMPATIBLE : INKTAG_IMAGE_STORE_EVENT_DELETE;
    current = id; cursor = 0; phase = CLEAR; return INKTAG_IMAGE_STORE_OK;
}
inktag_image_store_result_t inktag_image_store_delete(uint8_t id) { return clear(false, NULL, id); }
inktag_image_store_result_t inktag_image_store_clear_all(void) { return clear(true, NULL, 0); }
inktag_image_store_result_t inktag_image_store_clear_incompatible(const epd_screen_config_t *s) { return s ? clear(false, s, 0) : INKTAG_IMAGE_STORE_BAD_ARGUMENT; }
inktag_image_store_result_t inktag_image_store_clear_selected(const uint8_t *ids, uint8_t count, bool incompatible_operation) {
    if (!ready) return init_result;
    if (phase != IDLE || pending) return INKTAG_IMAGE_STORE_BUSY;
    if (!ids && count) return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
    if (!INKTAG_FIXED_STORE_POWER_ALLOWED()) return INKTAG_IMAGE_STORE_LOW_POWER;
    memset(clearing, 0, sizeof(clearing)); removed = 0;
    for (unsigned n = 0; n < count; n++) {
        if (ids[n] >= slots) return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
        if (bit(valid, ids[n]) && !bit(clearing, ids[n])) {
            mark(clearing, ids[n], true); removed++;
        }
    }
    event.type = incompatible_operation ? INKTAG_IMAGE_STORE_EVENT_CLEAR_INCOMPATIBLE : INKTAG_IMAGE_STORE_EVENT_CLEAR_ALL;
    current = 0; cursor = 0; phase = CLEAR; return INKTAG_IMAGE_STORE_OK;
}
void inktag_image_store_abort(void) {
    target = UINT8_MAX;
    if (phase == CAPTURE || phase == ERASE) finish(INKTAG_IMAGE_STORE_EVENT_ABORT, INKTAG_IMAGE_STORE_OK);
}
void inktag_image_store_process(void) {
    inktag_image_store_result_t result;
    if (phase == ERASE) {
        result = INKTAG_FIXED_STORE_MUTATE(address(current) + erase_next * INKTAG_FIXED_STORE_SECTOR_SIZE, NULL, 0);
        if (result == INKTAG_IMAGE_STORE_OK && erase_next == 0) mark(valid, current, false);
        if (result != INKTAG_IMAGE_STORE_OK || ++erase_next == stride) {
            phase = result == INKTAG_IMAGE_STORE_OK ? CAPTURE : IDLE;
            event = (inktag_image_store_event_t){INKTAG_IMAGE_STORE_EVENT_BEGIN, result, current, 0}; pending = true;
        }
    } else if (phase == CLEAR) {
        while (cursor < slots && !bit(clearing, cursor)) cursor++;
        if (cursor == slots) { finish(event.type, INKTAG_IMAGE_STORE_OK); return; }
        uint32_t zero = 0;
        result = INKTAG_FIXED_STORE_MUTATE(address(cursor), &zero, 4);
        if (result != INKTAG_IMAGE_STORE_OK) { finish(event.type, result); return; }
        mark(valid, cursor, false); cursor++;
    }
}

bool inktag_image_store_profile_allowed(const epd_screen_config_t *profile) {
    epd_screen_t screen;
    /* A resolved profile may fit no complete frame in any accepted extent.
     * Keep RESOURCES off, but permit returning to a smaller profile; real
     * init/IO failures still do not authorize remapping. No extra RAM state. */
    if ((!ready && init_result != INKTAG_IMAGE_STORE_NO_SPACE) || phase != IDLE || pending || !profile || epd_screen_resolve(profile, &screen) != EPD_SCREEN_STATUS_OK ||
        !screen.frame_bytes || screen.frame_bytes > UINT32_MAX - 96u - (INKTAG_FIXED_STORE_SECTOR_SIZE - 1u)) return false;
    if ((screen.frame_bytes + 96u + INKTAG_FIXED_STORE_SECTOR_SIZE - 1u) / INKTAG_FIXED_STORE_SECTOR_SIZE == stride) return true;
    for (unsigned id = 0; id < slots; id++) if (bit(valid, id)) return false;
    return true;  /* Existing clear-all persists invalid markers before remap. */
}
