#include "inktag_da_image_store.h"
#include "inktag_da_fixed_slot_io.h"
#include "inktag_epd_config.h"
#include <string.h>

extern void app_service_ble(void);
extern bool inktag_da_active_screen_config(epd_screen_config_t *out);

/* This MCU executes code from SRAM. Keep the shared engine boundary out of
 * the large command handler instead of duplicating it through LTO inlining.
 * These declarations change placement only, never common semantics. */
__attribute__((noinline)) inktag_image_store_result_t inktag_image_store_begin(
    const epd_screen_config_t *, uint8_t, uint8_t, uint8_t, uint8_t,
    const uint32_t[2], uint32_t, uint32_t, uint32_t, const char *, uint8_t *);
__attribute__((noinline)) inktag_image_store_result_t inktag_image_store_commit(void);
__attribute__((noinline)) inktag_image_store_result_t inktag_image_store_status(
    const epd_screen_config_t *, uint32_t, inktag_image_store_status_t *);
__attribute__((noinline)) void inktag_image_store_process(void);
__attribute__((noinline)) bool inktag_image_store_profile_allowed(const epd_screen_config_t *);

static inktag_image_store_result_t da_fixed_mutate(
    uint32_t address, const void *data, uint16_t length)
{
    inktag_status_v2_t result = inktag_da_fixed_io_mutate(address, data, length);
    return result == INKTAG_STATUS_OK ? INKTAG_IMAGE_STORE_OK :
           result == INKTAG_STATUS_LOW_POWER ? INKTAG_IMAGE_STORE_LOW_POWER :
           result == INKTAG_STATUS_INVALID_REQUEST ? INKTAG_IMAGE_STORE_BAD_ARGUMENT :
           INKTAG_IMAGE_STORE_IO_ERROR;
}

#define INKTAG_FIXED_STORE_SECTORS (INKTAG_DA_FIXED_STORE_SIZE / INKTAG_DA_FIXED_ERASE_BYTES)
#define INKTAG_FIXED_STORE_CAPACITY(step) (INKTAG_FIXED_STORE_SECTORS / (step))
#define INKTAG_FIXED_STORE_ADDRESS(id, step) \
    (INKTAG_DA_FIXED_STORE_START + (uint32_t)(id) * (step) * INKTAG_DA_FIXED_ERASE_BYTES)
#define INKTAG_FIXED_STORE_READ(address, data, length) \
    (inktag_da_fixed_io_read((address), (data), (length)) == INKTAG_STATUS_OK)
#define INKTAG_FIXED_STORE_MUTATE da_fixed_mutate
#define INKTAG_FIXED_STORE_POWER_ALLOWED() \
    inktag_da_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH, false)
#define INKTAG_FIXED_STORE_CONFIG_READ inktag_da_active_screen_config
/* The generated SDK copy places only the 96-byte capture header in the former
 * transaction scratch section. Keep initialized engine state in ordinary RAM;
 * high scratch must remain pure ZI, separate from SDK6's live BLE heaps. */
#include "inktag_image_fixed_store.inc"

_Static_assert(INKTAG_DA_FIXED_STORE_START + INKTAG_DA_FIXED_STORE_SIZE ==
                   INKTAG_DA_FIXED_STORE_END, "unchanged DA image Flash boundary");

static inktag_image_store_result_t wait_event(
    inktag_image_store_event_type_t expected, uint8_t *id, uint16_t *count)
{
    inktag_image_store_event_t completed;
    /* Every successful iteration advances one bounded physical sector/header.
     * SDK Flash waits retain their maintained timeout/watchdog policy. */
    while (!inktag_image_store_take_event(&completed)) {
        /* A BLE disconnect can abort while servicing between erased sectors.
         * Never wait forever for the BEGIN event that abort already consumed. */
        if (!inktag_image_store_busy()) return INKTAG_IMAGE_STORE_BUSY;
        inktag_image_store_process();
        app_service_ble();
    }
    if (completed.type != expected) return INKTAG_IMAGE_STORE_BUSY;
    if (id != NULL) *id = completed.id;
    if (count != NULL) *count = completed.removed;
    return completed.result;
}

