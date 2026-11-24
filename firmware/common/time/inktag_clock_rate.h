#ifndef INKTAG_CLOCK_RATE_H
#define INKTAG_CLOCK_RATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define INKTAG_CLOCK_RATE_MAX_PPM 4000
#define INKTAG_CLOCK_RATE_WIRE_BYTES 4u
#define INKTAG_CLOCK_RATE_REPORT_BYTES 6u

/* GET/SET_SETTINGS CLOCK_RATE v1 codec; no mutation on invalid input. */
bool inktag_clock_rate_decode(const uint8_t *payload, size_t length, int16_t *ppm);
/* Encode validated current/default values for GET and successful SET replies. */
void inktag_clock_rate_encode(int16_t ppm, int16_t default_ppm,
                              uint8_t payload[INKTAG_CLOCK_RATE_REPORT_BYTES]);

/* Zero initialization is the fallback uncompensated rate. The port initializes
 * from qualified board factory defaults via set() at normal boot. No Flash ownership.
 * Positive ppm speeds UTC up; negative ppm slows it down. The caller chooses
 * one fixed unit (preferably elapsed milliseconds or ticks, NOT callbacks or
 * already-truncated seconds) and retains this state across vendor sleep. */
typedef struct {
    uint32_t fraction;
    int16_t ppm;
} inktag_clock_rate_t;

/* Consume elapsed time at the old rate before changing it. This discards less
 * than one unit of correction phase, not the caller's UTC/subsecond value.
 * Also use after explicit host sync; never call on each sample/connection. */
bool inktag_clock_rate_set(inktag_clock_rate_t *rate, int16_t ppm);

/* Scale only UTC elapsed increments. Raw SDK/PM/radio/watchdog deadlines must
 * remain uncompensated. The existing elapsed-counter adapter owns wrap and
 * subsecond UTC accumulation. Equal total input has equal output regardless
 * of sample partitioning; error is less than one chosen unit. No float/64-bit
 * divide or extra wakeups. A uint32 overflow returns false without modifying
 * output or phase; bounded normal counter samples cannot hit it. */
bool inktag_clock_rate_apply(inktag_clock_rate_t *rate,
                             uint32_t elapsed, uint32_t *adjusted);

#endif
