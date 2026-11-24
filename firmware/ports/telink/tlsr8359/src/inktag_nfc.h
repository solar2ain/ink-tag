#include "../../common/src/inktag_tlsr_build_profile.h"
#if INKTAG_BUILD_PRO
#include "inktag_nfc_pro.h"
#else
#ifndef INKTAG_TLSR_NFC_UNSUPPORTED_ABI_H
#define INKTAG_TLSR_NFC_UNSUPPORTED_ABI_H
#include <stdbool.h>
#include <stdint.h>
void inktag_nfc_init(void);
void inktag_nfc_start_after_ble(uint32_t);
bool inktag_nfc_present(void);
bool inktag_nfc_probe(void);
bool inktag_nfc_irq_asserted(void);
bool inktag_nfc_service_pending(void);
bool inktag_nfc_busy(void);
void inktag_nfc_latch_irq(void);
#endif
#endif
