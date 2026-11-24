#ifndef INKTAG_GUI_FONT_CACHE_H
#define INKTAG_GUI_FONT_CACHE_H

#include <stdint.h>

/* Metadata only. Caller supplies non-null arrays of count entries, count>0,
 * current<count and keys other than UINT32_MAX. Counts are trusted build-time
 * budgets. Buffer allocation, IO, valid-length checks and failure/source
 * lifetime remain vendor-owned. Reset establishes ages as a permutation of
 * 0..count-1; touch preserves it without a wrapping timestamp counter. */
static inline void inktag_gui_font_cache_reset(
    uint32_t *offsets, uint16_t *lengths, uint8_t *ages,
    uint8_t count, uint8_t *current) {
    for (uint8_t i = 0u; i < count; ++i) {
        offsets[i] = UINT32_MAX;
        lengths[i] = 0u;
        ages[i] = i;
    }
    *current = 0u;
}

/* Selection has no side effects. On a miss the caller must complete its
 * bounded refill before publishing offsets[slot] and calling touch. */
static inline uint8_t inktag_gui_font_cache_choose(
    const uint32_t *offsets, const uint8_t *ages, uint8_t count,
    uint8_t current, uint32_t key) {
    if (offsets[current] == key) return current;
    uint8_t oldest = 0u;
    for (uint8_t slot = 0u; slot < count; ++slot) {
        if (offsets[slot] == key) return slot;
        if (ages[slot] > ages[oldest]) oldest = slot;
    }
    return oldest;
}

static inline void inktag_gui_font_cache_touch(
    uint8_t *ages, uint8_t count, uint8_t slot, uint8_t *current) {
    uint8_t age = ages[slot];
    for (uint8_t i = 0u; i < count; ++i)
        if (ages[i] < age) ++ages[i];
    ages[slot] = 0u;
    *current = slot;
}

#endif
