/* Fixed current-frame pilot. ID -> address is arithmetic; only a validity
 * bitmap lives in RAM. Names stay in 96-byte headers. No persistent index,
 * COW, generations or compatibility with retired headers. */
#include "inktag_nrf_image_store.h"
#include "inktag_nrf_external_flash.h"
#include "inktag_nrf_health.h"
#include "inktag_board.h"
#include "inktag_protocol_core.h"
#include "inktag_resource_name.h"
#include "inktag_epd_config.h"
#include "nrf_error.h"
#include <stddef.h>
#include <string.h>
#if defined(INKTAG_NRF_SDK17)
#include "app_timer.h"
/* Read-only SWD evidence: elapsed RTC ticks for actual external header scan. */
volatile uint32_t inktag_nrf_image_scan_ticks;
#endif

#define START INKTAG_BOARD_IMAGE_STORE_START
#define BYTES INKTAG_BOARD_IMAGE_STORE_SIZE
#define SECTORS (BYTES / 4096u)
#define MAGIC UINT32_C(0x31494b49) /* IKI1 */
#define VERSION 1u
static uint8_t valid[(SECTORS + 7u) / 8u], clearing[(SECTORS + 7u) / 8u];
static uint8_t stride, slots, target = UINT8_MAX, current, phase, erase_next;
static uint16_t cursor, removed;
static uint32_t frame, received, running_crc;
static bool ready, pending;
static inktag_image_store_header_t capture;
static inktag_image_store_event_t event;
enum { IDLE, ERASE, CAPTURE, CLEAR = 5 };
_Static_assert(sizeof(capture) == 96, "fixed image header must be 96 bytes");

static bool bit(const uint8_t *map, unsigned id) { return (map[id >> 3] & (1u << (id & 7))) != 0; }
static void mark(uint8_t *map, unsigned id, bool value) {
    uint8_t mask = 1u << (id & 7);
    if (value) map[id >> 3] |= mask; else map[id >> 3] &= (uint8_t)~mask;
}
static uint32_t address(unsigned id) { return START + id * stride * 4096u; }
static uint32_t crc(const void *data, uint16_t length) {
    return inktag_crc32_update(UINT32_MAX, data, length) ^ UINT32_MAX;
}
static uint32_t header_crc(const inktag_image_store_header_t *header) {
    return crc((const uint8_t *)header + 4, sizeof(*header) - 8);
}
static bool name_terminated(const char *name) {
    for (unsigned i=0; i<INKTAG_IMAGE_STORE_NAME_SIZE; ++i)
        if (!name[i]) return true;
    return false;
}
static bool read_bytes(uint32_t addr, void *data, uint32_t length) {
    if (addr < START || addr > START + BYTES || length > START + BYTES - addr ||
        !inktag_nrf_external_flash_begin()) return false;
    bool ok = inktag_nrf_external_flash_read(addr, data, length);
    inktag_nrf_external_flash_end();
    return ok;
}
static inktag_image_store_result_t mutate(uint32_t addr, const void *data, uint16_t length) {
    uint32_t extent = data ? length : 4096u;
    if (addr < START || addr > START + BYTES || extent > START + BYTES - addr ||
        (!data && (addr & 4095u))) return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
    if (!inktag_nrf_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH, true))
        return INKTAG_IMAGE_STORE_LOW_POWER;
    if (!inktag_nrf_external_flash_begin()) return INKTAG_IMAGE_STORE_IO_ERROR;
    bool ok = true;
    if (!data) ok = inktag_nrf_external_flash_erase_sector(addr);
    else while (length && ok) {
        uint16_t count = 256u - (addr & 255u);
        if (count > length) count = length;
        ok = inktag_nrf_external_flash_program(addr, data, count);
        addr += count; data = (const uint8_t *)data + count; length -= count;
    }
    uint32_t error = inktag_nrf_external_flash_last_error();
    inktag_nrf_external_flash_end();
    return ok ? INKTAG_IMAGE_STORE_OK : error == NRF_ERROR_FORBIDDEN ?
        INKTAG_IMAGE_STORE_LOW_POWER : INKTAG_IMAGE_STORE_IO_ERROR;
}
static bool header_valid(const inktag_image_store_header_t *h, uint8_t required_stride) {
    epd_screen_config_t screen;
    return h->magic == MAGIC && h->version == VERSION && h->header_size == 96 &&
        h->record_sectors && h->record_sectors <= SECTORS &&
        (!required_stride || h->record_sectors == required_stride) && h->header_crc == header_crc(h) &&
        name_terminated(h->name) && h->content_type <= INKTAG_IMAGE_CONTENT_MAX &&
        (h->black_mask & (uint8_t)~INKTAG_IMAGE_STORED_FLAGS_MASK) == 0u &&
        h->segment_count && h->segment_count <= 2 && h->payload_size &&
        h->payload_size <= h->record_sectors * 4096u - 96 &&
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
    ready = false; phase = IDLE; pending = false; target = UINT8_MAX;
    if (!epd_screen_config_read(&configured)) return false;
    if (epd_screen_resolve(&configured, &screen) != EPD_SCREEN_STATUS_OK) return false;
    frame = screen.frame_bytes;
    uint32_t step = (frame + 96u + 4095u) / 4096u;
    if (!step || step > SECTORS) return false;
    stride = step; slots = SECTORS / stride;
    memset(valid, 0, sizeof(valid));
#if defined(INKTAG_NRF_SDK17)
    uint32_t scan_started = app_timer_cnt_get();
    inktag_nrf_image_scan_ticks = UINT32_MAX;
#endif
    for (unsigned id = 0; id < slots; id++) {
        inktag_image_store_header_t header;
        if (!read_bytes(address(id), &header, sizeof(header))) return false;
        if (header_valid(&header, stride)) mark(valid, id, true);
    }
#if defined(INKTAG_NRF_SDK17)
    inktag_nrf_image_scan_ticks = app_timer_cnt_diff_compute(app_timer_cnt_get(), scan_started);
#endif
    ready = true; return true;
}
bool inktag_image_store_supported(void) { return ready; }
bool inktag_image_store_busy(void) { return phase != IDLE; }
bool inktag_image_store_capture_active(void) { return phase == CAPTURE || phase == ERASE; }
uint16_t inktag_image_store_sector_count(void) { return slots; }
bool inktag_image_store_take_event(inktag_image_store_event_t *out) {
    if (!out || !pending) return false;
    *out = event; pending = false; return true;
}
inktag_image_store_result_t inktag_image_store_get(uint8_t id, inktag_image_store_header_t *h) {
    if (!ready) return INKTAG_IMAGE_STORE_UNSUPPORTED;
    if (phase != IDLE) return INKTAG_IMAGE_STORE_BUSY;
    if (!h) return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
    if (id >= slots || !bit(valid, id)) return INKTAG_IMAGE_STORE_NOT_FOUND;
    if (!read_bytes(address(id), h, sizeof(*h))) return INKTAG_IMAGE_STORE_IO_ERROR;
    return header_valid(h, stride) ? INKTAG_IMAGE_STORE_OK : INKTAG_IMAGE_STORE_IO_ERROR;
}
static bool compatible(const inktag_image_store_header_t *h, const epd_screen_config_t *screen) {
    uint8_t wire[EPD_SCREEN_WIRE_SIZE];
    return screen && epd_screen_config_encode(screen, wire) == EPD_SCREEN_STATUS_OK &&
        epd_screen_wire_image_compatible(h->screen_config, wire);
}
static inktag_image_store_result_t clear_heads(const epd_screen_config_t *profile,
                                               uint16_t *count);
