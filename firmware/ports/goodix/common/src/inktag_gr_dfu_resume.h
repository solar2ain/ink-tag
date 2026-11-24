#ifndef INKTAG_GR_DFU_RESUME_H
#define INKTAG_GR_DFU_RESUME_H

#include <stdbool.h>
#include <stdint.h>

/* Native OTAS queue accounting only. Image, voltage, journal and mutation
 * policy remain in the existing Recovery guard. The SDK's void frame API is
 * not evidence of queue/TX success: only successful notification completions
 * for every byte of the admitted response can arm a main-context return. */
typedef struct {
    uint32_t queued_bytes;
    uint16_t notifications;
    uint16_t response_remaining;
    uint16_t response_ahead;
    bool pending;
    bool expecting_response;
    bool ready;
    bool invalid;
} inktag_gr_dfu_resume_tx_t;

static inline void inktag_gr_resume_cancel(inktag_gr_dfu_resume_tx_t *s) {
    s->pending = s->expecting_response = s->ready = false;
    s->response_remaining = s->response_ahead = 0u;
}

static inline void inktag_gr_resume_fail(inktag_gr_dfu_resume_tx_t *s) {
    inktag_gr_resume_cancel(s);
    s->invalid = true;
}

static inline bool inktag_gr_resume_prepare(inktag_gr_dfu_resume_tx_t *s) {
    if (s->invalid || s->pending || s->queued_bytes || s->notifications) return false;
    s->pending = s->expecting_response = true;
    return true;
}

static inline void inktag_gr_resume_queued(inktag_gr_dfu_resume_tx_t *s,
                                         uint16_t payload_length) {
    uint32_t bytes = (uint32_t)payload_length + 8u; /* Native DG frame. */
    if (s->pending) {
        if (!s->expecting_response || bytes > UINT16_MAX) {
            inktag_gr_resume_fail(s);
        } else {
            s->expecting_response = false;
            s->response_remaining = (uint16_t)bytes;
        }
    }
    if (UINT32_MAX - s->queued_bytes < bytes) inktag_gr_resume_fail(s);
    else s->queued_bytes += bytes;
}

static inline void inktag_gr_resume_sent(inktag_gr_dfu_resume_tx_t *s,
                                       uint16_t length, bool success) {
    if (!success || !length || s->queued_bytes < length ||
        s->notifications == UINT16_MAX) {
        inktag_gr_resume_fail(s);
        return;
    }
    s->queued_bytes -= length;
    ++s->notifications;
    if (s->pending) {
        if (s->expecting_response || s->response_remaining < length) {
            inktag_gr_resume_fail(s);
            return;
        }
        s->response_remaining -= length;
        if (!s->response_remaining) s->response_ahead = s->notifications;
    }
}

static inline void inktag_gr_resume_completed(inktag_gr_dfu_resume_tx_t *s) {
    if (!s->notifications) {
        inktag_gr_resume_fail(s);
        return;
    }
    --s->notifications;
    if (s->pending && s->response_ahead && !--s->response_ahead) s->ready = true;
}

static inline void inktag_gr_resume_disconnected(inktag_gr_dfu_resume_tx_t *s) {
    /* Disconnect is never TX evidence; preserve only an already completed ACK. */
    if (!s->ready) inktag_gr_resume_cancel(s);
    s->queued_bytes = 0u;
    s->notifications = 0u;
    s->invalid = true;
}

static inline bool inktag_gr_resume_take_ready(inktag_gr_dfu_resume_tx_t *s) {
    if (!s->pending || !s->ready) return false;
    inktag_gr_resume_cancel(s);
    return true;
}

#endif
