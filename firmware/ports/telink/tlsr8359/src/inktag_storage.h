#ifndef INKTAG_TLSR_STORAGE_H
#define INKTAG_TLSR_STORAGE_H

#include <stdbool.h>
#include <stdint.h>
#include "inktag_tlsr.h"
#include "inktag_epd_screen.h"
#include "inktag_input.h"
#if INKTAG_BUILD_PRO
#include "inktag_nfc_binding.h"
#endif
#include "inktag_display_settings.h"
#include "inktag_settings_records.h"
#include "inktag_settings_journal.h"
#include "inktag_status.h"
#include "inktag_tlsr_aux_io.h"
/* Keep the existing synchronous protocol boundary while the common core owns
 * the asynchronous transaction. These names are IO/event adapters, not another
 * allocation engine. All shared grammar/types come from the frozen v5 header. */
#define inktag_image_store_begin inktag_fixed_store_begin
#define inktag_image_store_commit inktag_fixed_store_commit
#define inktag_image_store_abort inktag_fixed_store_abort
#define inktag_image_store_delete inktag_fixed_store_delete
#define inktag_image_store_clear_incompatible inktag_fixed_store_clear_incompatible
#define inktag_image_store_clear_all inktag_fixed_store_clear_all
#include "inktag_image_fixed_store.h"
#undef inktag_image_store_begin
#undef inktag_image_store_commit
#undef inktag_image_store_abort
#undef inktag_image_store_delete
#undef inktag_image_store_clear_incompatible
#undef inktag_image_store_clear_all

typedef enum {
    INKTAG_SETTINGS_RECORD_READ_ERROR = 0,
    INKTAG_SETTINGS_RECORD_READ_MISSING = 1,
    INKTAG_SETTINGS_RECORD_READ_FOUND = 2,
} inktag_settings_record_read_result_t;

/* RAM model, never a persisted/wire memcpy. Explicit codecs define ID7.
 * Do not take naturally aligned common-aux pointers from a packed container.
 * Native TC32 still applies its existing whole-program -fpack-struct ABI.
 * Keep word fields first and the RAM object aligned, so its explicit codecs
 * do not pay for bytewise word access. No persisted offsets come from here. */
typedef struct __attribute__((aligned(4))) {
    uint32_t timestamp;
    uint32_t slideshow_interval;
    int16_t timezone_minutes;
    uint16_t display_internal_flags;
    uint8_t display_mode;
    uint8_t display_argument;
    inktag_display_settings_t display_settings;
    uint8_t name_length;
    uint8_t label_length;
    uint8_t name[19];
    uint8_t label[24];
    uint8_t screen_config[EPD_SCREEN_WIRE_SIZE];
    /* Historical mode bytes are decoded only to preserve the existing URI
     * authority. Never execute them, migrate them at boot or change the ABI. */
#if INKTAG_BUILD_PRO
#include "inktag_settings_nfc_fields_pro.inc"
#else
    uint8_t reserved_input[2];
#endif
    inktag_status_config_t led;
#if INKTAG_BUILD_PRO
#include "inktag_settings_custom_fields_pro.inc"
#else
    uint8_t legacy_input[17]; /* Opaque ID7 bytes, never an effective route. */
#endif
    /* One bounded ID7 extension, not another journal or per-board image. */
    bool pin_override_present;
    uint8_t pin_override[16];
} inktag_settings_t;

void inktag_storage_init(inktag_board_id_t board);
/* Internal half of inktag_storage_init(), split so settings and image records
 * cannot accidentally share a persistence format or erase domain. */
void inktag_settings_init(inktag_board_id_t board);
uint16_t inktag_image_store_sector_count(void);
inktag_settings_t *inktag_settings(void);
/* A decoded last snapshot exists; this does NOT prove current UTC after
 * power loss/reset. Cold App clock trust requires a successful SET_TIME. */
