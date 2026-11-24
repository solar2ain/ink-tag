/* DA normal SSD projection v1 */
/* Generic controller execution/transfer implementation: InkTag, 2026-11-04.
 * Specification: docs/research/2026-11-04-tlsr-ssd-uc-rewrite-spec.md.
 * Existing copyright and license terms remain in force;
 * excluded stock waveforms are absent from this normal-profile projection.
 */
#include "inktag_epd_driver.h"
#if !defined(INKTAG_EXCLUDE_SSD1675_154) || INKTAG_EPD_ENABLE_SSD1677_GRAY4
#error DA_source_SSD_source_requires_the_unchanged_normal_profile
#endif

#include <string.h>
#include "inktag_epd_temperature.h"

#if defined(INKTAG_NRF_SDK17)
#include "inktag_board.h"
#endif

bool SSD1xxx_ReadBusy(epd_model_t* epd) { (void)epd; return EPD_ReadBusy(); }

static void SSD1xxx_WaitBusy(uint16_t timeout) { EPD_WaitBusy(true, timeout); }

static void SSD1680_WaitBusy(epd_model_t* epd, uint16_t timeout) {
    EPD_WaitBusy(epd->color == COLOR_BW ? false : true, timeout);
}

static void SSD1619_SmallWaitBusy(uint16_t timeout) { EPD_WaitBusy(true, timeout); }

static void SSD1xxx_Update(uint8_t seq) {
    EPD_Write(SSD1xxx_DISP_CTRL2, seq);
    EPD_WriteCmd(SSD1xxx_MASTER_ACTIVATE);
}

static bool SSD1619_IsSmall213(epd_model_t* epd) {
    return epd->ic == DRV_IC_SSD1619 && epd->width <= 128 && epd->height <= 250;
}

static bool SSD1619_IsHema213(epd_model_t* epd) {
    return epd->driver_id == EPD_SCREEN_DRIVER_SSD1619 &&
           epd->panel_recipe == INKTAG_RECIPE_SSD1619_HEMA_213_BWR &&
           epd->width == 128 && epd->height == 250 &&
           epd->color == COLOR_BWR;
}

/* Bench-only recovery of the historical AESL selector02/driver5C path.
 * The existing 20/00 configuration is a legacy candidate, NOT identification
 * of E029A33 silicon. Keep this opt-in until a shared named recipe is agreed
 * and visually qualified; other geometries and ordinary builds are unchanged. */
static bool SSD1xxx_IsDaA33(const epd_model_t* epd) {
#if defined(INKTAG_DA_A33_PANEL_CANDIDATE) && INKTAG_DA_A33_PANEL_CANDIDATE
    /* DA and its host harness are little-endian. memcpy exposes adjacent
     * fields as one comparison without aliasing or unaligned dereferences.
     * The full identity/geometry still match exactly (no mask relaxation). */
    uint16_t identity;
    uint32_t geometry;
    if (epd == NULL) return false;
    memcpy(&identity, &epd->driver_id, sizeof(identity));
    memcpy(&geometry, &epd->width, sizeof(geometry));
    return identity == EPD_SCREEN_DRIVER_SSD1619 &&
           geometry == ((296u << 16) | 128u) && epd->color == COLOR_BWR;
#else
    (void)epd;
    return false;
#endif
}

static bool SSD1xxx_IsSSD1683(const epd_model_t* epd) {
#if INKTAG_EPD_ENABLE_SSD1683
    return epd->ic == DRV_IC_SSD1683;
#else
    (void)epd;
    return false;
#endif
}

bool SSD1xxx_TemperatureSupported(const epd_model_t* epd) {
    return epd != NULL && (SSD1xxx_IsDaA33(epd) ||
           epd->ic == DRV_IC_SSD1680 || SSD1xxx_IsSSD1683((epd_model_t*)epd));
}

bool SSD1xxx_RefreshDeferred(const epd_model_t* epd) {
#if defined(INKTAG_DIALOG)
    return epd != NULL && (SSD1xxx_IsDaA33(epd) ||
                           SSD1xxx_IsSSD1683(epd));
#else
    (void)epd;
    return false;
#endif
}

