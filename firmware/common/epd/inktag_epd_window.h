#ifndef INKTAG_EPD_WINDOW_H
#define INKTAG_EPD_WINDOW_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "inktag_protocol_core.h"

/* Streaming 1bpp admission, not a panel capability. The caller owns only the
 * bounded next window. Complete, ordered physical rows are mandatory; pixels
 * outside the byte-aligned window contribute to the invalidation CRC. A CRC
 * is not authentication or proof of the optical/controller state. */
typedef struct {
    uint16_t width, height, x_byte, y, row_bytes, rows, next_y;
    uint8_t *next;
    uint32_t outside_crc;
    bool valid;
} inktag_epd_window_stream_t;

static inline bool inktag_epd_window_begin(inktag_epd_window_stream_t *s,
    uint16_t width, uint16_t height, uint16_t x, uint16_t y,
    uint16_t w, uint16_t h, uint8_t *next, uint16_t capacity) {
    if (!s) return false;
    memset(s, 0, sizeof(*s));
    if (!width || (width & 7u) || !height || (x & 7u) || !w ||
        (w & 7u) || !h || (uint32_t)x+w>width || (uint32_t)y+h>height ||
        !next || (uint32_t)(w/8u)*h>capacity) return false;
    s->width=width; s->height=height; s->x_byte=x/8u; s->y=y;
    s->row_bytes=w/8u; s->rows=h; s->next=next;
    s->outside_crc=UINT32_MAX; s->valid=true;
    return true;
}

static inline bool inktag_epd_window_feed(inktag_epd_window_stream_t *s,
    const uint8_t *black, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    if (!s || !s->valid) return false;
    if (!black || x || w!=s->width || y!=s->next_y || !h ||
        (uint32_t)y+h>s->height) { s->valid=false; return false; }
    const uint16_t stride=w/8u;
    for (uint16_t row=0; row<h; ++row, black+=stride) {
        if (y+row>=s->y && y+row<s->y+s->rows) {
            memcpy(s->next+(uint32_t)(y+row-s->y)*s->row_bytes,
                   black+s->x_byte,s->row_bytes);
            s->outside_crc=inktag_crc32_update(s->outside_crc,black,s->x_byte);
            uint16_t tail=s->x_byte+s->row_bytes;
            s->outside_crc=inktag_crc32_update(s->outside_crc,black+tail,stride-tail);
        } else s->outside_crc=inktag_crc32_update(s->outside_crc,black,stride);
    }
    s->next_y+=h;
    return true;
}

static inline bool inktag_epd_window_complete(const inktag_epd_window_stream_t *s) {
    return s && s->valid && s->next_y==s->height;
}

/* Daily clock maintenance is a civil-date boundary, not a frame counter.
 * Caller compares the renderer/font/profile key and commits history only
 * after the entire display transaction succeeds. */
static inline bool inktag_epd_window_same_day(uint32_t previous_local,
                                             uint32_t next_local) {
    /* Display timestamps are already local civil seconds, not raw UTC. A
     * delayed midnight callback still forces a full surrounding-date update. */
    return previous_local/86400u==next_local/86400u;
}

static inline bool inktag_epd_window_admit(bool history_valid, bool same_key,
    uint32_t elapsed_seconds, uint32_t old_outside,
    uint32_t new_outside) {
    return history_valid && same_key && elapsed_seconds>0u &&
        elapsed_seconds<=120u && old_outside==new_outside;
}
#endif
