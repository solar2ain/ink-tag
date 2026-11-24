#ifndef INKTAG_DA_OTA_POLICY_H
#define INKTAG_DA_OTA_POLICY_H

/* Native OTA target/epoch context for the common DA14585 AESL application. Release versions
 * identify builds but are not an anti-rollback boundary. Increment the epoch
 * only when an older image is incompatible with durable state or the
 * fixed Recovery contract. */
#define INKTAG_DA_OTA_BOARD_ID "da14585_aesl"
#define INKTAG_DA_OTA_LAYOUT_ID "layout-v1"
#define INKTAG_DA_OTA_BOARD_SIGNATURE      0x42584144u /* "DAXB" */
#define INKTAG_DA_OTA_LAYOUT_SIGNATURE     0x31564144u /* "DAV1" */
#define INKTAG_DA_OTA_VERIFIER_SIGNATURE   0x314B4144u /* "DAK1" */
#define INKTAG_DA_OTA_COMPATIBILITY_EPOCH  1u

/* Actual read-only VERSION SP01 policy is permanently CRC-only (2).
 * Optional/required implementations are retained only in historical sources. */
#define INKTAG_DA_OTA_POLICY_CODE 2u
#define INKTAG_DA_OTA_UNSIGNED_MAGIC      0x31554B49u /* "IKU1" */
#define INKTAG_DA_OTA_UNSIGNED_VERIFIER   0x31554144u /* "DAU1" */
#define INKTAG_DA_OTA_UNSIGNED_ALGORITHM  0u

#endif