inktag_image_store_result_t inktag_image_store_status(const epd_screen_config_t *screen,
    uint32_t payload, inktag_image_store_status_t *out) {
    if (!ready) return INKTAG_IMAGE_STORE_UNSUPPORTED;
    if (!out) return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
    if (phase != IDLE) return INKTAG_IMAGE_STORE_BUSY;
    memset(out, 0, sizeof(*out)); out->total_sectors = SECTORS;
    out->required_sectors = (payload + 96u + 4095u) / 4096u;
    unsigned run = 0;
    for (unsigned id = 0; id < slots; id++) {
        if (!bit(valid, id)) {
            out->free_sectors += stride; run += stride;
            if (run > out->largest_free_run) out->largest_free_run = run;
        } else {
            inktag_image_store_header_t h;
            inktag_image_store_result_t result = inktag_image_store_get(id, &h);
            if (result != INKTAG_IMAGE_STORE_OK) return result;
            run = 0; out->saved_count++;
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
    if (!ready) return INKTAG_IMAGE_STORE_UNSUPPORTED;
    if (phase != IDLE || pending) return INKTAG_IMAGE_STORE_BUSY;
    if (!inktag_nrf_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH, true)) return INKTAG_IMAGE_STORE_LOW_POWER;
    if (!screen || !sizes || !id || payload != frame || !segments || segments > 2 ||
        (black & (uint8_t)~INKTAG_IMAGE_STORED_FLAGS_MASK) != 0u ||
        type > INKTAG_IMAGE_CONTENT_MAX || sizes[0] > payload ||
        sizes[1] != payload - sizes[0] || (segments == 1 && sizes[1])) return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
    size_t length = name ? strlen(name) : 0;
    if (length >= sizeof(capture.name) || (length && !inktag_resource_name_valid((const uint8_t *)name, length)))
        return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
    for (unsigned n = 0; n < slots; n++) {
        if (requested == UINT8_MAX && !bit(valid, n)) requested = n;
        if (bit(valid, n) && length) {
            inktag_image_store_header_t h;
            inktag_image_store_result_t result = inktag_image_store_get(n, &h);
            if (result != INKTAG_IMAGE_STORE_OK) return result;
            if (n != requested && inktag_resource_name_matches((const uint8_t *)name, length,
                (const uint8_t *)h.name, strlen(h.name))) return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
        }
    }
    if (requested == UINT8_MAX) return INKTAG_IMAGE_STORE_NO_SPACE;
    memset(&capture, 0, sizeof(capture));
    capture.magic = MAGIC; capture.version = VERSION; capture.header_size = 96; capture.record_sectors = stride;
    if (epd_screen_config_encode(screen, capture.screen_config) != EPD_SCREEN_STATUS_OK) return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
    /* The adapter matched the configured profile and all begin fields are
     * validated above. Reset-settings retains foreign-layout heads; invalidate
     * them before allocating pixels so overwritten old records cannot revive.
     * Avoid resolving the same profile again through the public change API. */
    uint16_t count;
    inktag_image_store_result_t prepared = clear_heads(screen, &count);
    if (prepared != INKTAG_IMAGE_STORE_OK) return prepared;
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
    inktag_image_store_result_t result = mutate(address(current) + 96u + received, data, length);
    if (result == INKTAG_IMAGE_STORE_OK) { running_crc = inktag_crc32_update(running_crc, data, length); received += length; }
    return result;
}
static inktag_image_store_result_t commit_header(const inktag_image_store_header_t *h) {
    uint32_t addr = address(current);
    inktag_image_store_result_t result = mutate(addr + 4, (const uint8_t *)h + 4, 92);
    if (result == INKTAG_IMAGE_STORE_OK) result = mutate(addr, &h->magic, 4);
    inktag_image_store_header_t check;
    if (result == INKTAG_IMAGE_STORE_OK && (!read_bytes(addr, &check, 96) || !header_valid(&check, stride))) result = INKTAG_IMAGE_STORE_IO_ERROR;
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
    return read_bytes(address(id) + 96u + offset, data, length) ? INKTAG_IMAGE_STORE_OK : INKTAG_IMAGE_STORE_IO_ERROR;
}
static inktag_image_store_result_t next(uint8_t after, const epd_screen_config_t *screen, bool image_only, uint8_t *id) {
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
    if (!ready) return INKTAG_IMAGE_STORE_UNSUPPORTED;
    if (phase != IDLE || pending) return INKTAG_IMAGE_STORE_BUSY;
    memset(clearing, 0, sizeof(clearing)); removed = 0;
    /* Explicit ALL invalidates every geometric header, including a durable
     * commit whose readback failed. No payload erase or header reread. */
    for (unsigned n = 0; n < slots; n++) if (all || (bit(valid, n) && (n == id || screen))) {
        if (!all) {
            inktag_image_store_header_t h;
            inktag_image_store_result_t result = inktag_image_store_get(n, &h);
            if (result != INKTAG_IMAGE_STORE_OK) return result;
            if (screen && compatible(&h, screen)) continue;
        }
        mark(clearing, n, true); if (bit(valid, n)) removed++;
    }
    if (!all && !screen && !removed) return INKTAG_IMAGE_STORE_NOT_FOUND;
    event.type = all ? INKTAG_IMAGE_STORE_EVENT_CLEAR_ALL : screen ? INKTAG_IMAGE_STORE_EVENT_CLEAR_INCOMPATIBLE : INKTAG_IMAGE_STORE_EVENT_DELETE;
    current = id; cursor = 0; phase = CLEAR; return INKTAG_IMAGE_STORE_OK;
}
inktag_image_store_result_t inktag_image_store_delete(uint8_t id) { return clear(false, NULL, id); }
inktag_image_store_result_t inktag_image_store_clear_all(void) { return clear(true, NULL, 0); }
inktag_image_store_result_t inktag_image_store_clear_incompatible(const epd_screen_config_t *s) { return s ? clear(false, s, 0) : INKTAG_IMAGE_STORE_BAD_ARGUMENT; }
inktag_image_store_result_t inktag_image_store_clear_selected(const uint8_t *ids, uint8_t count, bool incompatible_operation) {
    if (!ready) return INKTAG_IMAGE_STORE_UNSUPPORTED;
    if (phase != IDLE || pending) return INKTAG_IMAGE_STORE_BUSY;
    if (!ids && count) return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
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
        result = mutate(address(current) + erase_next * 4096u, NULL, 0);
        if (!erase_next && result == INKTAG_IMAGE_STORE_OK) mark(valid, current, false);
        if (result != INKTAG_IMAGE_STORE_OK || ++erase_next == stride) {
            phase = result == INKTAG_IMAGE_STORE_OK ? CAPTURE : IDLE;
            event = (inktag_image_store_event_t){INKTAG_IMAGE_STORE_EVENT_BEGIN, result, current, 0}; pending = true;
        }
    } else if (phase == CLEAR) {
        while (cursor < slots && !bit(clearing, cursor)) cursor++;
        if (cursor == slots) { finish(event.type, INKTAG_IMAGE_STORE_OK); return; }
        uint32_t zero = 0;
        result = mutate(address(cursor), &zero, 4);
        if (result != INKTAG_IMAGE_STORE_OK) { finish(event.type, result); return; }
        mark(valid, cursor, false); cursor++;
    }
}

