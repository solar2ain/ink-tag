#ifndef INKTAG_SOAK_TELEMETRY_H
#define INKTAG_SOAK_TELEMETRY_H

#include <stdbool.h>
#include <stdint.h>

#ifndef INKTAG_ENABLE_SOAK_TELEMETRY
#define INKTAG_ENABLE_SOAK_TELEMETRY 0
#endif

#define INKTAG_HEALTH_BASE_WIRE_SIZE 56u
#define INKTAG_SOAK_TELEMETRY_MAGIC_0 0x49u /* I */
#define INKTAG_SOAK_TELEMETRY_MAGIC_1 0x53u /* S */
#define INKTAG_SOAK_TELEMETRY_VERSION 1u
#define INKTAG_SOAK_TELEMETRY_EXTENSION_SIZE 52u

#if INKTAG_ENABLE_SOAK_TELEMETRY
#define INKTAG_SOAK_TELEMETRY_WIRE_SIZE INKTAG_SOAK_TELEMETRY_EXTENSION_SIZE
#else
#define INKTAG_SOAK_TELEMETRY_WIRE_SIZE 0u
#endif

typedef enum {
    INKTAG_SOAK_FLAG_FIND_WINDOWS = 1u << 0,
    INKTAG_SOAK_FLAG_CONNECTION_SESSIONS = 1u << 1,
    INKTAG_SOAK_FLAG_RADIO_MAINTENANCE = 1u << 2,
    INKTAG_SOAK_FLAG_SLEEP_ENTRIES = 1u << 3,
    INKTAG_SOAK_FLAG_UPTIME = 1u << 4,
    INKTAG_SOAK_FLAG_FIND_ACTIVE = 1u << 8,
    INKTAG_SOAK_FLAG_SATURATED = 1u << 15,
} inktag_soak_telemetry_flag_t;

/* All counters are volatile RAM telemetry. A platform calls init once after
 * its monotonic counter is available, then records only scheduling/state
 * transitions. These APIs are not RF packet callbacks and never write Flash. */
void inktag_soak_telemetry_init(uint16_t available_flags, uint32_t now_ms);
void inktag_soak_telemetry_connection_started(void);
void inktag_soak_telemetry_find_requested(void);
void inktag_soak_telemetry_find_started(uint32_t now_ms);
void inktag_soak_telemetry_find_completed(uint32_t now_ms);
void inktag_soak_telemetry_find_start_failed(void);
void inktag_soak_telemetry_find_stop_failed(void);
void inktag_soak_telemetry_find_paused_for_connection(uint32_t now_ms);
void inktag_soak_telemetry_radio_maintenance(void);
void inktag_soak_telemetry_sleep_entered(void);

/* Encodes only the append-only extension. Callers keep their existing first
 * 56 health bytes byte-for-byte and append this result when it is non-zero. */
uint16_t inktag_soak_telemetry_encode(uint8_t *output,
                                      uint16_t output_size,
                                      uint32_t now_ms);

#endif
