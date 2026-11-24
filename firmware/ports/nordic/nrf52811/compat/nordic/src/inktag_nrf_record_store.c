#include "inktag_nrf_record_store.h"

#include <stddef.h>

#include "inktag_nrf_config_store.h"
#include "nrf_error.h"

#define INKTAG_NRF_RECORD_MAX_BYTES 160u

static bool s_initialized;
static bool s_event_ready;
static inktag_nrf_record_event_t s_event;
static uint32_t s_last_error = NRF_SUCCESS;

static bool kind_valid(inktag_nrf_record_kind_t kind) {
    return kind == INKTAG_NRF_RECORD_HEALTH;
}

static void publish_event(inktag_nrf_record_kind_t kind, bool success,
                          bool deleted) {
    s_event.kind = kind;
    s_event.success = success;
    s_event.deleted = deleted;
    s_event_ready = true;
}

bool inktag_nrf_record_store_init(void) {
    if (s_initialized) {
        s_last_error = NRF_SUCCESS;
        return true;
    }
    s_initialized = inktag_nrf_config_store_init();
    s_last_error = inktag_nrf_config_store_last_error();
    return s_initialized;
}

bool inktag_nrf_record_store_read(inktag_nrf_record_kind_t kind,
                                  void *output, uint16_t capacity,
                                  uint16_t *length) {
    bool ok;
    if (!s_initialized || !kind_valid(kind) || output == NULL ||
        length == NULL || capacity == 0u) {
        s_last_error = NRF_ERROR_INVALID_PARAM;
        return false;
    }
    ok = inktag_nrf_config_store_read(INKTAG_NRF_CONFIG_HEALTH, output, capacity,
                                      length);
    s_last_error = inktag_nrf_config_store_last_error();
    return ok;
}

bool inktag_nrf_record_store_write(inktag_nrf_record_kind_t kind,
                                   const void *data, uint16_t length) {
    bool ok;
    if (!s_initialized || s_event_ready || !kind_valid(kind) || data == NULL ||
        length == 0u || length > INKTAG_NRF_RECORD_MAX_BYTES) {
        s_last_error = s_event_ready ? NRF_ERROR_BUSY : NRF_ERROR_INVALID_PARAM;
        return false;
    }
    ok = inktag_nrf_config_store_write(INKTAG_NRF_CONFIG_HEALTH, data, length);
    s_last_error = inktag_nrf_config_store_last_error();
    if (ok) publish_event(kind, true, false);
    return ok;
}

bool inktag_nrf_record_store_delete(inktag_nrf_record_kind_t kind) {
    bool ok;
    if (!s_initialized || s_event_ready || !kind_valid(kind)) {
        s_last_error = s_event_ready ? NRF_ERROR_BUSY : NRF_ERROR_INVALID_PARAM;
        return false;
    }
    ok = inktag_nrf_config_store_delete(INKTAG_NRF_CONFIG_HEALTH);
    s_last_error = inktag_nrf_config_store_last_error();
    if (ok) publish_event(kind, true, true);
    return ok;
}

bool inktag_nrf_record_store_pending(inktag_nrf_record_kind_t kind) {
    return s_event_ready && s_event.kind == kind;
}

bool inktag_nrf_record_store_take_event(inktag_nrf_record_event_t *event) {
    if (event == NULL || !s_event_ready) return false;
    *event = s_event;
    s_event_ready = false;
    return true;
}

uint32_t inktag_nrf_record_store_last_error(void) { return s_last_error; }