bool inktag_image_store_profile_allowed(const epd_screen_config_t *profile) {
    epd_screen_t screen;
    if (!ready || epd_screen_resolve(profile, &screen) != EPD_SCREEN_STATUS_OK) return false;
    if ((screen.frame_bytes + 96u + 4095u) / 4096u == stride) return true;
    for (unsigned id = 0; id < slots; id++) if (bit(valid, id)) return false;
    return true;  /* Existing clear-all persists invalid markers before remap. */
}

/* Headers can belong to a geometry retained by reset-settings. Profile changes
 * and new uploads preserve compatible records and skip their entire payload,
 * so image pixels resembling a header never become another record. */
static inktag_image_store_result_t clear_heads(const epd_screen_config_t *profile,
                                               uint16_t *count) {
    if (!ready) return INKTAG_IMAGE_STORE_UNSUPPORTED;
    if (phase != IDLE || pending) return INKTAG_IMAGE_STORE_BUSY;
    *count = 0u;
    for (unsigned sector = 0; sector < SECTORS; ++sector) {
        uint32_t addr = START + sector * 4096u, magic, zero = 0u;
        if (!read_bytes(addr, &magic, sizeof(magic))) return INKTAG_IMAGE_STORE_IO_ERROR;
        if (magic != MAGIC) continue;
        inktag_image_store_header_t header;
        if (!read_bytes(addr, &header, sizeof(header))) return INKTAG_IMAGE_STORE_IO_ERROR;
        bool valid_head = header_valid(&header, 0u) &&
            sector % header.record_sectors == 0u && header.record_sectors <= SECTORS - sector;
        if (profile && valid_head && compatible(&header, profile)) {
            sector += header.record_sectors - 1u;
            continue;
        }
        inktag_image_store_result_t result = mutate(addr, &zero, sizeof(zero));
        if (sector % stride == 0u) {
            unsigned id = sector / stride;
            if (id < slots) mark(valid, id, false);
        }
        if (result != INKTAG_IMAGE_STORE_OK) return result;
        if (!read_bytes(addr, &magic, sizeof(magic)) || magic != 0u) return INKTAG_IMAGE_STORE_IO_ERROR;
        if (valid_head) {
            (*count)++;
            if (profile) sector += header.record_sectors - 1u;
        }
    }
    return INKTAG_IMAGE_STORE_OK;
}

