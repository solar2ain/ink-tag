#include "inktag_nrf_password.h"

#include <stddef.h>
#include <string.h>
#include "inktag_nrf_config_store.h"
#include "inktag_protocol_core.h"

void inktag_nrf_password_wipe(void *data, uint16_t length) {
    volatile uint8_t *bytes = data;
    while (length-- != 0u) *bytes++ = 0u;
}

static inktag_device_password_store_result_t password_read(
    void *context, uint8_t *output, uint16_t capacity, uint16_t *length) {
    (void)context;
    /* Boot IO can fail before a journal is opened. The bounded idle password
     * retry must re-run the same strict header/whole-media-blank checks, not
     * remain locked on an uninitialized handle or invent MISSING from it.
     * Nonempty corrupt media is never reformatted by config_store_init(). */
    if (!inktag_nrf_config_store_init()) return INKTAG_PASSWORD_STORE_ERROR;
    inktag_settings_journal_read_result_t read =
        inktag_nrf_config_store_read_result(INKTAG_NRF_CONFIG_DEVICE_INFO_QR,
                                             output, capacity, length);
    if (read == INKTAG_SETTINGS_JOURNAL_READ_MISSING)
        return INKTAG_PASSWORD_STORE_MISSING;
    if (read != INKTAG_SETTINGS_JOURNAL_READ_FOUND)
        return INKTAG_PASSWORD_STORE_ERROR;
    /* The shared release-v1 decoder owns admission. Never normalize old QR. */
    return INKTAG_PASSWORD_STORE_OK;
}

static bool password_put(void *context, const uint8_t *wire, uint16_t length) {
    (void)context;
    return inktag_nrf_config_store_put_atomic(
        INKTAG_NRF_CONFIG_DEVICE_INFO_QR, wire, length);
}

static const inktag_device_password_store_io_t s_password_io = {
    NULL, password_read, password_put,
};

inktag_device_password_store_result_t inktag_nrf_password_load(
    uint8_t scratch[420], inktag_device_password_config_t *config,
    uint8_t *modules, uint16_t *matrix_offset, uint16_t *record_length) {
    return inktag_device_password_store_load_io(&s_password_io, scratch,
        config, modules, matrix_offset, record_length);
}

bool inktag_nrf_password_commit(uint8_t scratch[420], uint16_t record_length,
    inktag_device_password_config_t *actual) {
    bool saved = inktag_device_password_store_commit_io(&s_password_io, scratch,
                                                         record_length, actual);
    /* A false-after-publish erase can leave either generation's credentials.
     * Read back the complete inactive extent even on failure. Boot/idle repair
     * may scrub it later, but no cached open/authorized mode is usable now. */
    bool retired = inktag_nrf_config_store_inactive_erased();
    if (!retired && actual) actual->valid = false;
    return saved && retired;
}

bool inktag_nrf_password_read_qr(epd_device_info_qr_t *qr) {
    uint8_t scratch[420], modules;
    uint16_t offset, length;
    inktag_device_password_config_t config;
    bool result = qr && inktag_nrf_password_load(scratch, &config, &modules,
        &offset, &length) == INKTAG_PASSWORD_STORE_OK;
    if (result) {
        uint16_t matrix = (uint16_t)(length - offset);
        if (matrix > sizeof(qr->data)) result = false;
        else {
            qr->modules = modules;
            qr->version = 1u;
            qr->length = matrix;
            memcpy(qr->data, scratch + offset, matrix);
        }
    }
    inktag_nrf_password_wipe(scratch, sizeof(scratch));
    inktag_nrf_password_wipe(&config, sizeof(config));
    return result;
}
