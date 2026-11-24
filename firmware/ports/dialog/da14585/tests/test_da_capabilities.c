#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include "inktag_access.h"
#include "inktag_device_status.h"
#include "inktag_status.h"
int main(void) {
    const inktag_activation_t *unused=(const inktag_activation_t *)(uintptr_t)UINTPTR_MAX;
    assert(INKTAG_ACTIVATION_STATUS_WIRE_SIZE==24);
    assert(!inktag_access_is_pro(unused));
    assert(inktag_access_brand_should_render(unused,true));
    assert(inktag_access_brand_edition(unused,true)==INKTAG_BRAND_EDITION_FREE);
    assert(inktag_access_brand_hidden_setting(unused,true)==INKTAG_STATUS_DENIED);
    assert(inktag_access_bluetooth_name(unused,true)==INKTAG_STATUS_DENIED);
    for (unsigned feature=0;feature<INKTAG_PRODUCT_FEATURE_COUNT+3;feature++) {
        assert(inktag_access_check(unused,(inktag_product_feature_t)feature,false,true,true)==INKTAG_STATUS_UNSUPPORTED);
        assert(inktag_access_check(unused,(inktag_product_feature_t)feature,true,false,true)==INKTAG_STATUS_UNSUPPORTED);
    }
    const inktag_product_feature_t omitted[]={INKTAG_PRODUCT_FEATURE_FIND_CONFIGURATION,
        INKTAG_PRODUCT_FEATURE_FIND_RUNTIME,INKTAG_PRODUCT_FEATURE_DEVICE_PASSWORD,
        INKTAG_PRODUCT_FEATURE_BRAND_HIDE,INKTAG_PRODUCT_FEATURE_BRAND_COLOR,
        INKTAG_PRODUCT_FEATURE_TIMED_SLEEP,INKTAG_PRODUCT_FEATURE_NFC_ADVANCED,
        INKTAG_PRODUCT_FEATURE_BUTTON_BINDING,INKTAG_PRODUCT_FEATURE_BUZZER};
    for (unsigned i=0;i<sizeof(omitted)/sizeof(omitted[0]);i++) {
        assert(inktag_access_feature_rule(omitted[i])==NULL);
        assert(inktag_access_check(unused,omitted[i],true,true,true)==INKTAG_STATUS_DENIED);
    }
    /* Stock physical-key/default-action runtime remains BASIC. A Free
     * compatibility stub must not authorize configurable actions. */
    assert(inktag_access_feature_rule(INKTAG_PRODUCT_FEATURE_BASIC)!=NULL);
    assert(inktag_access_check(unused,INKTAG_PRODUCT_FEATURE_BASIC,true,true,false)==INKTAG_STATUS_OK);
    assert(inktag_access_indicator_policy(unused,INKTAG_STATUS_POLICY_OFF)==INKTAG_STATUS_OK);
    assert(inktag_access_indicator_policy(unused,INKTAG_STATUS_POLICY_ALL)==INKTAG_STATUS_OK);
    assert(inktag_access_indicator_policy(unused,INKTAG_STATUS_POLICY_CRITICAL)==INKTAG_STATUS_OK);
    for(unsigned source=1;source<=4;source++)for(unsigned action=0;action<=10;action++)
        assert(inktag_access_input_action(unused,source,action)==INKTAG_STATUS_DENIED);
    uint32_t random=UINT32_C(0x50687973);
    for(unsigned i=0;i<50000;i++) {
        random=random*UINT32_C(1664525)+UINT32_C(1013904223);
        uint16_t n=(uint16_t)random,w=(uint16_t)(random>>16);
        assert(inktag_access_free_image_record_limit(w,n)==UINT16_MAX);
        assert(inktag_access_image_visible_count(unused,n,w,n)==n);
        assert(inktag_access_image_write(unused,n,false,0,false,w,n)==INKTAG_STATUS_NO_SPACE);
        assert(inktag_access_image_write(unused,n,false,0,true,w,n)==INKTAG_STATUS_OK);
        assert(inktag_access_image_record_visible(unused,n,w,n)==(n<UINT16_MAX));
    }
    return 0;
}