static bool SSD1xxx_IsSSD1675_154(epd_model_t* epd) {
    (void)epd;
    return false;
}
static bool SSD1xxx_IsHanshowNowa154(epd_model_t* epd) { (void)epd; return false; }
static void SSD1675_154_SetPointer(epd_model_t* epd, uint16_t y) { (void)epd; (void)y; }
static uint16_t SSD1675_154_StreamY(uint16_t logical_y) { return logical_y; }
static void SSD1675_154_LoadLut(epd_model_t* epd) { (void)epd; }
static int8_t SSD1675_154_ReadTemperature(void) { return INT8_MIN; }

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
            SSD1xxx_IsHanshowNowa154(epd)) && epd->color == COLOR_BWR;
}

int8_t SSD1xxx_ReadTemp(epd_model_t* epd) {
    uint8_t temperature[2] = {0xFFu, 0xFFu};
    if (SSD1xxx_IsSSD1683(epd) || epd->ic == DRV_IC_SSD1680) {
        int8_t value;
        return SSD1xxx_ReadTemperature(epd, &value) ? value : INT8_MIN;
    }
    if (SSD1xxx_IsHanshowNowa154(epd))
        return SSD1675_154_ReadTemperature();
    if (SSD1xxx_IsSSD1675_154(epd)) return INT8_MIN;
    SSD1xxx_Update(0xB1);
    SSD1xxx_WaitBusy(500);
    EPD_WriteCmd(SSD1xxx_TSENSOR_READ);
    /* SSD1677 R1B is a 12-bit signed fixed-point value: A11..A4 followed
     * by A3..A0 in the high nibble of the second byte. Clock both bytes in
     * one CS-low interval; callers currently expose whole degrees. */
    EPD_ReadData(temperature, sizeof(temperature));
    return (int8_t)temperature[0];
}

bool SSD1xxx_ReadTemperature(epd_model_t* epd, int8_t* temperature) {
    uint8_t raw[2] = {0x7Fu, 0xF0u};
#if !defined(INKTAG_DIALOG)
    int16_t sixteenths;
#endif
    bool ssd1683, busy_status;
    if (epd == NULL || temperature == NULL || EPD_AbortRequested()) return false;
    /* A33 init already completed B1; other qualified SSD controllers need
     * their own conversion. Share the checked read/decode tail only. */
    ssd1683 = SSD1xxx_IsSSD1683(epd);
    if (!SSD1xxx_IsDaA33(epd)) {
        if (!ssd1683 && epd->ic != DRV_IC_SSD1680) return false;
        busy_status = !(epd->ic == DRV_IC_SSD1680 && epd->color == COLOR_BW);
        if (EPD_ReadBusy() == busy_status) return false;
        /* Temperature + OTP LUT only: no analog enable, display or RAM write. */
        SSD1xxx_Update(ssd1683 && epd->color == COLOR_BW ? 0xB9u : 0xB1u);
        if (!EPD_WaitBusyEdge(busy_status, 500u, 500u) || EPD_AbortRequested()) return false;
    }
    if (!EPD_ReadCommand(SSD1xxx_TSENSOR_READ, raw, ssd1683 ? 1u : 2u) ||
        EPD_AbortRequested()) return false;
#if defined(INKTAG_DIALOG)
    /* Whole-degree DA wire/GUI consumers need no retained fractional value.
     * Keep the shared full decoder below; exhaustive wire tests establish
     * equivalence for every SSD1683 byte and all65536 SSD1680 pairs. */
    if (ssd1683) {
        if ((uint8_t)(raw[0] + 40u) > 125u) return false;
        *temperature = (int8_t)raw[0];
        return true;
    }
    return inktag_ssd_r1b_temperature_floor_celsius(true, raw, temperature);
#else
    if (!(ssd1683 ? inktag_ssd1683_r1b_temperature_sixteenths(true, raw, &sixteenths) :
                   inktag_ssd1680_r1b_temperature_sixteenths(true, raw, &sixteenths)))
        return false;
    *temperature = inktag_epd_temperature_floor_celsius(sixteenths);
    return true;
#endif
}

static void SSD1xxx_SetWindow(epd_model_t* epd, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    /* DA frames use the qualified canonical ascending row window. */
    bool descending = false;
    bool pixel_x = false;
#if INKTAG_EPD_ENABLE_SSD1677
    pixel_x = epd->ic == DRV_IC_SSD1677;
#endif
    if (SSD1619_IsSmall213(epd) && !SSD1619_IsHema213(epd)) x += 8;
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
    }
    /* DA keeps one Y-window transaction per plane, as in its fixed baseline. */
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

