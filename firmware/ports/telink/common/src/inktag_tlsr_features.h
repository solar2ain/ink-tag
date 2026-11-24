#ifndef INKTAG_TLSR_FEATURES_H
#define INKTAG_TLSR_FEATURES_H

#include "inktag_tlsr_build_profile.h"
#include <stdbool.h>

#include "inktag_power.h"
#include "inktag_tlsr_aux_io.h"
#include "inktag_tlsr.h"

#define INKTAG_TLSR_NFC_IMPLEMENTED INKTAG_BUILD_PRO
#define INKTAG_TLSR_NFC_BACKEND_AVAILABLE INKTAG_BUILD_PRO
#define INKTAG_TLSR_INDICATOR_IMPLEMENTED 1
#define INKTAG_TLSR_INDICATOR_BACKEND_AVAILABLE 1
#define INKTAG_TLSR_AUX_IO_IMPLEMENTED INKTAG_BUILD_PRO
#define INKTAG_TLSR_AUX_IO_BACKEND_AVAILABLE INKTAG_BUILD_PRO
/* Keep the button wake backend in the application so a board can
 * be qualified by provisioning its exact route, without changing the OTA
 * artifact. Runtime board contracts still gate every pin and capability. */
#define INKTAG_TLSR_AUX_IO_BUILD_ENABLED INKTAG_BUILD_PRO
#define INKTAG_TLSR_LIFECYCLE_IMPLEMENTED 1
#define INKTAG_TLSR_LIFECYCLE_BACKEND_AVAILABLE 1
#define INKTAG_TLSR_HEALTH_IMPLEMENTED 1
#define INKTAG_TLSR_HEALTH_BACKEND_AVAILABLE 1
#define INKTAG_TLSR_ACTIVATION_IMPLEMENTED INKTAG_BUILD_PRO
#define INKTAG_TLSR_ACTIVATION_BACKEND_AVAILABLE INKTAG_BUILD_PRO
#define INKTAG_TLSR_FIND_IMPLEMENTED INKTAG_BUILD_PRO
#define INKTAG_TLSR_FIND_BACKEND_AVAILABLE INKTAG_BUILD_PRO
#define INKTAG_TLSR_DEVICE_INFO_QR_IMPLEMENTED 1
#define INKTAG_TLSR_DEVICE_INFO_QR_BACKEND_AVAILABLE 1
#define INKTAG_TLSR_GUI_ASSET_UPDATE_IMPLEMENTED INKTAG_BUILD_PRO
#define INKTAG_TLSR_GUI_ASSET_UPDATE_BACKEND_AVAILABLE INKTAG_BUILD_PRO /* native-only ITFA3, bounded32KiB */
#define INKTAG_TLSR_TRANSFER_PACKBITS_IMPLEMENTED 1
#define INKTAG_TLSR_TRANSFER_PACKBITS_BACKEND_AVAILABLE 1

static inline bool inktag_tlsr_board_has(
    inktag_board_id_t board, uint32_t flag) {
    const inktag_tlsr_board_profile_t *profile =
        inktag_tlsr_board_profile(board);
    return profile != NULL && (profile->feature_flags & flag) != 0u;
}

static inline bool inktag_tlsr_board_nfc_qualified(inktag_board_id_t board) {
    /* Contact-presence evidence only. Runtime probe/readiness and the separate
     * RF field/Type4 qualifications still govern their respective paths. */
    return inktag_tlsr_board_has(
        board, INKTAG_TLSR_BOARD_NFC_PRESENT_QUALIFIED);
}

static inline bool inktag_tlsr_board_nfc_field_action_qualified(
    inktag_board_id_t board) {
    return inktag_tlsr_board_has(
        board, INKTAG_TLSR_BOARD_NFC_FIELD_ACTION_QUALIFIED);
}

static inline bool inktag_tlsr_board_nfc_field_action_available(
    inktag_board_id_t board) {
    return inktag_tlsr_board_has(
        board, INKTAG_TLSR_BOARD_NFC_FIELD_ACTION_AVAILABLE);
}

