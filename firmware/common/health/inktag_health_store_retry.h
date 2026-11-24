#ifndef INKTAG_HEALTH_STORE_RETRY_H
#define INKTAG_HEALTH_STORE_RETRY_H

#include <stdbool.h>
#include <stdint.h>

/* Failure may already have caused vendor GC. Retry only at an existing
 * service opportunity, at least this raw-monotonic interval later. No timer,
 * UTC dependency or periodic rewrite is implied. Success removes the floor. */
#define INKTAG_HEALTH_STORE_RETRY_INTERVAL_MS 60000u

static inline bool inktag_health_store_retry_due(bool retry_pending,
                                                uint32_t now_ms,
                                                uint32_t last_attempt_ms) {
    return !retry_pending ||
           (uint32_t)(now_ms - last_attempt_ms) >=
               INKTAG_HEALTH_STORE_RETRY_INTERVAL_MS;
}

#endif