/* Share the identical full-window argument setup across init/refresh/clear.
 * Outlining saves SRAM code without changing any command or its ordering. */
__attribute__((noinline)) static void SSD1xxx_ResetWindow(epd_model_t* epd) {
    SSD1xxx_SetWindow(epd, 0, 0, epd->width, epd->height);
}

void SSD1xxx_Init(epd_model_t* epd) {
    if (SSD1xxx_IsDaA33(epd)) {
        /* Executable-backed trace: historical sibling dump SHA256
         * 21ec46c7...0ec1662, active payload 4040, init 07FC399E.
         * Preserve even the stock 74's extra DATA bytes 7E/3B (not a 7E
         * command), instead of silently substituting a guessed recipe.
         * No host LUT, VCOM/GDV/SDV values or OTP programming are added. */
        static const uint8_t commands[] = {
            0x74, 3u, 0x54, 0x7E, 0x3B,
            0x2B, 2u, 0x04, 0x63,
            SSD1xxx_SOFTSTART, 4u, 0x8B, 0x9C, 0x96, 0x0F,
            SSD1xxx_GDO_CTR, 3u, 0x28, 0x01, 0x00,
            SSD1xxx_ENTRY_MODE, 1u, 0x01,
            SSD1xxx_RAM_XPOS, 2u, 0x00, 0x0F,
            SSD1xxx_RAM_YPOS, 4u, 0x28, 0x01, 0x00, 0x00,
            SSD1xxx_BORDER_CTRL, 1u, 0x01,
            SSD1xxx_TSENSOR_CTRL, 1u, 0x80,
            /* Unlike the historical raw-frame caller, InkTag's canonical
             * color plane uses 0=red/1=white. Normalize in the controller,
             * as the ordinary SSD1619 refresh does, rather than allocating
             * another buffer or inverting every GUI/slot transfer byte. */
            SSD1xxx_DISP_CTRL1, 1u, 0x80,
        };
#if defined(INKTAG_DA_A33_PANEL_CANDIDATE) && INKTAG_DA_A33_PANEL_CANDIDATE
        /* Stock GPIO init 07FC20FE leaves configured BS=P2_2 HIGH.
         * It is absent from the driver pin tuple and is never lowered by
         * its 8-clock command/data writers. Do not infer 9-bit SPI from the
         * BS label; mirror the verified board callback's actual level. */
        EPD_AeslStockBusSelect(true);
#endif
        EPD_ResetPulse(100, 100, 100);
        EPD_WriteCmd(SSD1xxx_SW_RESET);
        (void)EPD_WaitBusyEdge(true, 300, 3000);
        EPD_WriteInitCommands(commands, sizeof(commands));
        SSD1xxx_Update(0xB1);
        (void)EPD_WaitBusyEdge(true, 300, 3000);
        return;
    }
    EPD_Reset(true, 10);

    EPD_WriteCmd(SSD1xxx_SW_RESET);
    if (SSD1xxx_IsSSD1675_154(epd)) {
        if (SSD1xxx_IsHanshowNowa154(epd)) {
            /* PA1 may assert about 1 ms after the command. A release-only
             * wait can return during that gap and let the following command
             * overtake software reset. The physically qualified Nowa path
             * waits for both assertion and release at each activation. */
            EPD_WaitBusyEdge(true, 300, 3000);
        } else {
            SSD1xxx_WaitBusy(7050);
        }
        if (SSD1xxx_IsHanshowNowa154(epd)) {
            /* Hanshow's preserved module at physical 0x03D964 first asks
             * SSD1675 to load its temperature-dependent OTP waveform
             * (0x22=B1 + master activation), reads TSC, then the 0x03D9A0
             * path applies the profile border and 0x22=A1 update before a
             * second TSC read. It does not upload the unrelated PriceETag
             * LUT/voltage recipe or program 0x01/0x11/0x44/0x45 windows. */
            EPD_Write(SSD1xxx_TSENSOR_CTRL, 0x80);
            SSD1xxx_Update(0xB1);
            EPD_WaitBusyEdge(true, 300, 3000);
            (void)SSD1675_154_ReadTemperature();
            EPD_Write(SSD1xxx_BORDER_CTRL, 0x05);
            SSD1xxx_Update(0xA1);
            EPD_WaitBusyEdge(true, 300, 3000);
            (void)SSD1675_154_ReadTemperature();
            /* The normal stock branch finishes 0x03D9A0 by applying B1 once
             * more after the temperature/status decision.  This final OTP
             * load is separate from the earlier temperature acquisition and
             * must complete before either RAM plane is streamed. */
            SSD1xxx_Update(0xB1);
            EPD_WaitBusyEdge(true, 300, 3000);
            /* Stock RAM-start functions 0x03DA7C/0x03DAA4 use X=1 and
             * descending Y from 151 for both 2,888-byte planes. */
            SSD1675_154_SetPointer(epd, SSD1675_154_StreamY(0));
            return;
        }
        {
            static const uint8_t commands[] = {
                SSD1xxx_ANALOG_BLOCK_CTRL, 1u, 0x54,
                SSD1xxx_DIGITAL_BLOCK_CTRL, 1u, 0x3B,
                SSD1xxx_GDO_CTR, 3u, 0x97, 0x00, 0x00,
                SSD1xxx_SOFTSTART, 4u, 0x8B, 0x9C, 0x96, 0x0F,
                SSD1xxx_GDV_CTRL, 1u, 0x19,
                SSD1xxx_SDV_CTRL, 3u, 0x41, 0x8E, 0x32,
                SSD1xxx_DUMMY_LINE, 1u, 0x11,
                SSD1xxx_GATE_LINE_WIDTH, 1u, 0x0D,
                SSD1xxx_BORDER_CTRL, 1u, 0xC0,
                SSD1xxx_ENTRY_MODE, 1u, 0x01,
                SSD1xxx_RAM_XPOS, 2u, 0x00, 0x12,
            };
            EPD_WriteInitCommands(commands, sizeof(commands));
        }
        /* Preserve the stock 0x0127..0x0000 controller window. The active
         * frame starts at Y=0x0097 and contains exactly 152 scan lines. */
        {
            static const uint8_t commands[] = {
                SSD1xxx_RAM_YPOS, 4u, 0x27, 0x01, 0x00, 0x00,
                SSD1xxx_VCOM_VOLTAGE, 1u, 0x54,
                SSD1xxx_DISP_CTRL2, 1u, 0xC7,
            };
            EPD_WriteInitCommands(commands, sizeof(commands));
        }
        SSD1675_154_LoadLut(epd);
        SSD1675_154_SetPointer(epd, SSD1675_154_StreamY(0));
        return;
    }
    if (epd->ic == DRV_IC_SSD1680 && epd->color == COLOR_BW)
        SSD1680_WaitBusy(epd, 200);
    else
        SSD1xxx_WaitBusy(200);

    if (epd->ic == DRV_IC_SSD1680) {
        uint16_t y_end = epd->height - 1;
        EPD_Write(SSD1xxx_GDO_CTR, y_end & 0xff, y_end >> 8, 0x00);
        SSD1xxx_ResetWindow(epd);
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
        SSD1xxx_ResetWindow(epd);
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
        EPD_Write(SSD1xxx_BORDER_CTRL,
            INKTAG_EPD_ENABLE_SSD1677_GRAY4 && epd->color == COLOR_GRAY4 ? 0x00 : 0x01);
        SSD1xxx_ResetWindow(epd);
        return;
    }

    {
        static const uint8_t commands[] = {
            SSD1xxx_BORDER_CTRL, 1u, 0x01,
            SSD1xxx_TSENSOR_CTRL, 1u, 0x80,
        };
        EPD_WriteInitCommands(commands, sizeof(commands));
    }

    SSD1xxx_ResetWindow(epd);
}

