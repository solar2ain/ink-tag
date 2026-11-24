#include "inktag_access.h"
#include <stddef.h>
#include "inktag_input.h"
#include "inktag_status.h"
static const inktag_product_feature_rule_t free_rule = {0u, false};
static const inktag_product_feature_rule_t experimental_rule = {0u, true};
const inktag_product_feature_rule_t *inktag_access_feature_rule(inktag_product_feature_t f) {
    if ((unsigned)f >= INKTAG_PRODUCT_FEATURE_COUNT) return NULL;
    switch (f) {
        case INKTAG_PRODUCT_FEATURE_BASIC:
        case INKTAG_PRODUCT_FEATURE_IMAGE_CAPACITY:
        case INKTAG_PRODUCT_FEATURE_NFC_BASIC: return &free_rule;
        case INKTAG_PRODUCT_FEATURE_EXPERIMENTAL_DISPLAY: return &experimental_rule;
        default: return NULL;
    }
}
bool inktag_access_is_pro(const inktag_activation_t *a) { (void)a; return false; }
inktag_status_v2_t inktag_access_check(const inktag_activation_t *a,
    inktag_product_feature_t f, bool implemented, bool capable, bool experiment) {
    (void)a;
    if (!implemented || !capable || (unsigned)f >= INKTAG_PRODUCT_FEATURE_COUNT)
        return INKTAG_STATUS_UNSUPPORTED;
    switch (f) {
        case INKTAG_PRODUCT_FEATURE_BASIC:
        case INKTAG_PRODUCT_FEATURE_IMAGE_CAPACITY:
        case INKTAG_PRODUCT_FEATURE_NFC_BASIC: return INKTAG_STATUS_OK;
        case INKTAG_PRODUCT_FEATURE_EXPERIMENTAL_DISPLAY:
            return experiment ? INKTAG_STATUS_OK : INKTAG_STATUS_LOCKED;
        default: return INKTAG_STATUS_DENIED;
    }
}
bool inktag_access_image_record_visible(const inktag_activation_t *a, uint16_t id,
    uint16_t w, uint16_t h) { (void)a;(void)w;(void)h;return id < UINT16_MAX; }
uint16_t inktag_access_image_visible_count(const inktag_activation_t *a,
    uint16_t n, uint16_t w, uint16_t h) { (void)a;(void)w;(void)h;return n; }
inktag_status_v2_t inktag_access_image_write(const inktag_activation_t *a,
    uint16_t n, bool replacing, uint16_t id, bool space, uint16_t w, uint16_t h) {
    (void)a;(void)n;(void)w;(void)h;
    if (!space) return INKTAG_STATUS_NO_SPACE;
    return replacing && id == UINT16_MAX ? INKTAG_STATUS_DENIED : INKTAG_STATUS_OK;
}
inktag_status_v2_t inktag_access_input_action(const inktag_activation_t *a,
    uint8_t source, uint8_t action) {
    (void)a;
    /* This compatibility entry authorizes custom actions, never stock-key
     * runtime. Stock defaults use BASIC; DA has no custom-input backend. */
    if (source < INKTAG_INPUT_SOURCE_NFC || source > INKTAG_INPUT_SOURCE_BUTTON_LONG ||
        action > INKTAG_INPUT_ACTION_PREVIOUS_PAGE)
        return INKTAG_STATUS_INVALID_REQUEST;
    return INKTAG_STATUS_DENIED;
}
inktag_status_v2_t inktag_access_indicator_policy(const inktag_activation_t *a,uint8_t p) {
    (void)a;
    return p == INKTAG_STATUS_POLICY_OFF || p == INKTAG_STATUS_POLICY_ALL ||
        p == INKTAG_STATUS_POLICY_CRITICAL ? INKTAG_STATUS_OK : INKTAG_STATUS_INVALID_REQUEST;
}
inktag_status_v2_t inktag_access_bluetooth_name(const inktag_activation_t *a,bool changed) {
    (void)a;return changed ? INKTAG_STATUS_DENIED : INKTAG_STATUS_OK;
}
inktag_status_v2_t inktag_access_brand_hidden_setting(const inktag_activation_t *a,bool hide) {
    (void)a;return hide ? INKTAG_STATUS_DENIED : INKTAG_STATUS_OK;
}
bool inktag_access_brand_should_render(const inktag_activation_t *a,bool requested) {
    (void)a;(void)requested;return true;
}
inktag_brand_edition_t inktag_access_brand_edition(const inktag_activation_t *a,bool requested) {
    (void)a;(void)requested;return INKTAG_BRAND_EDITION_FREE;
}