bool inktag_settings_time_valid(void);
/* Malformed present ID7 is not absence; routing consumers must fail closed. */
bool inktag_settings_device_valid(void);
bool inktag_settings_screen_valid(void);
bool inktag_settings_screen_commit(const epd_screen_config_t *screen);
/* Internal GPIO snapshot adapter; same bounded IO/allowlist as Main Settings.
 * The readonly scanner ignores mutation callbacks. Not a protocol read API. */
const inktag_settings_journal_io_t *inktag_settings_gpio_readonly_io(void);
/* The routing backend must first validate the entire effective candidate
 * against immutable package/mux/pin ownership facts and check idle/AUTH.
 * Storage independently checks ID7 shape, aux conflicts and fresh power.
 * false may still mean committed: caller must read actual state, never replay. */
bool inktag_settings_pin_commit(const uint8_t effective[16], bool override);
bool inktag_settings_aux_commit(const inktag_tlsr_aux_io_config_t *aux);
bool inktag_tlsr_factory_aux(inktag_tlsr_aux_io_config_t *aux);
bool inktag_settings_save(void);
/* Ordinary preference saves never piggyback advancing TIME. */
bool inktag_settings_save_time(void);
bool inktag_settings_time_checkpoint_ready(void);
bool inktag_settings_checkpoint_time(void);
#if INKTAG_BUILD_PRO
#include "inktag_settings_nfc_api_pro.inc"
#endif
bool inktag_settings_clear(inktag_board_id_t board,
                           inktag_settings_clear_action_t action);
bool inktag_settings_apply_default_name(const uint8_t *name, uint8_t length);
/* Activation, Find enable/mode, and the per-device Find derivation seed are
 * protected Main Settings records. BLE never exposes the stored seed. */
inktag_settings_record_read_result_t inktag_settings_protected_read(
    uint16_t record_id, void *data, uint16_t capacity, uint16_t *length);
bool inktag_settings_protected_write(
    uint16_t record_id, const void *data, uint16_t length);
bool inktag_settings_protected_delete(uint16_t record_id);
bool inktag_settings_find_public_provision_safe(void);
bool inktag_settings_find_public_storage_safe(void);
bool inktag_settings_find_public_replace(
    const void *record, uint16_t length);
#if INKTAG_BUILD_PRO
bool inktag_settings_find_public_unregister(void);
#endif
bool inktag_settings_device_info_qr_available(void);
/* ID5 is the single password/QR authority. No additional Flash allocation. */
#include "inktag_device_password_store.h"
inktag_device_password_store_result_t inktag_settings_password_load(
    uint8_t *scratch, inktag_device_password_config_t *config,
    uint8_t *modules, uint16_t *offset, uint16_t *length);
bool inktag_settings_password_commit(uint8_t *scratch, uint16_t length,
    inktag_device_password_config_t *actual);
/* RESET_SETTINGS only: after fresh W and successful ordinary cleanup.
 * Always reload the password authority and invalidate AUTH after this call. */
bool inktag_settings_password_clear(void);
inktag_image_store_result_t inktag_image_store_begin(const epd_screen_config_t *screen,
    uint8_t format, uint8_t segment_count, uint8_t black_mask, uint8_t content_type,
    const uint32_t segment_size[2], uint32_t payload_size, uint32_t payload_crc,
    uint32_t created_at, const char *name, uint8_t replace_id, uint8_t *id);
inktag_image_store_result_t inktag_image_store_commit(uint8_t *id);
void inktag_image_store_abort(void);
bool inktag_image_store_record_ordinal(uint8_t id, uint16_t *ordinal);
bool inktag_image_store_find_name(const uint8_t *text, uint8_t length, uint8_t *id);
inktag_image_store_result_t inktag_image_store_delete(uint8_t id);
inktag_image_store_result_t inktag_image_store_clear_incompatible(
    const epd_screen_config_t *screen, uint16_t *removed);
inktag_image_store_result_t inktag_image_store_clear_all(uint16_t *removed);

#endif
