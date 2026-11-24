#include "inktag_om_dfu_diagnostics.h"

#include <string.h>

static inktag_ota_reject_info_t s_last_reject;
static bool s_last_reject_valid;

void inktag_om_dfu_reject_clear(void) {
    memset(&s_last_reject, 0, sizeof(s_last_reject));
    s_last_reject_valid = false;
}

bool inktag_om_dfu_reject_pending(void) {
    return s_last_reject_valid;
}

void inktag_om_dfu_reject_set(inktag_ota_reject_reason_t reason,
                              inktag_ota_reject_stage_t stage,
                              uint8_t flags, uint32_t detail,
                              uint16_t sampled_mv, uint16_t threshold_mv) {
    s_last_reject.reason = reason;
    s_last_reject.stage = stage;
    s_last_reject.flags = flags;
    s_last_reject.detail = detail;
    s_last_reject.sampled_mv = sampled_mv;
    s_last_reject.threshold_mv = threshold_mv;
    s_last_reject_valid = true;
}

void inktag_om_dfu_reject_set_power(inktag_ota_reject_stage_t stage,
                                    uint8_t detail_flags, uint32_t detail,
                                    uint16_t sampled_mv, bool sampled_mv_valid,
                                    uint16_t threshold_mv) {
    uint8_t flags = (uint8_t)(detail_flags |
        INKTAG_OTA_REJECT_FLAG_THRESHOLD_MV_VALID);
    if (sampled_mv_valid) {
        flags |= INKTAG_OTA_REJECT_FLAG_SAMPLED_MV_VALID;
    }
    inktag_om_dfu_reject_set(
        sampled_mv_valid ? INKTAG_OTA_REJECT_REASON_VOLTAGE_BELOW_MIN
                         : INKTAG_OTA_REJECT_REASON_POWER_SAMPLE_INVALID,
        stage, flags, detail, sampled_mv_valid ? sampled_mv : 0u,
        threshold_mv);
}

bool inktag_om_dfu_reject_take(inktag_ota_reject_info_t *info) {
    if (!info || !s_last_reject_valid) return false;
    *info = s_last_reject;
    s_last_reject_valid = false;
    return true;
}
