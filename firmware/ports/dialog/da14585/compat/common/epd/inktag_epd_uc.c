/* Generic UC controller execution/transfer implementation: InkTag, 2026-11-04.
 * Specification: docs/research/2026-11-04-tlsr-ssd-uc-rewrite-spec.md.
 * Existing copyright and license terms remain in force; no whole-file
 * relicensing claim is made. Board-specific InkTag paths are retained.
 */
#include "inktag_epd_driver.h"

extern void app_feed_wdt(void);
#if defined(INKTAG_DIALOG)
extern void app_epd_trace(const char* tag);
#define UC8XXX_TRACE(_tag) app_epd_trace(_tag)
#else
#define UC8XXX_TRACE(_tag) ((void)0)
#endif

/*
 * UC8151 exposes BUSY_N: LOW means busy and HIGH means idle. PON and DRF both
 * trigger internal operations, so their timeout is only an upper bound; a
 * normally completing panel returns immediately when BUSY_N goes HIGH. Keep
 * POF also has a BUSY phase before the high-voltage rails are safe to release.
 * The tested 2.66-inch panel needs a longer release ceiling than the 2.13-inch
 * panel, but both still leave the wait immediately when BUSY_N returns HIGH.
 */
#define UC8176_POWER_RELEASE_TIMEOUT_MS 1000u
#define UC8151_POWER_RELEASE_TIMEOUT_MS 5000u
#define UC8XXX_BUSY_ASSERT_TIMEOUT_MS 1000u
#define UC8XXX_REFRESH_ASSERT_TIMEOUT_MS 3000u
#define UC8151_213_REFRESH_RELEASE_TIMEOUT_MS 35000u
#define UC8151_266_REFRESH_RELEASE_TIMEOUT_MS 60000u
#ifndef INKTAG_UC8176_REFRESH_RELEASE_TIMEOUT_MS
#define INKTAG_UC8176_REFRESH_RELEASE_TIMEOUT_MS UINT16_MAX
#endif

bool UC8xxx_ReadBusy(epd_model_t* epd) { return EPD_ReadBusy() == false; }

static void UC8xxx_WaitBusy(uint16_t timeout) { EPD_WaitBusy(false, timeout); }

#if defined(INKTAG_NRF_BOARD_NRF52832_PRICETAG)
static bool m_uc8xxx_power_ready = true;
static uint8_t m_uc8xxx_low_power_status = 0xFF;
static uint8_t m_uc8xxx_flag_status = 0xFF;
static uint8_t m_uc8xxx_revision = 0xFF;

bool UC8xxx_PowerReady(void) { return m_uc8xxx_power_ready; }
uint8_t UC8xxx_LowPowerStatus(void) { return m_uc8xxx_low_power_status; }
uint8_t UC8xxx_FlagStatus(void) { return m_uc8xxx_flag_status; }
uint8_t UC8xxx_Revision(void) { return m_uc8xxx_revision; }
#endif

/* Nowa-213 uses a two-plane MTP command path, not the Nowa-154 SSD1675
 * command family. The exact COG suffix remains unverified. */
static bool UC8xxx_IsHanshowNowa213(epd_model_t* epd) {
    return epd->driver_id == EPD_SCREEN_DRIVER_UC8XXX &&
           epd->panel_recipe == INKTAG_RECIPE_UC8XXX_HANSHOW_NOWA_213R_N;
}

static uint16_t UC8xxx_PowerReleaseTimeout(epd_model_t* epd) {
    return
#if INKTAG_EPD_ENABLE_UC8151
        epd->ic == DRV_IC_UC8151 ||
#endif
        UC8xxx_IsHanshowNowa213(epd) ?
        UC8151_POWER_RELEASE_TIMEOUT_MS : UC8176_POWER_RELEASE_TIMEOUT_MS;
}

