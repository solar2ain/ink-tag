#ifndef INKTAG_IMAGE_QUOTA_H
#define INKTAG_IMAGE_QUOTA_H
#include <stdint.h>
/* No commercial limit. The transactional store still enforces actual capacity. */
#define INKTAG_FREE_IMAGE_RECORD_LIMIT_PHYSICAL UINT16_MAX
uint16_t inktag_access_free_image_record_limit(uint16_t visible_width,uint16_t visible_height);
#endif
