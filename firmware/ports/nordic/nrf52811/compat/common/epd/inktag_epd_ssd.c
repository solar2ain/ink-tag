/* Generic controller execution/transfer implementation: InkTag, 2026-11-04.
 * Specification: docs/research/2026-11-04-tlsr-ssd-uc-rewrite-spec.md.
 * Historical EPD-nRF5 lineage and existing project license are retained;
 * panel-specific stock waveforms below have unresolved redistribution provenance.
 */
#include "inktag_epd_driver.h"
#include "inktag_epd_temperature.h"

#include <string.h>

#if defined(INKTAG_NRF_SDK17)
#include "inktag_board.h"
#endif

bool SSD1xxx_ReadBusy(epd_model_t* epd) { (void)epd; return EPD_ReadBusy(); }

static void SSD1xxx_WaitBusy(uint16_t timeout) { EPD_WaitBusy(true, timeout); }

static void SSD1680_WaitBusy(epd_model_t* epd, uint16_t timeout) {
    EPD_WaitBusy(epd->color == COLOR_BW ? false : true, timeout);
}

#ifndef INKTAG_EPD_SSD1619_SMALL_REFRESH_TIMEOUT_MS
#define INKTAG_EPD_SSD1619_SMALL_REFRESH_TIMEOUT_MS 30000u
#endif
_Static_assert(INKTAG_EPD_SSD1619_SMALL_REFRESH_TIMEOUT_MS > 0u &&
               INKTAG_EPD_SSD1619_SMALL_REFRESH_TIMEOUT_MS <= UINT16_MAX,
               "small SSD1619 refresh wait must remain bounded");
static void SSD1619_SmallWaitBusy(uint16_t timeout) {
    (void)EPD_WaitBusyEdge(true, 1000u, timeout);
}

static void SSD1xxx_Update(uint8_t seq) {
    EPD_Write(SSD1xxx_DISP_CTRL2, seq);
    EPD_WriteCmd(SSD1xxx_MASTER_ACTIVATE);
}

static bool SSD1xxx_IsHanshowOld200(const epd_model_t* epd) {
#if INKTAG_EPD_ENABLE_HANSHOW_OLD_200
    return epd != NULL && epd->driver_id == EPD_SCREEN_DRIVER_SSD1XXX &&
           epd->panel_recipe == INKTAG_RECIPE_SSD1XXX_HANSHOW_200_BWR_OLD &&
           epd->width == 200u && epd->height == 200u && epd->color == COLOR_BWR;
#else
    (void)epd;
    return false;
#endif
}

#if INKTAG_EPD_ENABLE_HANSHOW_OLD_200
static bool SSD1xxx_IsHanshowNative200(const epd_model_t* epd) {
    return epd != NULL && epd->driver_id == EPD_SCREEN_DRIVER_SSD1XXX &&
           epd->panel_recipe == INKTAG_RECIPE_SSD1XXX_HANSHOW_HSEL5_154M02_NATIVE_OTP &&
           epd->width == 200u && epd->height == 200u && epd->color == COLOR_BWR;
}

static bool SSD1xxx_IsHanshowStock200(const epd_model_t* epd) {
    return SSD1xxx_IsHanshowOld200(epd) || SSD1xxx_IsHanshowNative200(epd);
}
#endif