static void UC8xxx_PowerOn(epd_model_t* epd) {
#if defined(INKTAG_NRF_BOARD_NRF52832_PRICETAG)
    m_uc8xxx_power_ready = false;
    m_uc8xxx_low_power_status = 0xFF;
    m_uc8xxx_flag_status = 0xFF;
    m_uc8xxx_revision = 0xFF;
    EPD_WriteCmd(UC8xxx_PON);
    bool ready = EPD_WaitBusyEdge(false, UC8XXX_BUSY_ASSERT_TIMEOUT_MS, UC8xxx_PowerReleaseTimeout(epd));
    m_uc8xxx_power_ready = ready;
    if (!ready) {
        /* UC8176 permits LPD to be read while PON is active.  A zero result
         * means its panel-side VDD fell below the controller threshold; this
         * distinguishes an accepted command with failed booster startup from
         * a missing BUSY edge caused by wrong SPI/control wiring. */
        EPD_WriteCmd(UC8xxx_LPD);
        m_uc8xxx_low_power_status = EPD_ReadByte();
        EPD_WriteCmd(UC8xxx_FLG);
        m_uc8xxx_flag_status = EPD_ReadByte();
        EPD_WriteCmd(UC8xxx_REV);
        m_uc8xxx_revision = EPD_ReadByte();
    }
#else
    EPD_WriteCmd(UC8xxx_PON);
    EPD_WaitBusyEdge(false, UC8XXX_BUSY_ASSERT_TIMEOUT_MS, UC8xxx_PowerReleaseTimeout(epd));
#endif
}

static void UC8xxx_PowerOff(epd_model_t* epd) {
    EPD_WriteCmd(UC8xxx_POF);
    EPD_WaitBusyEdge(false, UC8XXX_BUSY_ASSERT_TIMEOUT_MS, UC8xxx_PowerReleaseTimeout(epd));
}

static bool UC8xxx_IsUC8151Bwr(epd_model_t* epd) {
#if INKTAG_EPD_ENABLE_UC8151
    return epd->ic == DRV_IC_UC8151 && epd->color == COLOR_BWR;
#else
    (void)epd;
    return false;
#endif
}

static void UC8xxx_WriteInvertedData(const uint8_t* data, uint8_t len) {
    uint8_t inverted[64];

    while (len > 0) {
        uint8_t chunk = len > sizeof(inverted) ? sizeof(inverted) : len;
        for (uint8_t i = 0; i < chunk; i++) inverted[i] = (uint8_t)~data[i];
        EPD_WriteData(inverted, chunk);
        data += chunk;
        len -= chunk;
    }
}

static void UC8xxx_WriteRows(const uint8_t* data, uint16_t stride, uint8_t row_len,
                             uint16_t height, uint8_t fallback, bool invert) {
    uint8_t fill[128];
    if (data == NULL) {
        for (uint8_t i = 0; i < row_len; i++) fill[i] = invert ? (uint8_t)~fallback : fallback;
    }
    for (uint16_t y = 0; y < height; y++) {
        if (data == NULL) {
            EPD_WriteData(fill, row_len);
        } else if (invert) {
            UC8xxx_WriteInvertedData(data + y * stride, row_len);
        } else {
            EPD_WriteData((uint8_t*)(data + y * stride), row_len);
        }
    }
}

#if !defined(INKTAG_DIALOG)
static uint16_t UC8xxx_RefreshTimeout(epd_model_t* epd) {
#if INKTAG_EPD_ENABLE_UC8151
    if (epd->ic == DRV_IC_UC8151 && epd->width == 152u && epd->height == 296u)
        return UC8151_266_REFRESH_RELEASE_TIMEOUT_MS;
    if (UC8xxx_IsUC8151Bwr(epd)) return UC8151_213_REFRESH_RELEASE_TIMEOUT_MS;
#endif
    if (epd->ic == DRV_IC_UC8176) return INKTAG_UC8176_REFRESH_RELEASE_TIMEOUT_MS;
    return UINT16_MAX;
}

static void UC8xxx_WaitActiveLowRefresh(epd_model_t* epd) {
    EPD_WaitBusyEdge(false, UC8XXX_REFRESH_ASSERT_TIMEOUT_MS, UC8xxx_RefreshTimeout(epd));
}

#endif

