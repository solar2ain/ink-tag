#ifndef INKTAG_OM_NFC_INTERFACE_H
#define INKTAG_OM_NFC_INTERFACE_H
#include "inktag_om_profile.h"
#include <stdbool.h>
#include <stdint.h>
#include "inktag_activation.h"
#define INKTAG_OM_NFC_STREAM_PILOT 0
#define INKTAG_OM_NFC_STREAM_DIAGNOSTICS 0
#define INKTAG_OM_NFC_BLOCK_BYTES 16u
#define INKTAG_OM_NFC_UID_BYTES 7u
#define INKTAG_OM_NFC_NDEF_MAX_BYTES 0u
#define INKTAG_OM_NFC_COMMAND_MASK 0u
typedef struct { uint8_t last_result,last_opcode; uint32_t last_sequence; } inktag_om_nfc_status_t;
typedef enum {INKTAG_OM_NFC_BOOT_WAIT,INKTAG_OM_NFC_BOOT_DONE,INKTAG_OM_NFC_BOOT_ERROR} inktag_om_nfc_boot_result_t;
typedef struct inktag_om_nfc_event inktag_om_nfc_event_t;
void inktag_om_nfc_init(void);
void inktag_om_nfc_set_activation(const inktag_activation_t *activation);
bool inktag_om_nfc_supported(void);
bool inktag_om_nfc_present(void);
uint8_t inktag_om_nfc_backend_selector(void);
inktag_om_nfc_boot_result_t inktag_om_nfc_boot_restore_url(const uint8_t *name,uint8_t length);
void inktag_om_nfc_display_begin(uint32_t now_ms);
bool inktag_om_nfc_command_tag_matches(const uint8_t block0[16]);
bool inktag_om_nfc_provision_default_url(const uint8_t *name,uint8_t length);
bool inktag_om_nfc_restore_managed_url(const uint8_t *name,uint8_t length);
bool inktag_om_nfc_managed_url_matches(const uint8_t *name,uint8_t length);
bool inktag_om_nfc_probe(uint8_t uid[7]);
bool inktag_om_nfc_read_identity(uint8_t block0[16]);
bool inktag_om_nfc_read_user(uint16_t offset,uint8_t *data,uint16_t length);
bool inktag_om_nfc_take_event(uint32_t now_ms,inktag_om_nfc_event_t *event);
bool inktag_om_nfc_set_static_ndef(const uint8_t *ndef,uint16_t length);
void inktag_om_nfc_process(void);
bool inktag_om_nfc_commit_sequence(uint32_t sequence);
void inktag_om_nfc_abort_command(uint8_t result);
void inktag_om_nfc_get_status(inktag_om_nfc_status_t *status);
bool inktag_om_nfc_read_ndef(uint8_t *output,uint16_t capacity,uint16_t *length);
#endif
