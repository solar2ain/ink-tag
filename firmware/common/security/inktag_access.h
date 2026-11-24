#ifndef INKTAG_ACCESS_H
#define INKTAG_ACCESS_H

#include <stdbool.h>
#include <stdint.h>

#include "inktag_activation.h"
#include "inktag_brand_overlay.h"
#include "inktag_image_quota.h"

typedef enum {
    INKTAG_PRODUCT_FEATURE_BASIC = 0,
    INKTAG_PRODUCT_FEATURE_IMAGE_CAPACITY,
    INKTAG_PRODUCT_FEATURE_FIND_CONFIGURATION,
    INKTAG_PRODUCT_FEATURE_FIND_RUNTIME,
    INKTAG_PRODUCT_FEATURE_EXPERIMENTAL_DISPLAY,
    INKTAG_PRODUCT_FEATURE_BUTTON_BINDING, /* Custom pins/actions, not stock buttons. */
    INKTAG_PRODUCT_FEATURE_RESERVED_6, /* Retired allocation; do not reuse. */
    INKTAG_PRODUCT_FEATURE_NFC_BASIC, /* Reserved public ID; NFC implementation is Pro-only. */
    INKTAG_PRODUCT_FEATURE_NFC_ADVANCED, /* All NFC actions, incl. tap/NEXT/CLEAR. */
    INKTAG_PRODUCT_FEATURE_TIMED_SLEEP,
    INKTAG_PRODUCT_FEATURE_BRAND_HIDE,
    INKTAG_PRODUCT_FEATURE_BRAND_COLOR,
    INKTAG_PRODUCT_FEATURE_BLUETOOTH_NAME,
    INKTAG_PRODUCT_FEATURE_DEVICE_PASSWORD,
    /* Partial refresh only: non-E6 ordinary full refresh stays base.
     * Requires a real exact-panel backend and explicit experimental opt-in. */
    INKTAG_PRODUCT_FEATURE_PARTIAL_REFRESH,
    INKTAG_PRODUCT_FEATURE_E6_CONTROL,
    INKTAG_PRODUCT_FEATURE_COUNT,
} inktag_product_feature_t;

typedef struct {
    uint32_t required_activation_features;
    bool experimental;
} inktag_product_feature_rule_t;

const inktag_product_feature_rule_t *inktag_access_feature_rule(
    inktag_product_feature_t feature);

inktag_status_v2_t inktag_access_check(
    const inktag_activation_t *activation,
    inktag_product_feature_t feature,
    bool implemented,
    bool board_capable,
    bool experiment_enabled);

bool inktag_access_is_pro(const inktag_activation_t *activation);

bool inktag_access_image_record_visible(
    const inktag_activation_t *activation,
    uint16_t stable_id_ordinal,
    uint16_t visible_width,
    uint16_t visible_height);
uint16_t inktag_access_image_visible_count(
    const inktag_activation_t *activation,
    uint16_t effective_user_record_count,
    uint16_t visible_width,
    uint16_t visible_height);
inktag_status_v2_t inktag_access_image_write(
    const inktag_activation_t *activation,
    uint16_t effective_user_record_count,
    bool replacing_existing_record,
    uint16_t existing_record_ordinal,
    bool physical_space_available,
    uint16_t visible_width,
    uint16_t visible_height);

/* Runtime action gate, not permission to save custom bindings. base platform
 * adapters supply only board-qualified original buttons/default actions. */
inktag_status_v2_t inktag_access_input_action(
    const inktag_activation_t *activation,
    uint8_t source,
    uint8_t action);

inktag_status_v2_t inktag_access_indicator_policy(
    const inktag_activation_t *activation,
    uint8_t indicator_policy);

/* Identity records also contain a base device label. Only gate the BLE
 * advertising name when its bytes actually change. */
inktag_status_v2_t inktag_access_bluetooth_name(
    const inktag_activation_t *activation,
    bool name_changed);

/* A missing/invalid activation always renders the base mark. Clients may
 * request hiding only for a verified Pro device; rendering must call the
 * effective helper again so stale persisted Pro settings fail closed. */
inktag_status_v2_t inktag_access_brand_hidden_setting(
    const inktag_activation_t *activation,
    bool hidden);
bool inktag_access_brand_should_render(
    const inktag_activation_t *activation,
    bool hidden_requested);
inktag_brand_edition_t inktag_access_brand_edition(
    const inktag_activation_t *activation,
    bool hidden_requested);

#endif
