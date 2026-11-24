#ifndef INKTAG_RECOVERY_INTEGRITY_POLICY_H
#define INKTAG_RECOVERY_INTEGRITY_POLICY_H

/* Wire facts from protocol/recovery-verification-policy-v1.json.
 * Emitted only by a Recovery which implements the corresponding policy.
 * This does not select or bypass a verifier by itself. */
#define INKTAG_RECOVERY_POLICY_OPTIONAL_SIGNATURE 0u
#define INKTAG_RECOVERY_POLICY_SIGNATURE_REQUIRED 1u
#define INKTAG_RECOVERY_POLICY_CRC_ONLY 2u
#define INKTAG_RECOVERY_INTEGRITY_INFO_BYTES \
    {'I','K','S','P',1u,INKTAG_RECOVERY_POLICY_CRC_ONLY}
#define INKTAG_RECOVERY_NORDIC_CRC_PROTOCOL 0xe1u

#endif
