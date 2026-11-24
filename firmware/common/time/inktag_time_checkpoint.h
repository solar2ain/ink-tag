#ifndef INKTAG_TIME_CHECKPOINT_H
#define INKTAG_TIME_CHECKPOINT_H

#include <stdbool.h>
#include <stdint.h>

#define INKTAG_TIME_CHECKPOINT_INTERVAL_MS UINT32_C(3600000)
#define INKTAG_TIME_CHECKPOINT_RETRY_MS UINT32_C(60000)

/* Volatile policy only: no timer, UTC, Flash format, validity or power gate.
 * now_ms is the port's continuous RAW monotonic timeline, sampled at least
 * once per full uint32 wrap. Never pass UTC or ppm-adjusted elapsed time.
 * Callers defer while busy and retain ordinary time/voltage/storage refusals.
 * A definite failed/refused attempt can arm bounded retry; unknown storage
 * authority must keep the existing fail-closed path, not replay a write. */
typedef struct {
    uint32_t saved_at_ms;
    uint32_t attempted_at_ms;
    bool has_checkpoint;
    bool retry_pending;
} inktag_time_checkpoint_t;

/* A known existing checkpoint starts one ordinary hour at this boot anchor.
 * Without one, the first valid, idle, admitted snapshot may be saved now.
 * This does NOT make a persisted timestamp trusted after a cold boot. */
static inline void inktag_time_checkpoint_init(
    inktag_time_checkpoint_t *state, uint32_t now_ms, bool has_checkpoint)
{
    if (state == 0) return;
    state->saved_at_ms = now_ms;
    state->attempted_at_ms = now_ms;
    state->has_checkpoint = has_checkpoint;
    state->retry_pending = false;
}

/* Level-triggered and read-only: repeated checks or busy deferral add no jobs.
 * The port captures the newest UTC only when starting its admitted write. */
static inline bool inktag_time_checkpoint_due(
    const inktag_time_checkpoint_t *state, uint32_t now_ms)
{
    if (state == 0) return false;
    if (state->retry_pending)
        return (uint32_t)(now_ms - state->attempted_at_ms) >=
               INKTAG_TIME_CHECKPOINT_RETRY_MS;
    return !state->has_checkpoint ||
           (uint32_t)(now_ms - state->saved_at_ms) >=
               INKTAG_TIME_CHECKPOINT_INTERVAL_MS;
}

/* Call after verified success, including an existing explicit SET_TIME save.
 * Do not reset the policy merely for a connection or a refused clock write. */
static inline void inktag_time_checkpoint_saved(
    inktag_time_checkpoint_t *state, uint32_t now_ms)
{
    if (state == 0) return;
    state->saved_at_ms = now_ms;
    state->has_checkpoint = true;
    state->retry_pending = false;
}

/* Also retains a failed explicit save that occurred before the hourly due.
 * A later retry saves the then-current value, not a stale captured timestamp. */
static inline void inktag_time_checkpoint_failed(
    inktag_time_checkpoint_t *state, uint32_t now_ms)
{
    if (state == 0) return;
    state->attempted_at_ms = now_ms;
    state->retry_pending = true;
}

#endif
