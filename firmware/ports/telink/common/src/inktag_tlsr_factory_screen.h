#ifndef INKTAG_TLSR_FACTORY_SCREEN_H
#define INKTAG_TLSR_FACTORY_SCREEN_H
#include "inktag_epd_screen.h"
#include "inktag_tlsr_pin_layout.h"
/* ISC1: immutable Full-selected default, not a user override. Six-byte
 * header +17-byte shared screen wire +zero pad +FNV/commit =32B. Recovery
 * never interprets this envelope. Exact-unit preservation is a host gate. */
uint32_t inktag_fnv1a(const uint8_t *data,uint16_t length);
static inline bool inktag_tlsr_factory_screen_decode(
    const uint8_t data[32],uint8_t backend,uint8_t wire[EPD_SCREEN_WIRE_SIZE]) {
    epd_screen_config_t candidate;
    uint32_t hash=0u,commit=0u;
    if (!data || !backend || !wire || EPD_SCREEN_WIRE_SIZE!=17u) return false;
    for(uint8_t i=0u;i<4u;++i) {
        hash|=(uint32_t)data[24u+i]<<(8u*i);
        commit|=(uint32_t)data[28u+i]<<(8u*i);
    }
    if (data[0]!='I' || data[1]!='S' || data[2]!='C' || data[3]!='1' ||
        data[4]!=backend || data[5]!=EPD_SCREEN_WIRE_SIZE || data[23]!=0u ||
        hash!=inktag_fnv1a(data,24u) || commit!=UINT32_C(0x314d4353) ||
        epd_screen_config_decode_exact(data+6u,EPD_SCREEN_WIRE_SIZE,&candidate)!=EPD_SCREEN_STATUS_OK)
        return false;
    for(uint8_t i=0u;i<EPD_SCREEN_WIRE_SIZE;++i) wire[i]=data[6u+i];
    return true;
}
#endif