#if INKTAG_EPD_ENABLE_HANSHOW_OLD_200
static bool s_hanshow_old200_ok;
static void HanshowOld200_Pointer(uint16_t y) {
    EPD_Write(SSD1xxx_RAM_XCOUNT, 0x00);
    EPD_Write(SSD1xxx_RAM_YCOUNT, (uint8_t)(199u - y), 0x00);
}
static int8_t HanshowOld200_Temperature(void) {
    uint8_t raw[2] = {0xFFu, 0xFFu};
    EPD_WriteCmd(SSD1xxx_TSENSOR_READ);
    EPD_ReadData(raw, sizeof(raw));
    int8_t value = (int8_t)raw[0];
    if ((raw[0] == 0xFFu && raw[1] == 0xFFu) || value < -40 || value > 85)
        s_hanshow_old200_ok = false;
    return value;
}
static bool HanshowOld200_Activate(uint8_t sequence, uint16_t timeout) {
    SSD1xxx_Update(sequence);
    s_hanshow_old200_ok = EPD_WaitBusyEdge(true, 1000, timeout) && !EPD_AbortRequested();
    return s_hanshow_old200_ok;
}
static void HanshowOld200_Init(epd_model_t* epd) {
    /* Confirmed 3D1C4/3D2F8 stock normal-BWR path, not Nowa's X=1/152
     * recipe or the unrelated external-LUT voltages. The stock status/depth
     * overrides (border4/7 and conditional 1A55) remain unqualified; this
     * candidate exposes only the normal cold BWR branch and no fast mode. */
    static const uint8_t setup[] = {
        SSD1xxx_SOFTSTART, 4u, 0x8B, 0x9C, 0x96, 0x0F,
        SSD1xxx_GDO_CTR, 3u, 0xC7, 0x00, 0x00,
        SSD1xxx_ENTRY_MODE, 1u, 0x01,
        SSD1xxx_RAM_XPOS, 2u, 0x00, 0x18,
        SSD1xxx_RAM_YPOS, 4u, 0xC7, 0x00, 0x00, 0x00,
        SSD1xxx_BORDER_CTRL, 1u, 0x05,
        SSD1xxx_TSENSOR_CTRL, 1u, 0x80,
    };
    s_hanshow_old200_ok = false;
    EPD_Reset(true, 200);
    EPD_WriteCmd(SSD1xxx_SW_RESET);
    if (!EPD_WaitBusyEdge(true, 1000, 5000) || EPD_AbortRequested()) return;
    if (SSD1xxx_IsHanshowNative200(epd)) {
        /* Exact _02 stock3DA34 normal branch. The controller's OTP owns
         * addressing/LUT: no borrowed old-P154 window or entry commands. */
        static const uint8_t native_setup[] = {
            SSD1xxx_BORDER_CTRL, 1u, 0x05,
            SSD1xxx_TSENSOR_CTRL, 1u, 0x80,
        };
        EPD_WriteInitCommands(native_setup, sizeof(native_setup));
    } else EPD_WriteInitCommands(setup, sizeof(setup));
    if (!HanshowOld200_Activate(0xB1, 5000)) return;
    (void)HanshowOld200_Temperature();
    if (!s_hanshow_old200_ok) return;
    if (!SSD1xxx_IsHanshowNative200(epd)) HanshowOld200_Pointer(0u);
}
#endif

static bool SSD1619_IsSmall213(epd_model_t* epd) {
    return epd->ic == DRV_IC_SSD1619 && epd->width <= 128 && epd->height <= 250;
}

static bool SSD1619_IsHema213(epd_model_t* epd) {
    return epd->driver_id == EPD_SCREEN_DRIVER_SSD1619 &&
           epd->panel_recipe == INKTAG_RECIPE_SSD1619_HEMA_213_BWR &&
           epd->width == 128 && epd->height == 250 &&
           epd->color == COLOR_BWR;
}

static bool SSD1xxx_IsSSD1683(epd_model_t* epd) {
#if INKTAG_EPD_ENABLE_SSD1683
    return epd->ic == DRV_IC_SSD1683;
#else
    (void)epd;
    return false;
#endif
}


static bool SSD1680_Is213Bwr(epd_model_t* epd) {
    return epd->ic == DRV_IC_SSD1680 && epd->color == COLOR_BWR && epd->width <= 128 && epd->height == 250;
}

#if INKTAG_EPD_ENABLE_SSD1677
static bool SSD1677_Is426(epd_model_t* epd) {
    return epd->ic == DRV_IC_SSD1677 && epd->width == 800 && epd->height == 480;
}


#else
static bool SSD1677_Is426(epd_model_t* epd) {
    (void)epd;
    return false;
}
#endif

static bool SSD1xxx_HasInvertedColorRam(epd_model_t* epd) {
    /* InkTag color buffers use 0 for a colored pixel. SSD1680/SSD1683 red
     * RAM uses 1 for red and 0 for non-red, so the second plane must be
     * inverted for both clear/fill and streamed image data. */
    return (epd->ic == DRV_IC_SSD1680 || SSD1xxx_IsSSD1683(epd) ||
            SSD1xxx_IsHanshowOld200(epd)) && epd->color == COLOR_BWR;
}



