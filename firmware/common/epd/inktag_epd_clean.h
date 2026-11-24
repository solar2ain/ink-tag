#ifndef INKTAG_EPD_CLEAN_H
#define INKTAG_EPD_CLEAN_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "inktag_epd_driver.h"
#include "inktag_epd_screen.h"

/* Maintenance uses the registered transfer format, not a GUI renderer or a
 * vendor-specific solid-image encoding. The caller owns power and bus I/O. */
static inline bool inktag_epd_clean_fill(epd_model_t *epd, bool black_plane,
                                        uint8_t value, uint32_t bytes,
                                        void (*maintain)(void)) {
    uint8_t buffer[64];
    bool begin = true;
    memset(buffer, value, sizeof(buffer));
    while (bytes != 0u) {
        uint8_t count = bytes > sizeof(buffer) ? sizeof(buffer) : (uint8_t)bytes;
        if (EPD_AbortRequested() ||
            !EPD_DriverWriteRam(epd, begin, black_plane, buffer, count)) return false;
        begin = false;
        bytes -= count;
        if (maintain) maintain();
    }
    return !EPD_AbortRequested();
}

static inline bool inktag_epd_clean_solid(epd_model_t *epd,
                                         const epd_screen_t *screen, bool white,
                                         void (*maintain)(void)) {
    uint32_t bytes = screen->frame_bytes;
    uint8_t value = white ? 0xFFu : 0x00u;
    bool two_planes = false;
    bool chromatic_white = false;
    switch (screen->transfer_format) {
        case EPD_SCREEN_TRANSFER_DUAL_1BPP:
            chromatic_white = true;
            two_planes = true;
            break;
        case EPD_SCREEN_TRANSFER_PLANAR_2X1BPP:
            two_planes = true;
            break;
        case EPD_SCREEN_TRANSFER_INDEXED_2X2BPP:
            two_planes = true;
            /* fall through */
        case EPD_SCREEN_TRANSFER_INDEXED_2BPP:
            value = white ? 0x55u : 0x00u;
            break;
        case EPD_SCREEN_TRANSFER_INDEXED_4BPP:
            value = white ? 0x11u : 0x00u;
            break;
        case EPD_SCREEN_TRANSFER_MONO_1BPP:
            break;
        default:
            return false;
    }
    if (two_planes) bytes /= 2u;
    if (!inktag_epd_clean_fill(epd, true, value, bytes, maintain)) return false;
    return !two_planes || inktag_epd_clean_fill(epd, false,
        chromatic_white ? 0xFFu : value, bytes, maintain);
}

typedef bool (*inktag_epd_clean_pass_t)(void *context, bool white);
typedef void (*inktag_epd_clean_progress_t)(void *context, uint8_t done,
                                           uint8_t total);
typedef void (*inktag_epd_clean_cleanup_t)(void *context);

/* One cycle is WHITE -> BLACK -> WHITE. Reinitialize and verify each physical
 * pass in the adapter. A failed/aborted black pass gets at most one bounded
 * best-effort white cleanup; cleanup never turns failure into success. */
static inline bool inktag_epd_clean_run(void *context, uint8_t cycles,
    inktag_epd_clean_pass_t pass, inktag_epd_clean_progress_t progress,
    inktag_epd_clean_cleanup_t cleanup) {
    uint8_t total = (uint8_t)(cycles * 3u);
    bool white = false;
    if (cycles < 1u || cycles > 10u || !pass) return false;
    for (uint8_t done = 0u; done < total; ++done) {
        bool target_white = done % 3u != 1u;
        white = false;
        if (!pass(context, target_white)) {
            if (cleanup) cleanup(context);
            return false;
        }
        white = target_white;
        if (progress) progress(context, (uint8_t)(done + 1u), total);
    }
    return white;
}
#endif