static inline bool inktag_tlsr_board_nfc_type4_ndef_available(
    inktag_board_id_t board) {
    return inktag_tlsr_board_has(
        board, INKTAG_TLSR_BOARD_NFC_TYPE4_AVAILABLE);
}

static inline bool inktag_tlsr_board_nfc_type4_ndef_qualified(
    inktag_board_id_t board) {
    return inktag_tlsr_board_has(
        board, INKTAG_TLSR_BOARD_NFC_TYPE4_QUALIFIED);
}

static inline bool inktag_tlsr_board_indicator_qualified(
    inktag_board_id_t board) {
    return inktag_tlsr_board_has(
        board, INKTAG_TLSR_BOARD_INDICATOR_QUALIFIED);
}

static inline bool inktag_tlsr_board_led_diagnostic_available(
    inktag_board_id_t board) {
    return inktag_tlsr_board_has(
        board, INKTAG_TLSR_BOARD_LED_DIAGNOSTIC_AVAILABLE);
}

static inline bool inktag_tlsr_board_aux_io_route(
    inktag_board_id_t board, inktag_tlsr_aux_io_route_t *route) {
    const inktag_tlsr_board_profile_t *profile;
    if (!route) return false;
    route->button_gpio = INKTAG_TLSR_AUX_GPIO_UNUSED;
    route->qualified = false;
    profile = inktag_tlsr_board_profile(board);
    if (!profile) return false;
    route->button_gpio = profile->aux_button_gpio;
    route->qualified = (profile->feature_flags &
        INKTAG_TLSR_BOARD_AUX_IO_QUALIFIED) != 0u;
    return route->qualified;
}

static inline bool inktag_tlsr_board_lifecycle_qualified(
    inktag_board_id_t board) {
    return inktag_tlsr_board_has(
        board, INKTAG_TLSR_BOARD_LIFECYCLE_QUALIFIED);
}

static inline bool inktag_tlsr_board_ota_qualified(inktag_board_id_t board) {
    return inktag_tlsr_board_has(
        board, INKTAG_TLSR_BOARD_OTA_RECOVERY_QUALIFIED);
}

static inline bool inktag_tlsr_board_activation_qualified(
    inktag_board_id_t board) {
    return inktag_tlsr_board_has(
        board, INKTAG_TLSR_BOARD_ACTIVATION_QUALIFIED);
}

static inline bool inktag_tlsr_board_find_qualified(
    inktag_board_id_t board) {
    return inktag_tlsr_board_has(
        board, INKTAG_TLSR_BOARD_FIND_QUALIFIED);
}

static inline bool inktag_tlsr_board_find_experimental_available(
    inktag_board_id_t board) {
    return inktag_tlsr_board_has(
        board, INKTAG_TLSR_BOARD_FIND_EXPERIMENT_AVAILABLE);
}

static inline bool inktag_tlsr_board_gui_asset_update_qualified(
    inktag_board_id_t board) {
    return inktag_tlsr_board_has(
        board, INKTAG_TLSR_BOARD_GUI_ASSET_UPDATE_QUALIFIED);
}

static inline bool inktag_tlsr_board_transfer_packbits_qualified(
    inktag_board_id_t board) {
    return inktag_tlsr_board_has(
        board, INKTAG_TLSR_BOARD_PACKBITS_RLE_QUALIFIED);
}

static inline bool inktag_tlsr_board_power_thresholds(
    inktag_board_id_t board, inktag_power_thresholds_t *thresholds) {
    const inktag_tlsr_board_profile_t *profile;
    if (!thresholds) return false;
    inktag_power_policy_thresholds(thresholds);
    profile = inktag_tlsr_board_profile(board);
    if (profile != NULL)
        return (profile->feature_flags &
            INKTAG_TLSR_BOARD_POWER_THRESHOLDS_QUALIFIED) != 0u;
    /* An unknown selector is unqualified, but it must retain the fail-safe
     * product thresholds. Zeroing these fields would accidentally authorize
     * Flash and OTA writes on an unidentified board. */
    return false;
}

#endif