int8_t SSD1xxx_ReadTemp(epd_model_t* epd) {
    uint8_t temperature[2] = {0xFFu, 0xFFu};

#if INKTAG_EPD_ENABLE_HANSHOW_OLD_200
    if (SSD1xxx_IsHanshowStock200(epd)) {
        if (SSD1xxx_IsHanshowNative200(epd) &&
            (!s_hanshow_old200_ok || EPD_AbortRequested())) return INT8_MIN;
        if (!HanshowOld200_Activate(0xB1, 5000)) return -127;
        return HanshowOld200_Temperature();
    }
#endif
    SSD1xxx_Update(0xB1);
    SSD1xxx_WaitBusy(500);
    EPD_WriteCmd(SSD1xxx_TSENSOR_READ);
    /* SSD1677 R1B is a 12-bit signed fixed-point value: A11..A4 followed
     * by A3..A0 in the high nibble of the second byte. Clock both bytes in
     * one CS-low interval; callers currently expose whole degrees. */
    EPD_ReadData(temperature, sizeof(temperature));
    return (int8_t)temperature[0];
}


static void SSD1xxx_SetWindow(epd_model_t* epd, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    bool descending = SSD1680_Is213Bwr(epd) || SSD1677_Is426(epd);
    bool pixel_x = false;
#if INKTAG_EPD_ENABLE_SSD1677
    pixel_x = epd->ic == DRV_IC_SSD1677;
#endif
    if (SSD1619_IsSmall213(epd) && !SSD1619_IsHema213(epd)) x += 8;
    if (SSD1677_Is426(epd)) y = epd->height - y - h;
    /* Endpoint expressions deliberately stay promoted signed arithmetic. */
    int end_x = x + w - 1;
    int end_y = y + h - 1;
    EPD_Write(SSD1xxx_ENTRY_MODE, descending ? 0x01 : 0x03);
    if (pixel_x) {
        EPD_Write(SSD1xxx_RAM_XPOS, x % 256, x / 256, end_x % 256, end_x / 256);
    } else {
        EPD_Write(SSD1xxx_RAM_XPOS, x / 8, end_x / 8);
    }
    if (descending) {
        /* Small1680 used a uint16 endpoint before the bus conversion. */
        if (pixel_x) {
            EPD_Write(SSD1xxx_RAM_YPOS, end_y % 256, end_y / 256, y % 256, y / 256);
        } else {
            uint16_t top = (uint16_t)end_y;
            EPD_Write(SSD1xxx_RAM_YPOS, top & 255, top >> 8, y & 255, y >> 8);
        }
    } else if (!pixel_x) {
        /* A repeated Y-window transaction is part of the accepted bus ABI. */
        EPD_Write(SSD1xxx_RAM_YPOS, y % 256, y / 256, end_y % 256, end_y / 256);
    }
    if (pixel_x) EPD_Write(SSD1xxx_RAM_XCOUNT, x % 256, x / 256);
    else if (descending) EPD_Write(SSD1xxx_RAM_XCOUNT, x / 8, 0x00);
    else EPD_Write(SSD1xxx_RAM_XCOUNT, x / 8);
    if (!descending) {
        EPD_Write(SSD1xxx_RAM_YPOS, y % 256, y / 256, end_y % 256, end_y / 256);
        EPD_Write(SSD1xxx_RAM_YCOUNT, y % 256, y / 256);
    } else if (pixel_x) {
        EPD_Write(SSD1xxx_RAM_YCOUNT, end_y % 256, end_y / 256);
    } else {
        uint16_t top = (uint16_t)end_y;
        EPD_Write(SSD1xxx_RAM_YCOUNT, top & 255, top >> 8);
    }
}

