#ifndef __GUI_H
#define __GUI_H

#include <stdbool.h>

#include "Adafruit_GFX.h"
#include "inktag_device_display_name.h"

#define GUI_TEMPERATURE_UNAVAILABLE ((int8_t)-128)
#define GUI_COUNTDOWN_NAME_MAX_BYTES 12u
/* Icon ink only: independent of display/Flash/OTA voltage admission. */
#define GUI_BATTERY_RED_MAX_MV 2600u
/* Display-only reference; does not mark the clock valid or write a checkpoint. */
#define GUI_UNSET_DISPLAY_TIMESTAMP UINT32_C(1735689600)

typedef struct {
    const char* name;
    uint32_t target_day;
} gui_countdown_t;

typedef enum {
    MODE_PICTURE = 0,
    MODE_CALENDAR = 1,
    MODE_CLOCK = 2,
    MODE_DEVICE_INFO = 3,
    MODE_LOW_BATTERY = 4, /* Reserved retired page; DrawGUIEx rejects it. */
} display_mode_t;

typedef struct {
    display_mode_t mode;
    uint16_t color;
    uint16_t width;
    uint16_t height;
#if defined(INKTAG_GUI_RAW_VIEWPORT)
    uint16_t visible_x;
    uint16_t visible_y;
#endif
    uint16_t visible_width;
    uint16_t visible_height;
    bool force_rotation;
    GFX_Rotate rotation;
    bool mirror_x;
    bool mirror_y;
    uint8_t native_ui_scale;
    uint32_t timestamp;
    uint8_t week_start;  // 0: Sunday, 1: Monday
    const gui_countdown_t* countdown;
    int8_t temperature;
    uint16_t voltage;
    char ssid[INKTAG_DEVICE_LABEL_MAX_BYTES + 1u]; // independent display Name
    /* Caller-owned actual GAP name, stable through every synchronous page. */
    const char* bluetooth_name;
    bool activated; /* Reserved ABI field; ignored by this Free renderer. */
    bool brand_hidden; /* Reserved ABI field; the Free mark is mandatory. */
    const char* board_name;
    /* Display metadata, not a board/OTA identity. No suffix parsing fallback. */
    const char* board_name_short;
    const char* screen_info;
    const char* device_name;
    const char* mcu_name; // exact silicon, never a PCB alias
    const uint8_t* device_info_qr_data[1];
    uint8_t device_info_qr_modules[1];
} gui_data_t;

static inline bool GUI_SetDeviceName(gui_data_t* data, const uint8_t* label,
                                     uint8_t length, uint16_t id4) {
    return data != NULL && inktag_device_display_name(
        data->ssid, sizeof(data->ssid), label, length, id4);
}

typedef struct {
    GFX_ColorPlane color_plane;
    bool force_buffer_color;
    uint16_t buffer_color;
    /* Prefer one callback for the complete physical frame when it fits in
     * the configured heap.  This is useful for controllers whose DTM stream
     * must remain a single logical frame; allocation still falls back to the
     * normal bounded page height when the frame does not fit. */
    bool prefer_full_frame;
    /* Use all caller-provided render storage for a page when a complete frame
     * does not fit. The default remains the conservative 64-row allocation;
     * SRAM-constrained ports can opt into fewer repeated layout passes after
     * reserving enough stack headroom for their exact target. */
    bool prefer_largest_page;
    /* Optional caller-owned render storage.  This avoids libc malloc on
     * platforms where the SDK's linker heap and _sbrk arena are different. */
    uint8_t* render_buffer;
    uint32_t render_buffer_size;
} gui_options_t;

bool DrawGUIEx(gui_data_t* data, buffer_callback callback, void* callback_data, const gui_options_t* options);
bool DrawGUI(gui_data_t* data, buffer_callback callback, void* callback_data);
const char* GUI_EpdColorName(uint16_t color);
const char* GUI_BoardDisplayName(Adafruit_GFX* gfx, const gui_data_t* data,
                               int width, int height, uint16_t scale,
                               uint16_t minimum);
bool GUI_CountdownNameSupported(const char* name, uint8_t length);
void GUI_SetHolidaySchedule(uint16_t year, const uint16_t* holidays, uint8_t count);
void GUI_ClearHolidaySchedule(void);

#endif