inktag_image_store_result_t inktag_image_store_prepare_profile(const epd_screen_config_t *profile) {
    epd_screen_t resolved;
    if (epd_screen_resolve(profile, &resolved) != EPD_SCREEN_STATUS_OK ||
        !resolved.frame_bytes || (resolved.frame_bytes + 96u + 4095u) / 4096u > SECTORS)
        return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
    uint16_t count;
    return clear_heads(profile, &count);
}

/* Image-only scope includes stale heads from every previous stride. Four-byte
 * invalidation and readback only; no sector erase or payload CRC scan. */
inktag_image_store_result_t inktag_image_store_clear_user_images(uint16_t *count) {
    if (!count) return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
    target = UINT8_MAX;
    return clear_heads(NULL, count);
}

inktag_image_store_result_t inktag_image_store_clear_sync(void) {
    inktag_image_store_result_t result = inktag_image_store_clear_all();
    if (result != INKTAG_IMAGE_STORE_OK) return result;
    while (inktag_image_store_busy()) inktag_image_store_process();
    inktag_image_store_event_t cleared;
    return inktag_image_store_take_event(&cleared) ? cleared.result : INKTAG_IMAGE_STORE_IO_ERROR;
}

uint8_t inktag_image_store_copy_occupancy(uint8_t *out) {
    uint8_t length = (uint8_t)((slots + 7u) / 8u);
    memcpy(out, valid, length);
    return length;
}
