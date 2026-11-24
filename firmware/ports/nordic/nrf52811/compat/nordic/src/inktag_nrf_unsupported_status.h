#ifndef INKTAG_NRF_UNSUPPORTED_STATUS_H
#define INKTAG_NRF_UNSUPPORTED_STATUS_H
#include <stdint.h>
#include "inktag_protocol_v2.h"

static inline uint8_t inktag_nrf_screen_omitted(const uint8_t *payload, uint16_t length)
{
    return length == INKTAG_SCREEN_PROFILE_V2_WIRE_SIZE &&
           payload[0] == INKTAG_SCREEN_PROFILE_V2_VERSION &&
           (payload[1] == INKTAG_CONTROLLER_JD79665CA ||
            payload[1] == INKTAG_CONTROLLER_UC8151 ||
            payload[1] == INKTAG_CONTROLLER_SSD1675 ||
            payload[1] == INKTAG_CONTROLLER_UC8159 ||
            (payload[1] == INKTAG_CONTROLLER_SSD1677 &&
             payload[3] == INKTAG_COLOR_GRAY4));
}

/* Frozen protocol allocation bitmap, checked against commands.json by host
 * regression. Reserved/unallocated bytes remain UNKNOWN_COMMAND. A known
 * command omitted by this constrained port must explicitly be UNSUPPORTED.
 * 0x1E is the shared 2026-10-06 SET_IMAGE_METADATA allocation; this snapshot
 * does not implement or advertise IMAGE_METADATA. Keep its response explicit
 * without changing the frozen generated bindings or reusing retired 0x1D. */
static inline inktag_status_v2_t inktag_nrf_unhandled_command_status(uint8_t opcode)
{
    static const uint8_t allocated[] = {
        0xfeu, 0xffu, 0x3fu, 0x7fu, 0x3fu, 0x0fu, 0x0fu, 0x00u,
        0xffu, 0x03u, 0xffu, 0x01u, 0x00u, 0x00u, 0xffu,
    };
    return opcode < sizeof(allocated) * 8u &&
           (allocated[opcode >> 3u] & (1u << (opcode & 7u))) != 0u ?
           INKTAG_STATUS_UNSUPPORTED : INKTAG_STATUS_UNKNOWN_COMMAND;
}
#endif