void SSD1xxx_Init(epd_model_t* epd) {
#if INKTAG_EPD_ENABLE_HANSHOW_OLD_200
    if (SSD1xxx_IsHanshowStock200(epd)) { HanshowOld200_Init(epd); return; }
#endif
    EPD_Reset(true, 10);

    EPD_WriteCmd(SSD1xxx_SW_RESET);

    if (epd->ic == DRV_IC_SSD1680 && epd->color == COLOR_BW)
        SSD1680_WaitBusy(epd, 200);
    else
        SSD1xxx_WaitBusy(200);

    if (epd->ic == DRV_IC_SSD1680) {
        uint16_t y_end = epd->height - 1;
        EPD_Write(SSD1xxx_GDO_CTR, y_end & 0xff, y_end >> 8, 0x00);
        SSD1xxx_SetWindow(epd, 0, 0, epd->width, epd->height);
        {
            static const uint8_t commands[] = {
                SSD1xxx_BORDER_CTRL, 1u, 0x05,
                SSD1xxx_DISP_CTRL1, 2u, 0x00, 0x80,
                SSD1xxx_TSENSOR_CTRL, 1u, 0x80,
            };
            EPD_WriteInitCommands(commands, sizeof(commands));
        }

        /* Load the SSD1680 OTP waveform before the first RAM write. Without
         * 0x22=B1 + 0x20, some 2.13-inch BWR modules never start their charge
         * pump and VGH/VGL/VCOM remain at 0 V. */
        if (SSD1680_Is213Bwr(epd)) SSD1xxx_Update(0xB1);
        SSD1680_WaitBusy(epd, 200);
        return;
    }

    if (SSD1xxx_IsSSD1683(epd)) {
        uint16_t y_end = epd->height - 1;
        EPD_Write(SSD1xxx_GDO_CTR, y_end & 0xff, y_end >> 8, 0x00);
        {
            static const uint8_t commands[] = {
                SSD1xxx_BORDER_CTRL, 1u, 0x01,
                SSD1xxx_TSENSOR_CTRL, 1u, 0x80,
            };
            EPD_WriteInitCommands(commands, sizeof(commands));
        }
        SSD1xxx_SetWindow(epd, 0, 0, epd->width, epd->height);
        return;
    }

    if (SSD1677_Is426(epd)) {
        uint16_t y_end = epd->height - 1;
        EPD_Write(SSD1xxx_TSENSOR_CTRL, 0x80);
        /* B1 loads the controller's temperature-dependent OTP waveform as
         * part of acquiring TSC. Read it before restoring the panel's custom
         * four-gray RAM LUT below, otherwise B1 can silently replace that LUT. */
        (void)SSD1xxx_ReadTemp(epd);
        EPD_Write(SSD1xxx_SOFTSTART, 0xAE, 0xC7, 0xC3, 0xC0, 0x80);
        EPD_Write(SSD1xxx_GDO_CTR, y_end & 0xff, y_end >> 8, 0x02);
        EPD_Write(SSD1xxx_BORDER_CTRL, 0x01);
        SSD1xxx_SetWindow(epd, 0, 0, epd->width, epd->height);
        return;
    }

    {
        static const uint8_t commands[] = {
            SSD1xxx_BORDER_CTRL, 1u, 0x01,
            SSD1xxx_TSENSOR_CTRL, 1u, 0x80,
        };
        EPD_WriteInitCommands(commands, sizeof(commands));
    }

    SSD1xxx_SetWindow(epd, 0, 0, epd->width, epd->height);
}

static void SSD1xxx_Refresh(epd_model_t* epd) {
#if INKTAG_EPD_ENABLE_HANSHOW_OLD_200
    if (SSD1xxx_IsHanshowStock200(epd)) {
        if (SSD1xxx_IsHanshowNative200(epd) && EPD_AbortRequested()) {
            s_hanshow_old200_ok = false;
            return;
        }
        if (s_hanshow_old200_ok) (void)HanshowOld200_Activate(0xC7, 45000);
        return;
    }
#endif


    uint8_t control = epd->color == COLOR_BWR ? 0x80 : 0x40;
    uint8_t sequence = 0xF7;
    uint16_t timeout = UINT16_MAX;
    bool control_write = true, command_only = false, restore = true, edge = true;
    bool small = SSD1619_IsSmall213(epd);
    if (epd->ic == DRV_IC_SSD1680) {
        edge = false;
        control_write = false;
        restore = false;
        sequence = epd->color == COLOR_BWR ? 0xC7 : 0xF7;
        command_only = epd->color == COLOR_BWR && !SSD1680_Is213Bwr(epd);
        if (epd->color == COLOR_BW) timeout = 15000;
    } else if (SSD1xxx_IsSSD1683(epd)) {
        control = epd->color == COLOR_BWR ? 0x00 : 0x40;
        timeout = UINT16_MAX;
        edge = true;
    } else if (SSD1677_Is426(epd)) {
        edge = false;
        control = 0x40;
        sequence = 0xF7;
        timeout = 15000;
    } else if (small) {
        edge = false;
        timeout = INKTAG_EPD_SSD1619_SMALL_REFRESH_TIMEOUT_MS;
    }
    if (control_write) EPD_Write(SSD1xxx_DISP_CTRL1, control, 0x00);
    EPD_DEBUG("refresh begin");
    if (!small && timeout == UINT16_MAX && control_write && !edge)
        EPD_DEBUG("temperature: %d", SSD1xxx_ReadTemp(epd));
    if (command_only) EPD_WriteCmd(SSD1xxx_MASTER_ACTIVATE);
    else SSD1xxx_Update(sequence);
    if (edge) (void)EPD_WaitBusyEdge(true, 1000, timeout);
    else if (epd->ic == DRV_IC_SSD1680) SSD1680_WaitBusy(epd, timeout);
    else if (small) SSD1619_SmallWaitBusy(timeout);
    else SSD1xxx_WaitBusy(timeout);
    EPD_DEBUG("refresh end");
    if (restore) SSD1xxx_SetWindow(epd, 0, 0, epd->width, epd->height);
    if (small) EPD_WriteCmd(0x83);
}

