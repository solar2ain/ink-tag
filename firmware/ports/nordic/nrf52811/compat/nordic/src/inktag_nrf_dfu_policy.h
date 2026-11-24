#ifndef INKTAG_NRF_DFU_POLICY_H
#define INKTAG_NRF_DFU_POLICY_H
/* Exact wire values. 0x81/0xc1 describe installed historical Recovery only;
 * they are not build options. Current Recovery has no publisher verifier. */
#define INKTAG_NRF_DFU_PROTOCOL_SIGNED_REQUIRED 0x81u
#define INKTAG_NRF_DFU_PROTOCOL_UNSIGNED_ALLOWED 0xc1u
#define INKTAG_NRF_DFU_PROTOCOL_CRC_ONLY 0xe1u
#define INKTAG_NRF_DFU_RESUME_PROTOCOL INKTAG_NRF_DFU_PROTOCOL_CRC_ONLY
#endif