static void SSD1xxx_Refresh(epd_model_t* epd) {
    if (SSD1xxx_IsDaA33(epd)) {
        /* Stock 07FC3BB8: B1 was loaded explicitly during init, then C7
         * drives the written RAM. A disabled DEBUG macro must not decide
         * whether the panel receives a waveform-load command. */
        SSD1xxx_Update(0xC7);
#if !defined(INKTAG_DIALOG)
        (void)EPD_WaitBusyEdge(true, 1000, 45000);
#endif
        /* DA's foreground owner observes assertion/release in its bounded
         * outer-loop wait. Do not keep the renderer stack live, or postpone
         * queued HELLO/CANCEL until the whole autonomous waveform completes.
         * Other platforms retain their original synchronous wait contract. */
        return;
    }
    if (SSD1xxx_IsSSD1675_154(epd)) {
        EPD_DEBUG("refresh begin");
        if (SSD1xxx_IsHanshowNowa154(epd))
            SSD1xxx_Update(0xC7);
        else
            EPD_WriteCmd(SSD1xxx_MASTER_ACTIVATE);
        if (SSD1xxx_IsHanshowNowa154(epd))
            /* Unlike the release-only waits used during stock initialization,
             * the final full update must prove that C7 actually started. */
            EPD_WaitBusyEdge(true, 1000, 45000);
        else
            EPD_WaitBusyEdge(true, 1000, 8000);
        EPD_DEBUG("refresh end");
        SSD1675_154_SetPointer(epd, SSD1675_154_StreamY(0));
        return;
    }

    uint8_t control = epd->color == COLOR_BWR ? 0x80 : 0x40;
    uint8_t sequence = 0xF7;
    uint16_t timeout = UINT16_MAX;
    bool control_write = true, command_only = false, restore = true, edge = false;
    bool small = SSD1619_IsSmall213(epd);
    if (epd->ic == DRV_IC_SSD1680) {
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
        control = epd->color == COLOR_GRAY4 ? 0x00 : 0x40;
        sequence = epd->color == COLOR_GRAY4 ? 0xC7 : 0xF7;
        timeout = 15000;
    } else if (small) {
        timeout = 30000;
    }
    if (control_write) EPD_Write(SSD1xxx_DISP_CTRL1, control, 0x00);
    EPD_DEBUG("refresh begin");
    if (timeout == UINT16_MAX && control_write && !edge)
        EPD_DEBUG("temperature: %d", SSD1xxx_ReadTemp(epd));
    if (command_only) EPD_WriteCmd(SSD1xxx_MASTER_ACTIVATE);
    else SSD1xxx_Update(sequence);
    if (SSD1xxx_RefreshDeferred(epd)) return;
    if (edge) (void)EPD_WaitBusyEdge(true, 1000, timeout);
    else if (epd->ic == DRV_IC_SSD1680) SSD1680_WaitBusy(epd, timeout);
    else if (small) SSD1619_SmallWaitBusy(timeout);
    else SSD1xxx_WaitBusy(timeout);
    EPD_DEBUG("refresh end");
    if (restore) SSD1xxx_SetWindow(epd, 0, 0, epd->width, epd->height);
    if (small) EPD_WriteCmd(0x83);
}