// Read temperature from driver chip
int8_t UC8xxx_ReadTemp(epd_model_t* epd) {
#if INKTAG_EPD_ENABLE_UC8151
    if (epd->ic == DRV_IC_UC8151) return INT8_MIN;
#endif
    if (UC8xxx_IsHanshowNowa213(epd)) {
        uint8_t raw[2] = {0xFFu, 0xFFu};
        /* Stock Nowa-213 sends TSC 0x40 after PON, waits for BUSY_N to
         * release, then clocks both response bytes in one read transaction.
         * The port caches only a validated two-byte controller sample. */
        EPD_WriteCmd(UC8xxx_TSC);
        UC8xxx_WaitBusy(100u);
        EPD_ReadData(raw, sizeof(raw));
        return (int8_t)raw[0];
    }
    /* TSC is a synchronous read command, not a PON/DRF-style operation with
     * a documented BUSY cycle. Read the response directly instead of adding
     * a fixed delay or requiring a BUSY edge that some panels never emit. */
    EPD_WriteCmd(UC8xxx_TSC);
    return (int8_t)EPD_ReadByte();
}

static void UC8xxx_SetWindow(epd_model_t* epd, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    (void)epd;
    uint16_t left = x & 0xFFF8u;
    uint16_t right = (x + w - 1) | 7;
    uint16_t bottom = y + h - 1;
    EPD_Write(UC8xxx_PTL, left >> 8, left & 255, right >> 8, right & 255,
              y >> 8, y & 255, bottom >> 8, bottom & 255, 0x00);
}

void UC8xxx_Refresh(epd_model_t* epd) {
    EPD_DEBUG("refresh begin");
    UC8XXX_TRACE("refresh:enter");

    bool nowa = UC8xxx_IsHanshowNowa213(epd);
    bool global_bwr = UC8xxx_IsUC8151Bwr(epd);
    if (nowa || global_bwr) UC8xxx_WaitBusy(1000);
    if (global_bwr) UC8xxx_PowerOn(epd);
    else if (!nowa) UC8xxx_SetWindow(epd, 0, 0, epd->width, epd->height);
    /* Finish the UC8176 data transaction before activating its waveform. */
    if (!nowa && epd->ic == DRV_IC_UC8176) {
        EPD_WriteCmd(UC8xxx_DSP);
        UC8XXX_TRACE("refresh:dsp");
    }
    EPD_WriteCmd(UC8xxx_DRF);
    UC8XXX_TRACE("refresh:cmd");
#if defined(INKTAG_DIALOG)
    /* DA14585 must return to its SDK main loop so BLE events can run while a
     * slow panel refreshes. The Dialog service polls the same active-low BUSY
     * edge without changing this controller command sequence. */
    EPD_BusyDiagReset();
    UC8XXX_TRACE("refresh:pending");
#else
    UC8xxx_WaitActiveLowRefresh(epd);
    UC8XXX_TRACE("refresh:wait");
#endif

    EPD_DEBUG("refresh end");
}

void UC8xxx_Init(epd_model_t* epd) {
    if (UC8xxx_IsHanshowNowa213(epd)) {
        EPD_Reset(true, 50u);
        /* The captured module relies on panel MTP. Its selected path does
         * not upload a LUT or set a partial window; CDI 0x57 is the normal
         * branch before the controller temperature/read sequence. */
        EPD_Write(UC8xxx_CDI, 0x57);
        UC8xxx_PowerOn(epd);
        (void)UC8xxx_ReadTemp(epd);
        return;
    }
    /* DA qualified UC reset cadence is retained. */
    EPD_Reset(true, 100u);
    const uint8_t *program = NULL;
    uint8_t length = 0;
    bool resolution = true;
    bool generic = true;
#if INKTAG_EPD_ENABLE_UC8159
    if (epd->ic == DRV_IC_UC8159) {
        static const uint8_t panel_registers[] = {
            UC8xxx_PWR, 2, 0x37, 0x00,
            UC8xxx_PSR, 2, 0xCF, 0x08,
            UC8xxx_PLL, 1, 0x3A,
            UC8xxx_VDCS, 1, 0x28,
            UC8xxx_BTST, 3, 0xC7, 0xCC, 0x15,
            UC8xxx_CDI, 1, 0x77,
            UC8xxx_TCON, 1, 0x22,
            0x65, 1, 0x00,
            0xE5, 1, 0x03,
        };
        program = panel_registers;
        length = sizeof(panel_registers);
        generic = false;
    }
#endif
#if INKTAG_EPD_ENABLE_UC8151
    if (epd->ic == DRV_IC_UC8151) {
        /* Pervasive global BWR hardware sequence; data follow before PON. */
        static const uint8_t panel_registers[] = {
            UC8xxx_TSSET, 1, 0x15,
            UC8xxx_CCSET, 1, 0x02,
            UC8xxx_PSR, 2, 0xCF, 0x89,
        };
        EPD_Write(UC8xxx_PSR, 0x0E);
        delay(5);
        program = panel_registers;
        length = sizeof(panel_registers);
        resolution = generic = false;
    }
#endif
    if (program != NULL) EPD_WriteInitCommands(program, length);
    if (generic) EPD_Write(UC8xxx_PSR, epd->color == COLOR_BWR ? 0x0F : 0x1F);
    if (resolution) {
        EPD_Write(UC8xxx_TRES, epd->width >> 8, epd->width & 255,
                  epd->height >> 8, epd->height & 255);
    }
    if (generic) EPD_Write(UC8xxx_CDI, epd->color == COLOR_BWR ? 0x77 : 0x97);
    if (!UC8xxx_IsUC8151Bwr(epd)) {
        UC8xxx_PowerOn(epd);
        UC8xxx_SetWindow(epd, 0, 0, epd->width, epd->height);
    }
}

