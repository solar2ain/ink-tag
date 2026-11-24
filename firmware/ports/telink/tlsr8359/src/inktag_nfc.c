#include "../../common/src/inktag_tlsr_build_profile.h"
#if INKTAG_BUILD_PRO
#include "inktag_nfc_pro.inc"
#else
#include "inktag_nfc.h"
void inktag_nfc_init(void) {}
void inktag_nfc_start_after_ble(uint32_t now_ms) { (void)now_ms; }
bool inktag_nfc_present(void) { return false; }
bool inktag_nfc_probe(void) { return false; }
bool inktag_nfc_irq_asserted(void) { return false; }
bool inktag_nfc_service_pending(void) { return false; }
bool inktag_nfc_busy(void) { return false; }
void inktag_nfc_latch_irq(void) {}
#endif