void SSD1xxx_FinishDeferredRefresh(epd_model_t* epd) {
#if defined(INKTAG_DIALOG)
    if (epd != NULL && SSD1xxx_IsSSD1683(epd))
        SSD1xxx_ResetWindow(epd);
#else
    (void)epd;
#endif
}

void SSD1xxx_Clear(epd_model_t* epd, bool refresh) {
    uint32_t ram_bytes = ((epd->width + 7) / 8) * epd->height;

    if (SSD1xxx_IsSSD1675_154(epd)) {
        SSD1675_154_SetPointer(epd, SSD1675_154_StreamY(0));
        EPD_FillRAM(SSD1xxx_WRITE_RAM1, 0xFF, ram_bytes);
        if (SSD1xxx_IsHanshowNowa154(epd)) {
            SSD1675_154_SetPointer(epd, SSD1675_154_StreamY(0));
            EPD_FillRAM(SSD1xxx_WRITE_RAM2, 0x00, ram_bytes);
        }
        if (refresh) SSD1xxx_Refresh(epd);
        return;
    }

    SSD1xxx_ResetWindow(epd);

    uint8_t white = epd->color == COLOR_GRAY4 && SSD1677_Is426(epd) ? 0x00 : 0xFF;
    uint8_t planes = epd->ic == DRV_IC_SSD1680 && epd->color == COLOR_BW ? 1 : 2;
    for (uint8_t plane = 0; plane < planes; ++plane) {
        if (plane != 0 && SSD1xxx_IsDaA33(epd)) SSD1xxx_ResetWindow(epd);
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
    bool gray4_mono = INKTAG_EPD_ENABLE_SSD1677_GRAY4 &&
        epd->color == COLOR_GRAY4 && SSD1677_Is426(epd) && color == NULL;
    x -= x % 8;                 // byte boundary
    w = wb * 8;                 // byte boundary
    uint16_t ram_width = (epd->width + 7u) & ~7u;
    if (x + w > ram_width || y + h > epd->height) return;

    if (SSD1xxx_IsSSD1675_154(epd)) {
        if (x != 0 || w != epd->width || black == NULL) return;
        SSD1675_154_SetPointer(epd, SSD1675_154_StreamY(y));
        EPD_WriteCmd(SSD1xxx_WRITE_RAM1);
        for (uint16_t row = 0; row < h; row++) EPD_WriteData(black + row * wb, (uint8_t)wb);
        if (SSD1xxx_IsHanshowNowa154(epd)) {
            SSD1675_154_SetPointer(epd, SSD1675_154_StreamY(y));
            EPD_WriteCmd(SSD1xxx_WRITE_RAM2);
            for (uint16_t row = 0; row < h; row++) {
                for (uint16_t column = 0; column < wb; column++) {
                    uint8_t value = color ? color[row * wb + column] : 0xFF;
                    EPD_WriteByte((uint8_t)~value);
                }
            }
        }
        return;
    }

    SSD1xxx_SetWindow(epd, x, y, w, h);
    EPD_WriteCmd(SSD1xxx_WRITE_RAM1);
    SSD1xxx_PlaneRows(black, wb, h, gray4_mono);
    if (epd->ic == DRV_IC_SSD1680 && epd->color == COLOR_BW) return;
    if (SSD1xxx_IsDaA33(epd)) SSD1xxx_SetWindow(epd, x, y, w, h);
    plane = black;
    if (epd->color == COLOR_BWR || (epd->color == COLOR_GRAY4 && color != NULL)) plane = color;
    invert = gray4_mono || (epd->color == COLOR_BWR && SSD1xxx_HasInvertedColorRam(epd));
    EPD_WriteCmd(SSD1xxx_WRITE_RAM2);
    SSD1xxx_PlaneRows(plane, wb, h, invert);
}

void SSD1xxx_WriteRam(epd_model_t* epd, bool begin, bool black, uint8_t* data, uint8_t len) {
    if (SSD1xxx_IsHanshowNowa154(epd)) {
        if (begin) {
            SSD1675_154_SetPointer(epd, SSD1675_154_StreamY(0));
            EPD_WriteCmd(black ? SSD1xxx_WRITE_RAM1 : SSD1xxx_WRITE_RAM2);
        }
        if (!black) {
            for (uint8_t i = 0; i < len; i++) EPD_WriteByte((uint8_t)~data[i]);
        } else {
            EPD_WriteData(data, len);
        }
        return;
    }
    if (SSD1xxx_IsSSD1675_154(epd)) {
        if (begin) {
            SSD1675_154_SetPointer(epd, SSD1675_154_StreamY(0));
            EPD_WriteCmd(SSD1xxx_WRITE_RAM1);
        }
        EPD_WriteData(data, len);
        return;
    }

    if (begin) {
        uint8_t command = SSD1xxx_WRITE_RAM1;
        if (!black && (epd->color == COLOR_BWR || epd->color == COLOR_GRAY4))
            command = SSD1xxx_WRITE_RAM2;
        SSD1xxx_SetWindow(epd, 0, 0, epd->width, epd->height);
        EPD_WriteCmd(command);
    }
    if (black || !SSD1xxx_HasInvertedColorRam(epd)) {
        EPD_WriteData(data, len);
    } else {
        while (len-- != 0) EPD_WriteByte((uint8_t)~*data++);
    }
}

void SSD1xxx_Sleep(epd_model_t* epd) {
    if (SSD1xxx_IsSSD1675_154(epd) && !SSD1xxx_IsHanshowNowa154(epd)) return;
    EPD_Write(SSD1xxx_SLEEP_MODE, 0x01);
    if (!SSD1xxx_IsHanshowNowa154(epd)) delay(100);
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

// SSD1619 400x300 Black/White/Red
