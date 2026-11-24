#ifndef INKTAG_TLSR_PIN_RECORD_PILOT_H
#define INKTAG_TLSR_PIN_RECORD_PILOT_H
/* Offline/explicit pilot only until the shared persistence/wire review passes.
 * Pure decode: no Flash writes, GPIO mutation, default repair or capability.
 * The caller must separately validate the complete electrical/mux candidate. */
#ifndef INKTAG_TLSR_PIN_RECORD_PILOT
#error TLSR pin envelope is not enabled for shipping firmware
#endif
#include "inktag_pin_config.h"
#include "inktag_aux_io.h"
#include "inktag_settings_journal.h"
#include "inktag_tlsr_flash_layout.h"
#include "inktag_tlsr_pin_layout.h"
typedef enum {
    INKTAG_TLSR_PIN_RECORD_INVALID = 0,
    INKTAG_TLSR_PIN_RECORD_ABSENT = 1,
    INKTAG_TLSR_PIN_RECORD_VALID = 2
} inktag_tlsr_pin_record_result_t;
/* Existing vendor checksum implementation; corruption check, not authority. */
uint32_t inktag_fnv1a(const uint8_t *data, uint16_t length);
static inline uint32_t inktag_tlsr_pin_record_u32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8u)|((uint32_t)p[2]<<16u)|((uint32_t)p[3]<<24u);
}
/* Complete candidate check for both update directions. The 16-byte block
 * does not replace the legacy input record. UART routes have no
 * qualified TLSR backend yet, so TXD/RXD must remain disabled. The caller
 * must still run each component's own physical/qualification validator. */
#if INKTAG_BUILD_PRO
#include "inktag_tlsr_pin_aux_disjoint_pro.inc"
#endif
#if INKTAG_BUILD_PRO
#include "inktag_tlsr_pin_aux_complete_pro.inc"
#endif
/* ID7 v1 is the current three-button record in the new Settings namespace;
 * unknown/old data is not absence and is never repaired at boot. */
static inline bool inktag_tlsr_settings_aux_decode(const uint8_t *data,
    uint16_t length, inktag_aux_io_t *out) {
    uint8_t wire[INKTAG_AUX_IO_WIRE_SIZE]={1u,255u,0u,255u,0u,255u,0u};
    if (!data || length!=INKTAG_TLSR_PIN_SETTINGS_BYTES ||
        data[0]!=INKTAG_TLSR_PIN_SETTINGS_VERSION) return false;
    for (uint8_t i=INKTAG_TLSR_PIN_SETTINGS_RESERVED_OFFSET;i<64u;i++) if (data[i]) return false;
    wire[1]=data[7]; wire[2]=data[8];
    for (uint8_t i=0u;i<4u;i++) wire[3u+i]=data[56u+i];
    wire[7]=data[9]; wire[8]=data[10]; wire[9]=data[11];
    wire[10]=data[12]; wire[11]=data[13];
    return inktag_aux_io_decode(wire,sizeof(wire),out);
}
#if INKTAG_BUILD_PRO
#include "inktag_tlsr_aux_factory_pro.inc"
#endif
static inline inktag_tlsr_pin_record_result_t inktag_tlsr_pin_record_decode(
    const uint8_t *data, uint16_t length, uint8_t backend, bool required,
    inktag_pin_allowed_fn allowed, void *context, inktag_pin_config_t *out) {
    uint8_t all_ff=0xffu;
    inktag_pin_config_t candidate;
    if (!data||!out||!backend||!allowed||length!=INKTAG_TLSR_PIN_RECORD_BYTES)
        return INKTAG_TLSR_PIN_RECORD_INVALID;
    for (uint8_t i=0u;i<INKTAG_TLSR_PIN_RECORD_BYTES;i++) all_ff&=data[i];
    if (all_ff==0xffu) return required?INKTAG_TLSR_PIN_RECORD_INVALID:INKTAG_TLSR_PIN_RECORD_ABSENT;
    if (data[0]!='I'||data[1]!='P'||data[2]!='N'||data[3]!='1'||data[4]!=1u||
        data[5]!=INKTAG_PIN_CONFIG_SIZE||data[6]!=backend||data[7]!=0u||
        inktag_tlsr_pin_record_u32(data+24u)!=inktag_fnv1a(data,24u)||
        inktag_tlsr_pin_record_u32(data+28u)!=UINT32_C(0x314d4350)||
        !inktag_pin_config_decode(data+8u,INKTAG_PIN_CONFIG_SIZE,&candidate)||
        !inktag_pin_config_validate(&candidate,allowed,context)) return INKTAG_TLSR_PIN_RECORD_INVALID;
    *out=candidate;
    return INKTAG_TLSR_PIN_RECORD_VALID;
}
/* Current ID7 contains ordinary preferences, a pin extension and three button
 * slots. Old Settings are Full-reinitialized, never migrated. App
 * must additionally decode those preferences with its existing validators.
 * Recovery interprets only this extension and aux collision ownership, never
 * button actions, display recipes, password/Find or a Settings write path. */