inktag_image_store_result_t inktag_da_store_begin(
    const epd_screen_config_t *screen, uint8_t format,
    uint8_t segments, uint8_t black, uint8_t type, const uint32_t sizes[2],
    uint32_t payload, uint32_t checksum, uint32_t created, const char *name,
    uint8_t requested, uint8_t *id)
{
    inktag_image_store_result_t result;
    if (requested != UINT8_MAX && inktag_image_store_supported() &&
        !inktag_image_store_replace_slot(requested))
        return inktag_image_store_busy() ? INKTAG_IMAGE_STORE_BUSY : INKTAG_IMAGE_STORE_NOT_FOUND;
    result = inktag_image_store_begin(screen, format, segments, black, type,
                                    sizes, payload, checksum, created, name, id);
    return result == INKTAG_IMAGE_STORE_OK ?
        wait_event(INKTAG_IMAGE_STORE_EVENT_BEGIN, id, NULL) : result;
}

inktag_image_store_result_t inktag_da_store_commit(uint8_t *id)
{
    inktag_image_store_result_t result = inktag_image_store_commit();
    inktag_image_store_event_t completed;
    /* Commit is synchronous, including failure after programming valid magic.
     * Drain its failure event too, otherwise the next upload stays BUSY. */
    if (inktag_image_store_take_event(&completed)) {
        if (completed.type != INKTAG_IMAGE_STORE_EVENT_COMMIT) return INKTAG_IMAGE_STORE_BUSY;
        if (id != NULL) *id = completed.id;
        return completed.result;
    }
    return result;
}

void inktag_da_store_abort(void)
{
    inktag_image_store_event_t discarded;
    inktag_image_store_abort();
    (void)inktag_image_store_take_event(&discarded);
}

bool inktag_da_store_profile_allowed(const epd_screen_config_t *profile)
{
    return inktag_image_store_profile_allowed(profile);
}

inktag_image_store_result_t inktag_da_store_next(
    uint8_t after, uint8_t *id, inktag_image_store_header_t *header)
{
    unsigned n = after == UINT8_MAX ? 0u : (unsigned)after + 1u;
    if (id == NULL || header == NULL) return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
    for (; n < inktag_image_store_fixed_slot_count(); n++) {
        inktag_image_store_result_t result = inktag_image_store_get((uint8_t)n, header);
        if (result == INKTAG_IMAGE_STORE_NOT_FOUND) continue;
        if (result == INKTAG_IMAGE_STORE_OK) *id = (uint8_t)n;
        return result;
    }
    /* Failed boot scan must not present a misleading empty directory. */
    if (!inktag_image_store_supported()) return inktag_image_store_get(0u, header);
    return INKTAG_IMAGE_STORE_NOT_FOUND;
}

inktag_image_store_result_t inktag_da_store_delete(uint8_t id)
{
    inktag_image_store_result_t result = inktag_image_store_delete(id);
    return result == INKTAG_IMAGE_STORE_OK ?
        wait_event(INKTAG_IMAGE_STORE_EVENT_DELETE, NULL, NULL) : result;
}

inktag_image_store_result_t inktag_da_store_clear(
    const epd_screen_config_t *screen, uint16_t *removed)
{
    inktag_image_store_result_t result = screen != NULL ?
        inktag_image_store_clear_incompatible(screen) : inktag_image_store_clear_all();
    return result == INKTAG_IMAGE_STORE_OK ?
        wait_event(screen != NULL ? INKTAG_IMAGE_STORE_EVENT_CLEAR_INCOMPATIBLE :
                   INKTAG_IMAGE_STORE_EVENT_CLEAR_ALL, NULL, removed) : result;
}
