#include "inktag_soak_telemetry.h"

#include <string.h>

#if INKTAG_ENABLE_SOAK_TELEMETRY

#ifndef INKTAG_SOAK_TELEMETRY_STORAGE
#define INKTAG_SOAK_TELEMETRY_STORAGE
#endif

typedef struct {
    uint16_t flags;
    bool initialized;
    bool find_active;
    uint32_t boot_ms;
    uint32_t find_started_ms;
    uint32_t connection_sessions;
    uint32_t find_requests;
    uint32_t find_starts;
    uint32_t find_completions;
    uint32_t find_start_failures;
    uint32_t find_stop_failures;
    uint32_t find_connection_pauses;
    uint32_t find_active_ms;
    uint32_t radio_maintenance_turns;
    uint32_t sleep_entries;
} inktag_soak_state_t;

/* Ports whose low-power state retains only a dedicated SRAM section may
 * provide a storage attribute without moving the common state machine into
 * vendor code. */
static INKTAG_SOAK_TELEMETRY_STORAGE inktag_soak_state_t s_soak;

static void put_u16(uint8_t *output, uint16_t value) {
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
}

static void put_u32(uint8_t *output, uint32_t value) {
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
    output[2] = (uint8_t)(value >> 16);
    output[3] = (uint8_t)(value >> 24);
}

static void saturating_add(uint32_t *value, uint32_t amount) {
    if (!value || amount == 0u) return;
    if (*value > UINT32_MAX - amount) {
        *value = UINT32_MAX;
        s_soak.flags |= INKTAG_SOAK_FLAG_SATURATED;
    } else {
        *value += amount;
    }
}

static void saturating_increment(uint32_t *value) {
    saturating_add(value, 1u);
}

static void close_find_window(uint32_t now_ms) {
    if (!s_soak.find_active) return;
    saturating_add(&s_soak.find_active_ms, now_ms - s_soak.find_started_ms);
    s_soak.find_active = false;
}

void inktag_soak_telemetry_init(uint16_t available_flags, uint32_t now_ms) {
    memset(&s_soak, 0, sizeof(s_soak));
    s_soak.flags = (uint16_t)(available_flags &
        (INKTAG_SOAK_FLAG_FIND_WINDOWS |
         INKTAG_SOAK_FLAG_CONNECTION_SESSIONS |
         INKTAG_SOAK_FLAG_RADIO_MAINTENANCE |
         INKTAG_SOAK_FLAG_SLEEP_ENTRIES));
    s_soak.flags |= INKTAG_SOAK_FLAG_UPTIME;
    s_soak.boot_ms = now_ms;
    s_soak.initialized = true;
}

void inktag_soak_telemetry_connection_started(void) {
    if (s_soak.initialized &&
        (s_soak.flags & INKTAG_SOAK_FLAG_CONNECTION_SESSIONS) != 0u)
        saturating_increment(&s_soak.connection_sessions);
}

void inktag_soak_telemetry_find_requested(void) {
    if (s_soak.initialized &&
        (s_soak.flags & INKTAG_SOAK_FLAG_FIND_WINDOWS) != 0u)
        saturating_increment(&s_soak.find_requests);
}

void inktag_soak_telemetry_find_started(uint32_t now_ms) {
    if (!s_soak.initialized ||
        (s_soak.flags & INKTAG_SOAK_FLAG_FIND_WINDOWS) == 0u) return;
    close_find_window(now_ms);
    saturating_increment(&s_soak.find_starts);
    s_soak.find_started_ms = now_ms;
    s_soak.find_active = true;
}

void inktag_soak_telemetry_find_completed(uint32_t now_ms) {
    if (!s_soak.initialized ||
        (s_soak.flags & INKTAG_SOAK_FLAG_FIND_WINDOWS) == 0u) return;
    if (!s_soak.find_active) {
        saturating_increment(&s_soak.find_stop_failures);
        return;
    }
    close_find_window(now_ms);
    saturating_increment(&s_soak.find_completions);
}

void inktag_soak_telemetry_find_start_failed(void) {
    if (s_soak.initialized &&
        (s_soak.flags & INKTAG_SOAK_FLAG_FIND_WINDOWS) != 0u)
        saturating_increment(&s_soak.find_start_failures);
}

