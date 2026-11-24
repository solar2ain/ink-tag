#ifndef INKTAG_GR_OTA_POLICY_H
#define INKTAG_GR_OTA_POLICY_H
#include "inktag_recovery_version.h"

/* Signed OTA compatibility policy shared by the GR5331 and GR551x fixed
 * Recoveries. Release versions identify builds but are not an anti-rollback
 * boundary. Increment this epoch only when an older signed application is
 * genuinely incompatible with durable state or the fixed Recovery contract.
 * The value is stored in dfu_image_info_t.version and is therefore covered by
 * the Goodix publisher signature. */
#define INKTAG_GR_OTA_COMPATIBILITY_EPOCH INKTAG_RECOVERY_EPOCH
/* App/source release 0.2.1 and Recovery 1.0.1 retain epoch 1. Software patch
 * versions do not force Recovery replacement. Signed family/layout markers,
 * exact addresses/bounds and native journal admission remain independent. */

#endif
