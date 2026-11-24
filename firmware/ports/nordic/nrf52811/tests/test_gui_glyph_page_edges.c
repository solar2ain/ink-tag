/* Real renderer regression for the portable page-edge fix accepted in
 * common component f7b75caa1ebef670f76c327fec08601cfaf7f117. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "Adafruit_GFX.h"
#include "inktag_gui_fonts.h"

enum { WIDTH = 128, HEIGHT = 128, ROW_BYTES = WIDTH / 8,
       FRAME_BYTES = ROW_BYTES * HEIGHT };

typedef struct {
    uint8_t *pixels;
    uint16_t next_y;
} capture_t;

static void capture(void *user, uint8_t *black, uint8_t *color,
                    uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
    capture_t *out = user;
    assert(black && !color && x == 0u && width == WIDTH);
    assert(y == out->next_y && height > 0u && y + height <= HEIGHT);
    memcpy(out->pixels + (uint32_t)y * ROW_BYTES, black,
           (uint32_t)height * ROW_BYTES);
    out->next_y += height;
}

static void render(uint8_t *output, uint16_t page, uint16_t scale,
                   uint16_t glyph, int16_t y, const uint8_t *font,
                   GFX_Rotate rotation) {
    uint8_t buffer[FRAME_BYTES];
    Adafruit_GFX gfx;
    capture_t out = {output, 0u};
    memset(output, 0xff, FRAME_BYTES);
    GFX_begin_with_buffer(&gfx, WIDTH, HEIGHT, page, buffer, sizeof(buffer));
    GFX_setRotation(&gfx, rotation);
    GFX_setFont(&gfx, font);
    GFX_setFontScale(&gfx, scale);
    GFX_setFontMode(&gfx, 1u);
    GFX_setTextColor(&gfx, GFX_BLACK, GFX_WHITE);
    GFX_firstPage(&gfx);
    do {
        assert(GFX_drawGlyph(&gfx, 32, y, glyph) > 0);
    } while (GFX_nextPage(&gfx, capture, &out));
    assert(out.next_y == HEIGHT);
    GFX_end(&gfx);
}

int main(void) {
    const uint8_t *fonts[] = {u8g2_font_wqy9_t_lunar,
                              u8g2_font_wqy12_t_lunar};
    const uint16_t scales[] = {128, 224, 256, 280, 320, 448, 512, 1024};
    const int16_t baselines[] = {7, 8, 15, 16, 17, 31, 32,
                                 33, 63, 64, 65, 95, 96};
    const uint16_t glyphs[] = {'g', 'j', 'p', 'q', 'y', '0', 0x65e5, 0x4e00};
    const uint16_t pages[] = {3, 8, 17, 32};
    uint8_t full[FRAME_BYTES], paged[FRAME_BYTES];
    unsigned comparisons = 0u;

    for (unsigned f = 0; f < sizeof(fonts) / sizeof(*fonts); ++f)
    for (unsigned s = 0; s < sizeof(scales) / sizeof(*scales); ++s)
    for (unsigned g = 0; g < sizeof(glyphs) / sizeof(*glyphs); ++g)
    for (unsigned y = 0; y < sizeof(baselines) / sizeof(*baselines); ++y)
    for (unsigned rotation = 0; rotation < 4u; ++rotation) {
        /* The fixed WQY12 subset contains digits/CJK but no Latin letters.
         * Missing glyphs must not count as blank full/paged comparisons. */
        if (f == 1u && g < 5u) continue;
        render(full, HEIGHT, scales[s], glyphs[g], baselines[y], fonts[f],
               (GFX_Rotate)rotation);
        if (scales[s] == 256u &&
            baselines[y] == 64 && rotation == 0u) {
            unsigned ink = 0u;
            for (unsigned i = 0; i < sizeof(full); ++i)
                ink += full[i] != 0xffu;
            assert(ink > 0u); /* A missing font cannot pass as blank parity. */
        }
        for (unsigned p = 0; p < sizeof(pages) / sizeof(*pages); ++p) {
            render(paged, pages[p], scales[s], glyphs[g], baselines[y],
                   fonts[f], (GFX_Rotate)rotation);
            if (memcmp(full, paged, sizeof(full))) {
                fprintf(stderr,
                        "glyph page mismatch: font=%u scale=%u glyph=%u y=%d rotation=%u page=%u\n",
                        f, scales[s], glyphs[g], baselines[y], rotation, pages[p]);
                return 1;
            }
            ++comparisons;
        }
    }
    printf("PASS %u real glyph full/paged comparisons\n", comparisons);
    return 0;
}