void inktag_soak_telemetry_find_stop_failed(void) {
    if (s_soak.initialized &&
        (s_soak.flags & INKTAG_SOAK_FLAG_FIND_WINDOWS) != 0u)
        saturating_increment(&s_soak.find_stop_failures);
}

void inktag_soak_telemetry_find_paused_for_connection(uint32_t now_ms) {
    if (!s_soak.initialized ||
        (s_soak.flags & INKTAG_SOAK_FLAG_FIND_WINDOWS) == 0u) return;
    if (s_soak.find_active) close_find_window(now_ms);
    saturating_increment(&s_soak.find_connection_pauses);
}

void inktag_soak_telemetry_radio_maintenance(void) {
    if (s_soak.initialized &&
        (s_soak.flags & INKTAG_SOAK_FLAG_RADIO_MAINTENANCE) != 0u)
        saturating_increment(&s_soak.radio_maintenance_turns);
}

void inktag_soak_telemetry_sleep_entered(void) {
    if (s_soak.initialized &&
        (s_soak.flags & INKTAG_SOAK_FLAG_SLEEP_ENTRIES) != 0u)
        saturating_increment(&s_soak.sleep_entries);
}

uint16_t inktag_soak_telemetry_encode(uint8_t *output,
                                      uint16_t output_size,
                                      uint32_t now_ms) {
    uint16_t flags;
    uint32_t active_ms;
    if (!output || output_size < INKTAG_SOAK_TELEMETRY_EXTENSION_SIZE)
        return 0u;
    memset(output, 0, INKTAG_SOAK_TELEMETRY_EXTENSION_SIZE);
    output[0] = INKTAG_SOAK_TELEMETRY_MAGIC_0;
    output[1] = INKTAG_SOAK_TELEMETRY_MAGIC_1;
    output[2] = INKTAG_SOAK_TELEMETRY_VERSION;
    output[3] = INKTAG_SOAK_TELEMETRY_EXTENSION_SIZE;
    if (!s_soak.initialized) return INKTAG_SOAK_TELEMETRY_EXTENSION_SIZE;
    flags = s_soak.flags;
    if (s_soak.find_active) flags |= INKTAG_SOAK_FLAG_FIND_ACTIVE;
    active_ms = s_soak.find_active_ms;
    if (s_soak.find_active) {
        uint32_t elapsed = now_ms - s_soak.find_started_ms;
        if (active_ms > UINT32_MAX - elapsed) {
            active_ms = UINT32_MAX;
            flags |= INKTAG_SOAK_FLAG_SATURATED;
        } else {
            active_ms += elapsed;
        }
    }
    put_u16(&output[4], flags);
    put_u32(&output[8], now_ms - s_soak.boot_ms);
    put_u32(&output[12], s_soak.connection_sessions);
    put_u32(&output[16], s_soak.find_requests);
    put_u32(&output[20], s_soak.find_starts);
    put_u32(&output[24], s_soak.find_completions);
    put_u32(&output[28], s_soak.find_start_failures);
    put_u32(&output[32], s_soak.find_stop_failures);
    put_u32(&output[36], s_soak.find_connection_pauses);
    put_u32(&output[40], active_ms);
    put_u32(&output[44], s_soak.radio_maintenance_turns);
    put_u32(&output[48], s_soak.sleep_entries);
    return INKTAG_SOAK_TELEMETRY_EXTENSION_SIZE;
}

#else

void inktag_soak_telemetry_init(uint16_t available_flags, uint32_t now_ms) {
    (void)available_flags;
    (void)now_ms;
}
void inktag_soak_telemetry_connection_started(void) {}
void inktag_soak_telemetry_find_requested(void) {}
void inktag_soak_telemetry_find_started(uint32_t now_ms) { (void)now_ms; }
void inktag_soak_telemetry_find_completed(uint32_t now_ms) { (void)now_ms; }
void inktag_soak_telemetry_find_start_failed(void) {}
void inktag_soak_telemetry_find_stop_failed(void) {}
void inktag_soak_telemetry_find_paused_for_connection(uint32_t now_ms) { (void)now_ms; }
void inktag_soak_telemetry_radio_maintenance(void) {}
void inktag_soak_telemetry_sleep_entered(void) {}
uint16_t inktag_soak_telemetry_encode(uint8_t *output,
                                      uint16_t output_size,
                                      uint32_t now_ms) {
    (void)output;
    (void)output_size;
    (void)now_ms;
    return 0u;
}

#endif
