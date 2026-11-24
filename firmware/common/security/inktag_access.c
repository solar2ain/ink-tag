#include "inktag_access.h"

#include <stddef.h>

#include "inktag_input.h"
#include "inktag_status.h"

#define BASE_RULE {0u, false}
#define BASE_EXPERIMENTAL_RULE {0u, true}
#define PRO_RULE {INKTAG_ACTIVATION_FEATURE_ADVANCED, false}
#define PRO_EXPERIMENTAL_RULE {INKTAG_ACTIVATION_FEATURE_ADVANCED, true}

static const inktag_product_feature_rule_t s_feature_rules[] = {
    BASE_RULE,             /* BASIC */
    BASE_RULE,             /* IMAGE_CAPACITY: physical capacity only */
    PRO_RULE,              /* FIND_CONFIGURATION */
    PRO_EXPERIMENTAL_RULE, /* FIND_RUNTIME */
    BASE_EXPERIMENTAL_RULE,/* EXPERIMENTAL_DISPLAY */
    PRO_RULE,              /* BUTTON_BINDING: customization, not stock input */
    PRO_RULE,              /* RESERVED_6: no implementation */
    PRO_RULE,              /* NFC_BASIC: all NFC implementation is private */
    PRO_RULE,              /* NFC_ADVANCED: all received NFC commands */
    PRO_EXPERIMENTAL_RULE, /* TIMED_SLEEP */
    PRO_RULE,              /* BRAND_HIDE */
    PRO_RULE,              /* BRAND_COLOR */
    PRO_RULE,              /* BLUETOOTH_NAME */
    PRO_RULE,              /* DEVICE_PASSWORD: enable/change, never a read gate */
    PRO_EXPERIMENTAL_RULE, /* PARTIAL_REFRESH: no backend/opt-in means no partial */
    PRO_RULE,              /* E6_CONTROL: includes ordinary full refresh */
};

typedef char inktag_feature_rule_count_must_match[
    sizeof(s_feature_rules) / sizeof(s_feature_rules[0]) ==
            INKTAG_PRODUCT_FEATURE_COUNT
        ? 1
        : -1];

const inktag_product_feature_rule_t *inktag_access_feature_rule(
    inktag_product_feature_t feature) {
    if ((uint32_t)feature >= (uint32_t)INKTAG_PRODUCT_FEATURE_COUNT ||
        feature == INKTAG_PRODUCT_FEATURE_RESERVED_6) return NULL;
    return &s_feature_rules[feature];
}

bool inktag_access_is_pro(const inktag_activation_t *activation) {
    (void)activation;
    return false;
}

inktag_status_v2_t inktag_access_check(
    const inktag_activation_t *activation,
    inktag_product_feature_t feature,
    bool implemented,
    bool board_capable,
    bool experiment_enabled) {
    const inktag_product_feature_rule_t *rule =
        inktag_access_feature_rule(feature);
    if (rule == NULL || !implemented || !board_capable) {
        return INKTAG_STATUS_UNSUPPORTED;
    }
    (void)activation;
    if (rule->required_activation_features != 0u) {
        return INKTAG_STATUS_DENIED;
    }
    if (rule->experimental && !experiment_enabled) {
        return INKTAG_STATUS_LOCKED;
    }
    return INKTAG_STATUS_OK;
}

bool inktag_access_image_record_visible(
    const inktag_activation_t *activation,
    uint16_t stable_id_ordinal,
    uint16_t visible_width,
    uint16_t visible_height) {
    (void)activation;
    (void)stable_id_ordinal;
    (void)visible_width;
    (void)visible_height;
    /* The storage backend already validates existence and physical bounds. */
    return true;
}

uint16_t inktag_access_image_visible_count(
    const inktag_activation_t *activation,
    uint16_t effective_user_record_count,
    uint16_t visible_width,
    uint16_t visible_height) {
    (void)activation;
    (void)visible_width;
    (void)visible_height;
    return effective_user_record_count;
}

inktag_status_v2_t inktag_access_image_write(
    const inktag_activation_t *activation,
    uint16_t effective_user_record_count,
    bool replacing_existing_record,
    uint16_t existing_record_ordinal,
    bool physical_space_available,
    uint16_t visible_width,
    uint16_t visible_height) {
    (void)activation;
    (void)effective_user_record_count;
    (void)replacing_existing_record;
    (void)existing_record_ordinal;
    (void)visible_width;
    (void)visible_height;
    /* Do not turn removal of an edition quota into an allocation bypass. */
    return physical_space_available ? INKTAG_STATUS_OK : INKTAG_STATUS_NO_SPACE;
}

inktag_status_v2_t inktag_access_input_action(
    const inktag_activation_t *activation,
    uint8_t source,
    uint8_t action) {
    if (!inktag_input_action_allowed((inktag_input_source_t)source, action)) {
        return INKTAG_STATUS_INVALID_REQUEST;
    }
    if (action == INKTAG_INPUT_ACTION_DISABLED) return INKTAG_STATUS_OK;
    if (source != INKTAG_INPUT_SOURCE_NFC) {
        /* Running an original board button is basic operation. Configuration
         * writers separately require BUTTON_BINDING; base adapters use only
         * the exact board's fixed pins and default bindings. */
        return inktag_access_check(
            activation, INKTAG_PRODUCT_FEATURE_BASIC,
            true, true, true);
    }
    /* NFC implementation is Pro-only; reserved input IDs do not grant it. */
    return inktag_access_check(
        activation, INKTAG_PRODUCT_FEATURE_NFC_ADVANCED,
        true, true, true);
}

inktag_status_v2_t inktag_access_indicator_policy(
    const inktag_activation_t *activation,
    uint8_t indicator_policy) {
    (void)activation;
    if (indicator_policy == INKTAG_STATUS_POLICY_OFF ||
        indicator_policy == INKTAG_STATUS_POLICY_ALL ||
        indicator_policy == INKTAG_STATUS_POLICY_CRITICAL) {
        return INKTAG_STATUS_OK;
    }
    return INKTAG_STATUS_INVALID_REQUEST;
}

inktag_status_v2_t inktag_access_bluetooth_name(
    const inktag_activation_t *activation,
    bool name_changed) {
    if (!name_changed) return INKTAG_STATUS_OK;
    return inktag_access_check(
        activation, INKTAG_PRODUCT_FEATURE_BLUETOOTH_NAME,
        true, true, true);
}

inktag_status_v2_t inktag_access_brand_hidden_setting(
    const inktag_activation_t *activation,
    bool hidden) {
    if (!hidden) return INKTAG_STATUS_OK;
    return inktag_access_check(
        activation, INKTAG_PRODUCT_FEATURE_BRAND_HIDE,
        true, true, true);
}

bool inktag_access_brand_should_render(
    const inktag_activation_t *activation,
    bool hidden_requested) {
    (void)activation;
    (void)hidden_requested;
    return true;

}

inktag_brand_edition_t inktag_access_brand_edition(
    const inktag_activation_t *activation,
    bool hidden_requested) {
    (void)activation;
    (void)hidden_requested;
    return INKTAG_BRAND_EDITION_DEFAULT;

}