void UC8xxx_Clear(epd_model_t* epd, bool refresh) {
#if INKTAG_EPD_ENABLE_STELLAR154
    if (Stellar154_Is(epd)) {
        if (!s_stellar_ok || EPD_AbortRequested()) return;
        EPD_FillRAM(UC8xxx_DTM1,0xFFu,5000u);
        EPD_FillRAM(UC8xxx_DTM2,0x00u,5000u);
        if (refresh) Stellar154_Refresh();
        return;
    }
#endif

    uint32_t bytes = (uint32_t)((epd->width + 7) / 8) * epd->height;
#if INKTAG_EPD_ENABLE_UC8159
    if (epd->ic == DRV_IC_UC8159) {
        EPD_WriteCmd(UC8xxx_DTM1);
        for (uint32_t pairs = bytes * 4; pairs != 0; --pairs) EPD_WriteByte(0x33);
    } else
#endif
    {
        bool global_bwr = UC8xxx_IsUC8151Bwr(epd);
        EPD_FillRAM(UC8xxx_DTM1, global_bwr ? 0x00 : 0xFF, bytes);
        EPD_FillRAM(UC8xxx_DTM2, global_bwr || UC8xxx_IsHanshowNowa213(epd) ? 0x00 : 0xFF, bytes);
    }
    if (refresh) UC8xxx_Refresh(epd);
}

void UC8176_WriteImage(epd_model_t* epd, uint8_t* black, uint8_t* color, uint16_t x, uint16_t y, uint16_t w,
                       uint16_t h) {
    uint16_t stride = (w + 7) / 8;
    bool global_bwr = UC8xxx_IsUC8151Bwr(epd);
    bool windowed = !global_bwr && !UC8xxx_IsHanshowNowa213(epd);
    x &= 0xFFF8u;
    w = stride * 8;
    if (x + w > epd->width || y + h > epd->height) return;
    if (windowed) {
        EPD_WriteCmd(UC8xxx_PTIN);
        UC8xxx_SetWindow(epd, x, y, w, h);
    }
    const uint8_t *source = black;
    uint8_t first = epd->color == COLOR_BWR ? 0 : 1;
    for (uint8_t index = first; index < 2; ++index) {
        EPD_WriteCmd(index == 0 ? UC8xxx_DTM1 : UC8xxx_DTM2);
        UC8xxx_WriteRows(source, stride, (uint8_t)stride, h, 0xFF,
                         global_bwr || (index != 0 && UC8xxx_IsHanshowNowa213(epd)));
        source = color;
    }
    if (windowed) EPD_WriteCmd(UC8xxx_PTOUT);
}

#if INKTAG_EPD_ENABLE_UC8159
static void UC8159_SendPixel(uint8_t black_data, uint8_t color_data) {
    for (uint8_t bit = 0x80; bit != 0; bit >>= 2) {
        uint8_t next = bit >> 1;
        uint8_t left = !(color_data & bit) ? 4 : (!(black_data & bit) ? 0 : 3);
        uint8_t right = !(color_data & next) ? 4 : (!(black_data & next) ? 0 : 3);
        EPD_WriteByte((uint8_t)((left << 4) | right));
    }
}

