#ifndef INKTAG_TLSR_OTA_POLICY_H
#define INKTAG_TLSR_OTA_POLICY_H

/* Signed OTA policy for the one common Hanshow TLSR8359 application.
 * Release versions identify builds and remain freely upgradeable or
 * downgradeable within one compatibility epoch. Increment the epoch only
 * when an older signed application can no longer use the durable state or
 * fixed-Recovery contract safely. */
#define INKTAG_TLSR_OTA_BOARD_ID "tlsr8359_hanshow"
#define INKTAG_TLSR_OTA_LAYOUT_ID "layout-v1"
#define INKTAG_TLSR_OTA_BOARD_SIGNATURE       0x53484C54u /* "TLHS" */
/* Fresh release-v1 context; actual security epochs remain unchanged. */
#define INKTAG_TLSR_OTA_LAYOUT_SIGNATURE      0x31564C54u
#define INKTAG_TLSR_OTA_VERIFIER_SIGNATURE    0x314B4C54u /* "TLK1" */
/* Epoch1 predates upper-stock exclusion. Protected selectors must never
 * resume/install an old signed App that ignores that durable constraint. */
#define INKTAG_TLSR_OTA_COMPATIBILITY_EPOCH   1u
#define INKTAG_TLSR_OTA_UPPER_STOCK_MIN_EPOCH 2u

/* Preserve the already-deployed Recovery Journal identity. It is a journal
 * schema discriminator, not the signed full-image layout identifier above. */
#define INKTAG_TLSR_RECOVERY_JOURNAL_SIGNATURE 0x544C5231u

#endif
