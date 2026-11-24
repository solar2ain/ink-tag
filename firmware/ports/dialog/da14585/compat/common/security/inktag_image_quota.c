#include "inktag_image_quota.h"
uint16_t inktag_access_free_image_record_limit(uint16_t w,uint16_t h) {
    (void)w;(void)h;return INKTAG_FREE_IMAGE_RECORD_LIMIT_PHYSICAL;
}
