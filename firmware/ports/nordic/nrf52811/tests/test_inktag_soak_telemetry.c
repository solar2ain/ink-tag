#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "inktag_soak_telemetry.h"

static uint16_t get_u16(const uint8_t *input) {
    return (uint16_t)input[0] | ((uint16_t)input[1] << 8);
}

static uint32_t get_u32(const uint8_t *input) {
    return (uint32_t)input[0] |
           ((uint32_t)input[1] << 8) |
           ((uint32_t)input[2] << 16) |
           ((uint32_t)input[3] << 24);
}

int main(void) {
    uint8_t wire[INKTAG_SOAK_TELEMETRY_EXTENSION_SIZE];
    uint16_t flags = INKTAG_SOAK_FLAG_FIND_WINDOWS |
                     INKTAG_SOAK_FLAG_CONNECTION_SESSIONS |
                     INKTAG_SOAK_FLAG_RADIO_MAINTENANCE |
                     INKTAG_SOAK_FLAG_SLEEP_ENTRIES;
    inktag_soak_telemetry_init(flags, 0xFFFFFF00u);
    inktag_soak_telemetry_connection_started();
    inktag_soak_telemetry_find_requested();
    inktag_soak_telemetry_find_started(0xFFFFFF80u);
    inktag_soak_telemetry_radio_maintenance();
    inktag_soak_telemetry_sleep_entered();
    assert(inktag_soak_telemetry_encode(wire, sizeof(wire), 0x00000080u) ==
           INKTAG_SOAK_TELEMETRY_EXTENSION_SIZE);
    assert(wire[0] == INKTAG_SOAK_TELEMETRY_MAGIC_0);
    assert(wire[1] == INKTAG_SOAK_TELEMETRY_MAGIC_1);
    assert(wire[2] == INKTAG_SOAK_TELEMETRY_VERSION);
    assert(wire[3] == INKTAG_SOAK_TELEMETRY_EXTENSION_SIZE);
    assert((get_u16(&wire[4]) & INKTAG_SOAK_FLAG_FIND_ACTIVE) != 0u);
    assert(get_u32(&wire[8]) == 384u);
    assert(get_u32(&wire[12]) == 1u);
    assert(get_u32(&wire[16]) == 1u);
    assert(get_u32(&wire[20]) == 1u);
    assert(get_u32(&wire[40]) == 256u);
    assert(get_u32(&wire[44]) == 1u);
    assert(get_u32(&wire[48]) == 1u);

    inktag_soak_telemetry_find_paused_for_connection(0x00000180u);
    inktag_soak_telemetry_find_start_failed();
    inktag_soak_telemetry_find_stop_failed();
    assert(inktag_soak_telemetry_encode(wire, sizeof(wire), 0x00000200u) ==
           INKTAG_SOAK_TELEMETRY_EXTENSION_SIZE);
    assert((get_u16(&wire[4]) & INKTAG_SOAK_FLAG_FIND_ACTIVE) == 0u);
    assert(get_u32(&wire[28]) == 1u);
    assert(get_u32(&wire[32]) == 1u);
    assert(get_u32(&wire[36]) == 1u);
    assert(get_u32(&wire[40]) == 512u);
    assert(inktag_soak_telemetry_encode(wire, 51u, 0u) == 0u);
    puts("InkTag soak telemetry tests passed");
    return 0;
}
