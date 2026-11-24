#ifndef INKTAG_IMAGE_QUOTA_H
#define INKTAG_IMAGE_QUOTA_H

#include <stdint.h>

#define INKTAG_COMPACT_SCREEN_MAX_EDGE_EXCLUSIVE 300u
#define INKTAG_DEFAULT_IMAGE_RECORD_LIMIT_NORMAL 4u
#define INKTAG_DEFAULT_IMAGE_RECORD_LIMIT_COMPACT 10u
#define INKTAG_DEFAULT_IMAGE_RECORD_LIMIT_MAX INKTAG_DEFAULT_IMAGE_RECORD_LIMIT_COMPACT

uint16_t inktag_access_default_image_record_limit(
    uint16_t visible_width,
    uint16_t visible_height);

/* Both editions use physical capacity. UINT16_MAX is an ABI sentinel only;
 * never allocate an ID snapshot from it. Retired 4/10 constants above remain
 * temporarily for unchanged bounded platform callers during extraction;
 * new clear operations use the backend's existing transactional clear paths. */

#endif