void SSD1xxx_Clear(epd_model_t* epd, bool refresh) {
    uint32_t ram_bytes = ((epd->width + 7) / 8) * epd->height;
#if INKTAG_EPD_ENABLE_HANSHOW_OLD_200
    if (SSD1xxx_IsHanshowStock200(epd)) {
        if (!s_hanshow_old200_ok || EPD_AbortRequested()) return;
        if (!SSD1xxx_IsHanshowNative200(epd)) HanshowOld200_Pointer(0u);
        EPD_FillRAM(SSD1xxx_WRITE_RAM1, 0xFF, ram_bytes);
        if (!SSD1xxx_IsHanshowNative200(epd)) HanshowOld200_Pointer(0u);
        EPD_FillRAM(SSD1xxx_WRITE_RAM2, 0x00, ram_bytes);
        if (refresh) SSD1xxx_Refresh(epd);
        return;
    }
#endif



    SSD1xxx_SetWindow(epd, 0, 0, epd->width, epd->height);

    uint8_t white = 0xFF;
    uint8_t planes = epd->ic == DRV_IC_SSD1680 && epd->color == COLOR_BW ? 1 : 2;
    for (uint8_t plane = 0; plane < planes; ++plane) {
        uint8_t value = plane != 0 && SSD1xxx_HasInvertedColorRam(epd) ? 0x00 : white;
        EPD_FillRAM(plane == 0 ? SSD1xxx_WRITE_RAM1 : SSD1xxx_WRITE_RAM2, value, ram_bytes);
    }
    if (refresh) SSD1xxx_Refresh(epd);
}

/* A direct row is kept intact for the port's SPI/DMA staging contract.
 * Synthetic and inverted rows retain byte transactions. */
static void SSD1xxx_PlaneRows(const uint8_t *source, uint16_t stride,
                              uint16_t rows, bool invert) {
    /* The public width parameter is uint16: its rounded pixel width may
     * wrap at65536. Retain the accepted zero-width bus behavior there. */
    uint16_t bytes = (uint16_t)(stride * 8u) / 8u;
    while (rows-- != 0) {
        if (source != NULL && !invert) {
            EPD_WriteData((uint8_t *)source, (uint8_t)stride);
        } else {
            for (uint16_t column = 0; column < bytes; ++column) {
                uint8_t value = source == NULL ? 0xFF : source[column];
                EPD_WriteByte(invert ? (uint8_t)~value : value);
            }
        }
        if (source != NULL) source += stride;
    }
}