void UC8159_WriteImage(epd_model_t* epd, uint8_t* black, uint8_t* color, uint16_t x, uint16_t y, uint16_t w,
                       uint16_t h) {
    uint16_t stride = (w + 7) / 8;
    x &= 0xFFF8u;
    w = stride * 8;
    if (x + w > epd->width || y + h > epd->height) return;
    EPD_WriteCmd(UC8xxx_PTIN);
    UC8xxx_SetWindow(epd, x, y, w, h);
    EPD_WriteCmd(UC8xxx_DTM1);
    uint32_t count = (uint32_t)(w / 8) * h;
    for (uint32_t offset = 0; offset < count; ++offset)
        UC8159_SendPixel(black == NULL ? 0xFF : black[offset],
                         color == NULL ? 0xFF : color[offset]);
    EPD_WriteCmd(UC8xxx_PTOUT);
}
#endif

void UC8xxx_WriteImage(epd_model_t* epd, uint8_t* black, uint8_t* color, uint16_t x, uint16_t y, uint16_t w,
                       uint16_t h) {
#if INKTAG_EPD_ENABLE_STELLAR154
    if (Stellar154_Is(epd)) {
        /* Only a complete frame is admitted; GUI streams complete planes. */
        if (!s_stellar_ok || EPD_AbortRequested() || !black || x || y || w != 200u || h != 200u) return;
        EPD_WriteCmd(UC8xxx_DTM1);
        for (uint16_t row = 0; row < 200u; ++row) EPD_WriteData(black+row*25u,25u);
        EPD_WriteCmd(UC8xxx_DTM2);
        for (uint16_t row = 0; row < 200u; ++row)
            if (color) UC8xxx_WriteInvertedData(color+row*25u,25u);
            else for (uint8_t col = 0; col < 25u; ++col) EPD_WriteByte(0x00u);
        return;
    }
#endif
#if INKTAG_EPD_ENABLE_UC8159
    if (epd->ic == DRV_IC_UC8159) {
        UC8159_WriteImage(epd, black, color, x, y, w, h);
        return;
    }
#endif
    UC8176_WriteImage(epd, black, color, x, y, w, h);
}

void UC8xxx_WriteRam(epd_model_t* epd, bool begin, bool black, uint8_t* data, uint8_t len) {
#if INKTAG_EPD_ENABLE_STELLAR154
    if (Stellar154_Is(epd)) {
        if (!s_stellar_ok || EPD_AbortRequested() || data == NULL) return;
        if (begin) EPD_WriteCmd(black ? UC8xxx_DTM1 : UC8xxx_DTM2);
        if (black) EPD_WriteData(data,len);
        else UC8xxx_WriteInvertedData(data,len);
        return;
    }
#endif

    bool global_bwr = UC8xxx_IsUC8151Bwr(epd);
    bool nowa = UC8xxx_IsHanshowNowa213(epd);
    if (begin && black && !global_bwr && !nowa)
        UC8xxx_SetWindow(epd, 0, 0, epd->width, epd->height);
#if INKTAG_EPD_ENABLE_UC8159
    if (epd->ic == DRV_IC_UC8159) {
        if (begin) EPD_WriteCmd(UC8xxx_DTM1);
        EPD_WriteData(data, len);
        return;
    }
#endif
    if (begin) {
        uint8_t command = epd->color == COLOR_BWR && black ? UC8xxx_DTM1 : UC8xxx_DTM2;
        EPD_WriteCmd(command);
    }
    if (global_bwr || (nowa && !black)) UC8xxx_WriteInvertedData(data, len);
    else EPD_WriteData(data, len);
}

void UC8xxx_Sleep(epd_model_t* epd) {
    UC8xxx_PowerOff(epd);
    EPD_Write(UC8xxx_DSLP, 0xA5);
}

// Declare driver and models
const epd_driver_t epd_drv_uc8xxx = {
    .init = UC8xxx_Init,
    .clear = UC8xxx_Clear,
    .write_image = UC8xxx_WriteImage,
    .write_ram = UC8xxx_WriteRam,
    .refresh = UC8xxx_Refresh,
    .sleep = UC8xxx_Sleep,
    .read_temp = UC8xxx_ReadTemp,
    .read_busy = UC8xxx_ReadBusy,
};

// UC8176 400x300 Black/White