static inline inktag_tlsr_pin_record_result_t inktag_tlsr_pin_settings_decode(
    const uint8_t *data, uint16_t length, inktag_pin_allowed_fn allowed,
    void *context, inktag_pin_config_t *out) {
    inktag_pin_config_t candidate;
    if (!data || !out || !allowed)
        return INKTAG_TLSR_PIN_RECORD_INVALID;
    if (length!=INKTAG_TLSR_PIN_SETTINGS_BYTES ||
        data[0]!=INKTAG_TLSR_PIN_SETTINGS_VERSION || data[39]>1u)
        return INKTAG_TLSR_PIN_RECORD_INVALID;
    for (uint8_t i=INKTAG_TLSR_PIN_SETTINGS_RESERVED_OFFSET;i<INKTAG_TLSR_PIN_SETTINGS_BYTES;++i)
        if (data[i]) return INKTAG_TLSR_PIN_RECORD_INVALID;
    if (!data[39]) {
        uint8_t nonzero=0u;
        for (uint8_t i=40u;i<56u;++i) nonzero|=data[i];
        return nonzero?INKTAG_TLSR_PIN_RECORD_INVALID:INKTAG_TLSR_PIN_RECORD_ABSENT;
    }
    if (!inktag_pin_config_decode(data+40u,INKTAG_PIN_CONFIG_SIZE,&candidate) ||
        !inktag_pin_config_validate(&candidate,allowed,context))
        return INKTAG_TLSR_PIN_RECORD_INVALID;
    *out=candidate;
    return INKTAG_TLSR_PIN_RECORD_VALID;
}
/* Fresh bounded snapshot, no cached pointer and no GPIO mutations. Caller
 * supplies a validated immutable default and a whole-candidate backend. On
 * any uncertainty, leave caller output untouched: never mix field defaults.
 * No generic fallback for a missing/damaged Factory envelope is supplied. */
/* Private vendor fast path for defaults already admitted by the complete
 * Factory/backend check. The caller owns that invariant for the lifetime of
 * the call; this does not relax validation of a fresh journal override. */
static inline bool inktag_tlsr_pin_settings_effective_prevalidated(
    const inktag_settings_journal_io_t *io, const inktag_pin_config_t *defaults,
    inktag_pin_allowed_fn allowed, void *context, inktag_pin_config_t *out,
    bool *overridden) {
    uint8_t data[INKTAG_TLSR_PIN_SETTINGS_BYTES];
    uint16_t length=0u;
    inktag_pin_config_t candidate;
    inktag_settings_journal_read_result_t read_result;
    inktag_tlsr_pin_record_result_t decoded;
    bool override=false;
    if (!io || io->start_address!=INKTAG_SETTINGS_SECTOR || !out || !overridden)
        return false;
    candidate=*defaults;
    read_result=inktag_settings_journal_readonly_result(io,
        INKTAG_SETTINGS_RECORD_DEVICE_SETTINGS,data,sizeof(data),&length);
    if (read_result==INKTAG_SETTINGS_JOURNAL_READ_ERROR) return false;
    if (read_result==INKTAG_SETTINGS_JOURNAL_READ_FOUND) {
#if INKTAG_BUILD_PRO
        inktag_aux_io_t aux;
#endif
        decoded=inktag_tlsr_pin_settings_decode(data,length,allowed,context,&candidate);
        if (decoded==INKTAG_TLSR_PIN_RECORD_INVALID) return false;
        override=decoded==INKTAG_TLSR_PIN_RECORD_VALID;
#if INKTAG_BUILD_PRO
#include "inktag_tlsr_pin_settings_aux_pro.inc"
#endif
    }
    *out=candidate;
    *overridden=override;
    return true;
}
#if INKTAG_BUILD_PRO
#include "inktag_tlsr_pin_effective_pro.inc"
#else
static inline bool inktag_tlsr_pin_settings_effective(
    const inktag_settings_journal_io_t *io, const inktag_pin_config_t *defaults,
    inktag_pin_allowed_fn allowed, void *context, inktag_pin_config_t *out,
    bool *overridden) {
    return io && io->start_address==INKTAG_SETTINGS_SECTOR && out && overridden &&
        inktag_pin_config_validate(defaults,allowed,context) &&
        defaults->pins[INKTAG_PIN_TXD]==INKTAG_PIN_UNUSED &&
        defaults->pins[INKTAG_PIN_RXD]==INKTAG_PIN_UNUSED &&
        inktag_tlsr_pin_settings_effective_prevalidated(io,defaults,allowed,context,out,overridden);
}
#endif
#endif
