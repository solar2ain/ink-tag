static const uint8_t v1_word[9][7] = {
    {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, /* A */
    {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E}, /* G */
    {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F}, /* I */
    {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, /* K */
    {0x11, 0x19, 0x19, 0x15, 0x13, 0x13, 0x11}, /* N */
    {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, /* T */
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, /* O */
    {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}, /* P */
    {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}, /* R */
};

/* The two badge words share these nine existing glyphs. */
static void v1_block_word(Adafruit_GFX* gfx, int x, int y,
                          int scale, uint16_t color, bool pro) {
    static const uint8_t glyphs[] = {2,4,3,5,0,1,9,7,8,6};
    for (uint8_t i = 0u; i < (pro ? 10u : 6u); i++) {
        uint8_t glyph = glyphs[i];
        if (glyph == 9u) { x += 2*scale; continue; }
        for (int row = 0; row < 7; row++)
            for (int column = 0; column < 5; column++)
                if (v1_word[glyph][row] & (1u << (4-column)))
                    GFX_fillRect(gfx,x+column*scale,y+row*scale,scale,scale,color);
        x += 5*scale+1;
    }
}

static void v1_edition_badge(Adafruit_GFX* gfx, const gui_data_t* data,
                             int x, int y, int width,
                             int height, v1_align_t align) {
    if (data->activated && data->brand_hidden) return;
    if (!GFX_pageIntersectsLogicalY(gfx, y, height)) return;
    int scale = width >= 122 && height >= 22 ? 2 : 1;
    int padding = scale + 1;
    /* One physical pixel of tracking even at 2x, with a compact word space. */
    int badge_width = data->activated ? 49 * scale + 10 : 32 * scale + 7;
    int badge_height = v1_min(height, 7 * scale + padding * 2);
    int badge_x = x;
    if (align == V1_ALIGN_CENTER) badge_x = x + (width - badge_width) / 2;
    if (align == V1_ALIGN_RIGHT) badge_x = x + width - badge_width;
    int badge_y = y + (height - badge_height) / 2;
    uint16_t background = !data->activated && v1_has_red(data) ? V1_RED : V1_BLACK;
    GFX_fillRect(gfx, badge_x, badge_y, badge_width, badge_height, V1_WHITE);
    GFX_drawRoundRect(gfx, badge_x, badge_y, badge_width, badge_height,
                      v1_clamp(badge_height / 3, 2, 6), background);
    v1_block_word(gfx, badge_x + padding, badge_y + padding, scale,
                  background, data->activated);
}