void SSD1xxx_WriteImage(epd_model_t* epd, uint8_t* black, uint8_t* color, uint16_t x, uint16_t y, uint16_t w,
                        uint16_t h) {
    uint16_t wb = (w + 7) / 8;  // width bytes, bitmaps are padded
    uint8_t* plane;
    bool invert;
    x -= x % 8;                 // byte boundary
    w = wb * 8;                 // byte boundary
    uint16_t ram_width = (epd->width + 7u) & ~7u;
    if (x + w > ram_width || y + h > epd->height) return;
#if INKTAG_EPD_ENABLE_HANSHOW_OLD_200
    if (SSD1xxx_IsHanshowStock200(epd)) {
        if (!s_hanshow_old200_ok || EPD_AbortRequested() || x != 0u || w != 200u || black == NULL) return;
        if (SSD1xxx_IsHanshowNative200(epd) && (y != 0u || h != 200u)) {
            s_hanshow_old200_ok = false;
            return;
        }
        if (!SSD1xxx_IsHanshowNative200(epd)) HanshowOld200_Pointer(y);
        EPD_WriteCmd(SSD1xxx_WRITE_RAM1);
        for (uint16_t row = 0; row < h; ++row) EPD_WriteData(black + row * 25u, 25u);
        if (!SSD1xxx_IsHanshowNative200(epd)) HanshowOld200_Pointer(y);
        EPD_WriteCmd(SSD1xxx_WRITE_RAM2);
        for (uint16_t row = 0; row < h; ++row) for (uint8_t col = 0; col < 25u; ++col)
            EPD_WriteByte(color ? (uint8_t)~color[row * 25u + col] : 0u);
        return;
    }
#endif



    SSD1xxx_SetWindow(epd, x, y, w, h);
    EPD_WriteCmd(SSD1xxx_WRITE_RAM1);
    SSD1xxx_PlaneRows(black, wb, h, false);
    if (epd->ic == DRV_IC_SSD1680 && epd->color == COLOR_BW) return;
    plane = black;
    if (epd->color == COLOR_BWR || (epd->color == COLOR_GRAY4 && color != NULL)) plane = color;
    invert = epd->color == COLOR_BWR && SSD1xxx_HasInvertedColorRam(epd);
    EPD_WriteCmd(SSD1xxx_WRITE_RAM2);
    SSD1xxx_PlaneRows(plane, wb, h, invert);
}

void SSD1xxx_WriteRam(epd_model_t* epd, bool begin, bool black, uint8_t* data, uint8_t len) {
#if INKTAG_EPD_ENABLE_HANSHOW_OLD_200
    if (SSD1xxx_IsHanshowStock200(epd)) {
        if (!s_hanshow_old200_ok || EPD_AbortRequested() || data == NULL) return;
        if (begin) {
            if (!SSD1xxx_IsHanshowNative200(epd)) HanshowOld200_Pointer(0u);
            EPD_WriteCmd(black ? SSD1xxx_WRITE_RAM1 : SSD1xxx_WRITE_RAM2);
        }
        if (black) EPD_WriteData(data, len);
        else for (uint8_t i = 0; i < len; ++i) EPD_WriteByte((uint8_t)~data[i]);
        return;
    }
#endif



    if (begin) {
        uint8_t command = SSD1xxx_WRITE_RAM1;
        if (!black && (epd->color == COLOR_BWR || epd->color == COLOR_GRAY4))
            command = SSD1xxx_WRITE_RAM2;
        EPD_WriteCmd(command);
    }
    if (black || !SSD1xxx_HasInvertedColorRam(epd)) {
        EPD_WriteData(data, len);
    } else {
        while (len-- != 0) EPD_WriteByte((uint8_t)~*data++);
    }
}

void SSD1xxx_Sleep(epd_model_t* epd) {
    EPD_Write(SSD1xxx_SLEEP_MODE, 0x01);
    delay(100);
}

const epd_driver_t epd_drv_ssd1xxx = {
    .init = SSD1xxx_Init,
    .clear = SSD1xxx_Clear,
    .write_image = SSD1xxx_WriteImage,
    .write_ram = SSD1xxx_WriteRam,
    .refresh = SSD1xxx_Refresh,
    .sleep = SSD1xxx_Sleep,
    .read_temp = SSD1xxx_ReadTemp,
    .read_busy = SSD1xxx_ReadBusy,
};

#if INKTAG_EPD_ENABLE_HANSHOW_OLD_200
bool SSD1xxx_OperationOK(epd_model_t* epd) {
    return !SSD1xxx_IsHanshowStock200(epd) || (s_hanshow_old200_ok && !EPD_AbortRequested());
}
#endif

// SSD1619 400x300 Black/White/Red
