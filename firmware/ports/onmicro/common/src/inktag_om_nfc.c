/* base unsupported ABI. No NFC driver, parser, NDEF, state or storage. */
#include "inktag_om_nfc.h"
#include <string.h>
void inktag_om_nfc_init(void) {}
void inktag_om_nfc_set_activation(const inktag_activation_t *a) {(void)a;}
bool inktag_om_nfc_supported(void) {return false;}
bool inktag_om_nfc_present(void) {return false;}
uint8_t inktag_om_nfc_backend_selector(void) {return 0;}
inktag_om_nfc_boot_result_t inktag_om_nfc_boot_restore_url(const uint8_t *n,uint8_t l) {(void)n;(void)l;return INKTAG_OM_NFC_BOOT_ERROR;}
void inktag_om_nfc_display_begin(uint32_t n) {(void)n;}
bool inktag_om_nfc_command_tag_matches(const uint8_t b[16]) {(void)b;return false;}
bool inktag_om_nfc_provision_default_url(const uint8_t *n,uint8_t l) {(void)n;(void)l;return false;}
bool inktag_om_nfc_restore_managed_url(const uint8_t *n,uint8_t l) {(void)n;(void)l;return false;}
bool inktag_om_nfc_managed_url_matches(const uint8_t *n,uint8_t l) {(void)n;(void)l;return false;}
bool inktag_om_nfc_probe(uint8_t u[7]) {(void)u;return false;}
bool inktag_om_nfc_read_identity(uint8_t b[16]) {(void)b;return false;}
bool inktag_om_nfc_read_user(uint16_t o,uint8_t *d,uint16_t n) {(void)o;(void)d;(void)n;return false;}
bool inktag_om_nfc_take_event(uint32_t n,inktag_om_nfc_event_t *e) {(void)n;(void)e;return false;}
bool inktag_om_nfc_set_static_ndef(const uint8_t *p,uint16_t n) {(void)p;(void)n;return false;}
void inktag_om_nfc_process(void) {}
bool inktag_om_nfc_commit_sequence(uint32_t n) {(void)n;return false;}
void inktag_om_nfc_abort_command(uint8_t r) {(void)r;}
void inktag_om_nfc_get_status(inktag_om_nfc_status_t *s) {if(s)memset(s,0,sizeof(*s));}
bool inktag_om_nfc_read_ndef(uint8_t *p,uint16_t n,uint16_t *l) {(void)p;(void)n;if(l)*l=0;return false;}
