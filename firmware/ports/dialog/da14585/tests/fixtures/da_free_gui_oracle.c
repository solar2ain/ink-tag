#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "inktag_gui.h"
static unsigned rows, pages;
static uint8_t buffer[2304];
static void capture(void *arg, uint8_t *black, uint8_t *red,
                    uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
    const gui_data_t *data = arg;
    assert(black && red && black != red && x == 0 && y == rows);
    assert(width == data->width && height && y + height <= data->height);
    unsigned count = ((width + 7u) / 8u) * height;
    printf("R %u %u\nB ", y, height);
    for (unsigned i = 0; i < count; ++i) printf("%02x", black[i]);
    printf("\nC ");
    for (unsigned i = 0; i < count; ++i) printf("%02x", red[i]);
    putchar('\n'); rows += height; ++pages;
}
int main(void) {
    static const unsigned sizes[][2] = {{128,296},{128,250},{400,300},
        {152,152},{200,200},{240,240},{208,200},{768,552}};
    for (unsigned size = 0; size < sizeof sizes / sizeof sizes[0]; ++size)
      for (unsigned mode = 1; mode <= 3; ++mode)
       for (unsigned rotation = 0; rotation < 4; ++rotation)
        for (unsigned minute = 0; minute < 2; ++minute)
         for (unsigned reserved = 0; reserved < 2; ++reserved) {
            gui_data_t data = {.mode=mode, .color=2, .width=sizes[size][0],
                .height=sizes[size][1], .visible_width=sizes[size][0],
                .visible_height=sizes[size][1], .force_rotation=true,
                .rotation=rotation, .timestamp=1790346480u + minute * 60u, .week_start=1,
                .temperature=GUI_TEMPERATURE_UNAVAILABLE, .voltage=3300,
                .activated=reserved, .brand_hidden=reserved,
                .board_name="da14585_asel42", .board_name_short="asel42",
                .bluetooth_name="IKT_DA_1234", .mcu_name="DA14585",
                .screen_info="native BWR"};
            strcpy(data.ssid,"Desk");
            gui_options_t options = {.color_plane=GFX_COLOR_PLANE_ALL,
                .prefer_largest_page=true, .render_buffer=buffer,
                .render_buffer_size=sizeof buffer};
            rows=pages=0;
            printf("O %u %u %u %u %u %u\n",data.width,data.height,mode,rotation,minute,reserved);
            assert(DrawGUIEx(&data,capture,&data,&options));
            assert(rows == data.height && pages);
            printf("E %u %u\n",rows,pages);
        }
    gui_data_t invalid={.mode=MODE_LOW_BATTERY,.width=400,.height=300,.color=2};
    gui_options_t options={.render_buffer=buffer,.render_buffer_size=sizeof buffer};
    rows=pages=0;
    assert(!DrawGUIEx(&invalid,capture,&invalid,&options));
    assert(rows==0 && pages==0);
    puts("INVALID rejected-without-callback");
    return 0;
}
