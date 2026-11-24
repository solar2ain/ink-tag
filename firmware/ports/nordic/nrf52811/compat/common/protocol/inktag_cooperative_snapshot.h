#ifndef INKTAG_COOPERATIVE_SNAPSHOT_H
#define INKTAG_COOPERATIVE_SNAPSHOT_H

#include <stdbool.h>
#include "inktag_protocol_v2.h"

/* Bounded snapshots only. Read-only resource/settings queries can perform
 * storage scans and must not re-enter an in-progress storage transaction.
 * Ports retain the normal request/authentication parser and FIFO ordering. */
static inline bool inktag_protocol_cooperative_snapshot(uint8_t opcode) {
    switch (opcode) {
    case INKTAG_CMD_HELLO:
    case INKTAG_CMD_GET_CAPABILITIES:
    case INKTAG_CMD_GET_DEVICE_INFO:
    case INKTAG_CMD_GET_STATUS:
    case INKTAG_CMD_GET_HEALTH:
    case INKTAG_CMD_GET_TIME:
    case INKTAG_CMD_GET_SCREEN_PROFILE:
        return true;
    default:
        return false;
    }
}

#endif
