#include "inktag_gr_screen.h"
#include "inktag_gr_health.h"
#include <string.h>

inktag_gr_settings_read_result_t inktag_gr_screen_read(epd_screen_config_t *out) {
    epd_screen_record_t record;
    uint16_t length = sizeof(record);
    if (!out) return INKTAG_GR_SETTINGS_READ_ERROR;
    inktag_gr_settings_read_result_t result = inktag_gr_settings_get(
        INKTAG_GR_SETTINGS_SLOT_SCREEN_PROFILE, &length, record.bytes);
    if (result != INKTAG_GR_SETTINGS_READ_OK) return result;
    return length == sizeof(record) &&
        epd_screen_record_decode_exact(&record, out) == EPD_SCREEN_STATUS_OK
        ? INKTAG_GR_SETTINGS_READ_OK : INKTAG_GR_SETTINGS_READ_ERROR;
}

inktag_status_v2_t inktag_gr_screen_commit(const epd_screen_config_t *screen) {
    epd_screen_record_t requested, observed;
    uint16_t length = sizeof(observed);
    if (!screen || epd_screen_record_encode(screen, &requested) != EPD_SCREEN_STATUS_OK ||
        requested.bytes[6] != screen->panel_recipe) return INKTAG_STATUS_INCOMPATIBLE;
    if (!inktag_gr_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH, true))
        return INKTAG_STATUS_LOW_POWER;
    inktag_gr_settings_write_result_t result = inktag_gr_settings_put_result(
        INKTAG_GR_SETTINGS_SLOT_SCREEN_PROFILE, sizeof(requested), requested.bytes);
    /* Readback never acknowledges a false-after-commit. No compensation,
     * retry, inferred rollback, generic settings save or post-commit routing. */
    bool same = inktag_gr_settings_get(INKTAG_GR_SETTINGS_SLOT_SCREEN_PROFILE,
        &length, observed.bytes) == INKTAG_GR_SETTINGS_READ_OK &&
        length == sizeof(observed) && !memcmp(&requested, &observed, sizeof(requested));
    return result == INKTAG_GR_SETTINGS_WRITE_OK && same ?
        INKTAG_STATUS_OK : INKTAG_STATUS_PERSISTENCE_ERROR;
}
