#include "inktag_epd_driver.h"

#include <string.h>

// JD79xxx command values overlap with several UC8xxx commands, but the
// controllers have their own initialization sequences and pixel format.
enum {
    JD79xxx_PSR = 0x00,
    JD79xxx_POF = 0x02,
    JD79xxx_PON = 0x04,
    JD79xxx_BTST = 0x06,
    JD79xxx_DSLP = 0x07,
    JD79xxx_DTM1 = 0x10,
    JD79xxx_DTM2 = 0x13,
    JD79xxx_DRF = 0x12,
    JD79xxx_AUTO = 0x17,
    JD79xxx_PLL = 0x30,
    JD79xxx_TSC = 0x40,
    JD79xxx_TSE = 0x41,
    JD79xxx_CDI = 0x50,
    JD79xxx_TCON = 0x60,
    JD79xxx_TRES = 0x61,
    JD79xxx_GSST = 0x65,
    JD79xxx_PTL = 0x83,
    JD79xxx_AREA_EN = 0x8F,
    JD79xxx_PWS = 0xE3,
};


static bool JD79xxx_Is398A0(epd_model_t* epd) {
    return epd->driver_id == EPD_SCREEN_DRIVER_JD79665AA &&
           epd->panel_recipe == INKTAG_RECIPE_JD79665AA_HUAWEI_398_A0;
}

static bool JD79xxx_Is398A1(epd_model_t* epd) {
    return epd->driver_id == EPD_SCREEN_DRIVER_JD79665AA &&
           epd->panel_recipe == INKTAG_RECIPE_JD79665AA_HUAWEI_398_A1;
}

static bool JD79xxx_IsFactoryPacked(epd_model_t* epd) {
    return (epd->driver_id == EPD_SCREEN_DRIVER_JD79661 &&
            epd->panel_recipe == INKTAG_RECIPE_JD79661_ZKC21B_FACTORY_B2) ||
           (epd->driver_id == EPD_SCREEN_DRIVER_JD79668AA &&
            epd->panel_recipe == INKTAG_RECIPE_JD79668AA_ZKC42B_FACTORY_B3);
}

static bool JD79xxx_IsHanshow213(epd_model_t* epd) {
    return epd->driver_id == EPD_SCREEN_DRIVER_JD79XXX &&
           epd->panel_recipe == INKTAG_RECIPE_JD79XXX_HANSHOW_NEBULAR_213R_N;
}

static bool JD79xxx_IsHanshow420(epd_model_t* epd) {
    return epd->driver_id == EPD_SCREEN_DRIVER_JD79668AA &&
           epd->panel_recipe == INKTAG_RECIPE_JD79668AA_HANSHOW_NEBULAR_420R_N;
}

static bool JD79xxx_IsLenmory364(epd_model_t* epd) {
    (void)epd;
    return false;
}

static bool JD79xxx_IsFullFramePacked(epd_model_t* epd) {
    return JD79xxx_IsFactoryPacked(epd) || JD79xxx_IsHanshow420(epd) ||
           JD79xxx_IsHanshow213(epd);
}

static uint16_t JD79xxx_A0ControllerY(uint16_t logical_row) {
    return logical_row < 276 ? logical_row * 2 : 1103 - logical_row * 2;
}

static void JD79xxx_WaitBusy(uint16_t timeout) { EPD_WaitBusy(false, timeout); }

static void JD79xxx_PowerOn(epd_model_t* epd) {
    EPD_WriteCmd(JD79xxx_PON);
    if (JD79xxx_IsHanshow420(epd) || JD79xxx_Is398A1(epd)) {
        (void)EPD_WaitBusyEdge(false, 500u, 5000u);
        return;
    }
    JD79xxx_WaitBusy(JD79xxx_IsFactoryPacked(epd) ? UINT16_MAX : 200);
}

static void JD79xxx_PowerOff(epd_model_t* epd) {
    (void)epd;
    EPD_WriteCmd(JD79xxx_POF);
    EPD_WriteByte(0x00);
    if (JD79xxx_IsHanshow420(epd) || JD79xxx_Is398A1(epd)) {
        (void)EPD_WaitBusyEdge(false, 500u, 5000u);
        return;
    }
    JD79xxx_WaitBusy(JD79xxx_IsFactoryPacked(epd) ? 5000 : 200);
}

static void JD79xxx_SetWindow(epd_model_t* epd, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    (void)epd;
    EPD_Write(JD79xxx_PTL, x / 256, x % 256, (x + w - 1) / 256, (x + w - 1) % 256, y / 256, y % 256,
              (y + h - 1) / 256, (y + h - 1) % 256, 0x01);
}

static void JD79xxx_WriteDataChunks(uint8_t* data, uint32_t len) {
    while (len > 0) {
        uint8_t chunk = len > UINT8_MAX ? UINT8_MAX : (uint8_t)len;
        EPD_WriteData(data, chunk);
        data += chunk;
        len -= chunk;
    }
}

static void JD79xxx_WriteRepeatData(uint8_t value, uint32_t len) {
    uint8_t buffer[64];
    memset(buffer, value, sizeof(buffer));
    while (len > 0) {
        uint8_t chunk = len > sizeof(buffer) ? sizeof(buffer) : (uint8_t)len;
        EPD_WriteData(buffer, chunk);
        len -= chunk;
    }
}

static bool JD79xxx_ReadBusy(epd_model_t* epd) {
    (void)epd;
    return EPD_ReadBusy() == false;
}

bool JD79xxx_OperationOK(epd_model_t* epd) {
    (void)epd;
    return true;
}

bool JD79xxx_ReadTemperature(epd_model_t* epd, int8_t* temperature_out) {
    uint8_t raw[2] = {0xFFu, 0xFFu};
    int8_t whole_degrees;
    uint8_t quarter_fraction;
    if (epd == NULL || temperature_out == NULL ||
        (!JD79xxx_Is398A0(epd) && !JD79xxx_Is398A1(epd) &&
         !JD79xxx_IsLenmory364(epd) && !JD79xxx_IsHanshow213(epd))) return false;
    if (!EPD_ReadCommand(JD79xxx_TSC, raw, sizeof(raw)) ||
        (raw[0] == 0xFFu && raw[1] == 0xFFu)) return false;

    /* JD79665AA R40 returns a signed whole-degree byte followed by TS[9:8]
     * in bits 7:6. The fraction is positive even for a negative integer:
     * FF 40 is therefore -0.75 C, not -1.25 C. */
    whole_degrees = (int8_t)raw[0];
    quarter_fraction = (raw[1] >> 6u) & 0x03u;
    if (whole_degrees < -40 || whole_degrees > 85 ||
        (whole_degrees == 85 && quarter_fraction != 0u)) return false;
    *temperature_out = whole_degrees;
    return true;
}

static int8_t JD79xxx_ReadTemp(epd_model_t* epd) {
    int8_t decoded;
    if (JD79xxx_Is398A0(epd) || JD79xxx_Is398A1(epd) ||
        JD79xxx_IsLenmory364(epd) || JD79xxx_IsHanshow213(epd)) {
        return JD79xxx_ReadTemperature(epd, &decoded) ? decoded : INT8_MIN;
    }
    uint8_t temperature[2] = {0u, 0u};
    EPD_WriteCmd(JD79xxx_TSC);
    if (JD79xxx_IsHanshow420(epd)) {
        /* Hanshow performs two controller reads under one continuous CS
         * assertion. The second byte is fractional/calibration state; keep
         * both clocks before the qualified manual PON/DRF sequence. */
        (void)EPD_WaitBusyEdge(false, 100u, 5000u);
        EPD_ReadData(temperature, sizeof(temperature));
    } else {
        JD79xxx_WaitBusy(100);
        temperature[0] = EPD_ReadByte();
    }
    return (int8_t)temperature[0];
}

/* Recovered from the Nebular-420R-N profile-7 stock overlay and corrected by
 * the EPD Lab thin-edge regression. Both TRES and PTL are exactly 400x300;
 * using a padded 304-line TRES can repolarize the separate top-edge electrode. */
static void JD79668_Hanshow420_Init(epd_model_t* epd) {
    (void)epd;
    {
        static const uint8_t commands[] = {
            0x4D, 1u, 0x78,
            0xAE, 1u, 0x8F,
            0xBD, 1u, 0x80,
            0xB0, 1u, 0x13,
            0xAD, 1u, 0x17,
            0xB6, 1u, 0x0F,
            JD79xxx_PSR, 2u, 0x07, 0x29,
            0x01, 2u, 0x07, 0x00,
            0x03, 3u, 0x10, 0x54, 0x44,
            JD79xxx_BTST, 7u, 0x0F, 0x00, 0x3F, 0x0A, 0x25, 0x2D, 0x18,
            JD79xxx_PLL, 1u, 0x08,
            JD79xxx_CDI, 1u, 0x37,
            0xF0, 1u, 0x7D,
            JD79xxx_TRES, 4u, 0x01, 0x90, 0x01, 0x2C,
            JD79xxx_GSST, 4u, 0x00, 0x00, 0x00, 0x00,
            JD79xxx_PWS, 1u, 0x22,
            0xE7, 1u, 0x1C,
            0xE9, 1u, 0x01,
        };
        EPD_WriteInitCommands(commands, sizeof(commands));
    }
}

static void JD79668_Init(epd_model_t* epd) {
    if (JD79xxx_Is398A0(epd)) {
        {
            static const uint8_t commands[] = {
                JD79xxx_PSR, 1u, 0x0B,
                JD79xxx_TRES, 4u, 0x03, 0x00, 0x02, 0x28,
            };
            EPD_WriteInitCommands(commands, sizeof(commands));
        }
        return;
    }
    if (JD79xxx_Is398A1(epd)) {
        {
            static const uint8_t commands[] = {
                0xAA, 6u, 0x49, 0x55, 0x20, 0x08, 0x09, 0x18,
                0x01, 1u, 0x3F,
                JD79xxx_PSR, 2u, 0x4B, 0x69,
                0x05, 4u, 0x40, 0x1F, 0x1F, 0x2C,
                0x08, 4u, 0x6F, 0x1F, 0x1F, 0x22,
                JD79xxx_BTST, 4u, 0x6F, 0x1F, 0x14, 0x14,
                0x03, 4u, 0x00, 0x54, 0x00, 0x44,
                JD79xxx_TCON, 2u, 0x02, 0x00,
                JD79xxx_PLL, 1u, 0x08,
                JD79xxx_CDI, 1u, 0x3F,
                JD79xxx_TRES, 4u, 0x03, 0x00, 0x02, 0x28,
                JD79xxx_GSST, 4u, 0x10, 0x00, 0x20, 0x00,
                JD79xxx_PWS, 1u, 0x2F,
                0x84, 1u, 0x01,
            };
            EPD_WriteInitCommands(commands, sizeof(commands));
        }
        /* The CJ1TM/NK865 A1 sample requires PTL at three distinct state
         * boundaries. This is the pre-PON latch; init below reasserts it
         * after PON, and refresh reasserts it before DRF. */
        JD79xxx_SetWindow(epd, 0, 0, epd->width, epd->height);
        return;
    }

    {
        static const uint8_t commands[] = {
            0x4D, 1u, 0x78,
            JD79xxx_PSR, 2u, 0x0F, 0x29,
            JD79xxx_BTST, 7u, 0x0D, 0x12, 0x24, 0x25, 0x12, 0x29, 0x10,
            JD79xxx_PLL, 1u, 0x08,
            JD79xxx_CDI, 1u, 0x37,
        };
        EPD_WriteInitCommands(commands, sizeof(commands));
    }
    EPD_Write(JD79xxx_TRES, epd->width / 256, epd->width % 256, epd->height / 256, epd->height % 256);
    {
        static const uint8_t commands[] = {
            0xAE, 1u, 0xCF,
            0xB0, 1u, 0x13,
            0xBD, 1u, 0x07,
            0xBE, 1u, 0xFE,
            0xE9, 1u, 0x01,
        };
        EPD_WriteInitCommands(commands, sizeof(commands));
    }
}

static void JD79665_Init(epd_model_t* epd) {
    {
        static const uint8_t commands[] = {
            0x4D, 1u, 0x78,
            JD79xxx_PSR, 2u, 0x2F, 0x29,
            JD79xxx_BTST, 4u, 0x0F, 0x8B, 0x93, 0xA1,
            JD79xxx_TSE, 1u, 0x00,
            JD79xxx_CDI, 1u, 0x37,
            JD79xxx_TCON, 2u, 0x02, 0x02,
        };
        EPD_WriteInitCommands(commands, sizeof(commands));
    }
    EPD_Write(JD79xxx_TRES, epd->width / 256, epd->width % 256, epd->height / 256, epd->height % 256);
    EPD_Write(0x62, 0x98, 0x98, 0x98, 0x75, 0xCA, 0xB2, 0x98, 0x7E);
    if (epd->width == 800u && epd->height == 480u)
        EPD_Write(JD79xxx_GSST, 0x00, 0x00, 0x00, 0x00);
    else
        EPD_Write(JD79xxx_GSST, 0x00, 0x10, 0x00, 0x00);
    {
        static const uint8_t commands[] = {
            0xE7, 1u, 0x1C,
            JD79xxx_PWS, 1u, 0x00,
            0xE9, 1u, 0x01,
            JD79xxx_PLL, 1u, 0x08,
        };
        EPD_WriteInitCommands(commands, sizeof(commands));
    }
}


/* Recovered from the factory B2 panel dispatch selected by ZKC21B-NAE4.
 * The 128x250 panel streams one complete packed 2-bpp frame through DTM1. */
static void JD79xxx_B2_213_Init(void) {
    {
        static const uint8_t commands[] = {
            0x4D, 1u, 0x78,
            JD79xxx_PSR, 2u, 0x07, 0x29,
            JD79xxx_TRES, 4u, 0x00, 0x80, 0x00, 0xFA,
            0xBE, 1u, 0xFF,
            0xE9, 1u, 0x01,
        };
        EPD_WriteInitCommands(commands, sizeof(commands));
    }
}

/* Exact Nebular-213R-N stock profile 3 sequence, separate from ZKC21B B2.
 * Its controller family is JD-compatible; the die marking remains unknown. */
static void JD79xxx_Hanshow213_Init(void) {
    {
        static const uint8_t commands[] = {
            JD79xxx_PSR, 2u, 0x07, 0x29,
            JD79xxx_BTST, 4u, 0x0F, 0x8B, 0x9C, 0x96,
            JD79xxx_PLL, 1u, 0x08,
            JD79xxx_CDI, 1u, 0x37,
            JD79xxx_TRES, 4u, 0x00, 0x80, 0x00, 0xFA,
            JD79xxx_GSST, 4u, 0x00, 0x00, 0x00, 0x00,
            0xE7, 1u, 0x96,
            0xE9, 1u, 0x01,
        };
        EPD_WriteInitCommands(commands, sizeof(commands));
    }
}

/* Recovered from the factory B3 panel dispatch selected by ZKC42B-NE4.
 * Keep this separate from the public JD79668 profile: the command family
 * overlaps, but the exact controller die has not been identified. */
static void JD79xxx_B3_420_Init(void) {
    {
        static const uint8_t commands[] = {
            0x4D, 1u, 0x78,
            JD79xxx_PSR, 2u, 0x07, 0x29,
            0xAE, 1u, 0xCF,
            0xB0, 1u, 0x13,
            0xBD, 1u, 0x07,
            0xBE, 1u, 0xFE,
            0xE9, 1u, 0x01,
        };
        EPD_WriteInitCommands(commands, sizeof(commands));
    }
}

static void JD79xxx_Init(epd_model_t* epd) {
    int8_t temperature;
    if (JD79xxx_Is398A1(epd)) {
        EPD_ResetPulse(20u, 2u, 20u);
        if (!EPD_ReadBusy()) JD79xxx_WaitBusy(2000u);
        delay(30u);
    } else {
        EPD_Reset(true, JD79xxx_IsHanshow420(epd) ? 200u : 50u);
    }
    if (JD79xxx_IsHanshow420(epd)) {
        JD79668_Hanshow420_Init(epd);
        JD79xxx_SetWindow(epd, 0, 0, epd->width, epd->height);
        (void)JD79xxx_ReadTemp(epd);
        JD79xxx_PowerOn(epd);
        /* PON may restore the MTP window, so program the qualified visible
         * window again immediately before the DTM1 stream. */
        JD79xxx_SetWindow(epd, 0, 0, epd->width, epd->height);
        return;
    } else if (epd->driver_id == EPD_SCREEN_DRIVER_JD79661 &&
               epd->panel_recipe == INKTAG_RECIPE_JD79661_ZKC21B_FACTORY_B2)
        JD79xxx_B2_213_Init();
    else if (JD79xxx_IsHanshow213(epd))
        JD79xxx_Hanshow213_Init();
    else if (epd->driver_id == EPD_SCREEN_DRIVER_JD79668AA &&
             epd->panel_recipe == INKTAG_RECIPE_JD79668AA_ZKC42B_FACTORY_B3)
        JD79xxx_B3_420_Init();
    else if (JD79xxx_Is398A0(epd) || JD79xxx_Is398A1(epd))
        JD79668_Init(epd);
    else if (epd->ic == DRV_IC_JD79665)
        JD79665_Init(epd);
    else
        JD79668_Init(epd);
    if (JD79xxx_IsHanshow213(epd)) {
        /* Acquire before native drawing, using the existing checked R40
         * transaction/cache instead of the legacy uncached one-byte read. */
        (void)JD79xxx_ReadTemperature(epd, &temperature);
        return;
    }
    if (JD79xxx_IsFactoryPacked(epd)) return;
    JD79xxx_PowerOn(epd);
    JD79xxx_SetWindow(epd, 0, 0, epd->width, epd->height);
    if (JD79xxx_Is398A0(epd) || JD79xxx_Is398A1(epd)) {
        /* R40 is valid only after the controller is powered. Prime the same
         * checked two-byte path used by telemetry before native GUI rendering
         * asks the platform for its effective temperature. */
        (void)JD79xxx_ReadTemperature(epd, &temperature);
    }
}

static void JD79xxx_Refresh(epd_model_t* epd) {
    EPD_DEBUG("refresh begin");
    if (JD79xxx_IsHanshow420(epd)) {
        /* The qualified Hanshow path does not use AUTO/A5. It streams one
         * exact 400x300 packed frame, then explicitly runs DRF and POF.
         * BUSY_N is active low. */
        JD79xxx_SetWindow(epd, 0, 0, epd->width, epd->height);
        EPD_Write(JD79xxx_DRF, 0x00);
        (void)EPD_WaitBusyEdge(false, 500u, 60000u);
        JD79xxx_PowerOff(epd);
        EPD_DEBUG("refresh end");
        return;
    } else if (JD79xxx_IsFactoryPacked(epd) || JD79xxx_IsHanshow213(epd))
        JD79xxx_PowerOn(epd);
    else
        JD79xxx_SetWindow(epd, 0, 0, epd->width, epd->height);
    EPD_WriteCmd(JD79xxx_DRF);
    EPD_WriteByte(JD79xxx_Is398A0(epd) ? 0x01 : 0x00);
#if defined(INKTAG_DIALOG)
    if (JD79xxx_Is398A0(epd) || JD79xxx_Is398A1(epd)) {
        /* The four-colour Huawei waveforms complete in Dialog's event-aware
         * refresh state machine. Waiting for A1 here consumed its BUSY edge,
         * then made the outer state machine wait a second time until timeout.
         * Return immediately after DRF so BLE maintenance, cancellation and
         * the monotonic assert/release timeout observe the physical cycle. */
        EPD_DEBUG("refresh pending");
    } else
#endif
    if (JD79xxx_Is398A1(epd)) {
        delay(2u);
        (void)EPD_WaitBusyEdge(false, 500u, 60000u);
    } else {
        delay(100);
        JD79xxx_WaitBusy(UINT16_MAX);
    }
    EPD_DEBUG("refresh end");
}

static void JD79xxx_Clear(epd_model_t* epd, bool refresh) {
    uint16_t row_bytes = (epd->width + 3) / 4;
    if (!JD79xxx_IsFullFramePacked(epd) && !JD79xxx_Is398A1(epd))
        JD79xxx_SetWindow(epd, 0, 0, epd->width, epd->height);
    EPD_WriteCmd(JD79xxx_DTM1);
    JD79xxx_WriteRepeatData(0x55, (uint32_t)row_bytes * epd->height);
    if (refresh) JD79xxx_Refresh(epd);
}

static void JD79xxx_WriteImage(epd_model_t* epd, uint8_t* black, uint8_t* color, uint16_t x, uint16_t y,
                              uint16_t w, uint16_t h) {
    (void)color;
    uint16_t wb = (w + 3) / 4;
    bool is_a0 = JD79xxx_Is398A0(epd);
    x -= x % 4;
    w = wb * 4;
    if (x + w > epd->width || y + h > epd->height) return;

    if (JD79xxx_IsFullFramePacked(epd)) {
        /* Factory drivers do not program PTL between GUI pages. They issue
         * DTM1 once at offset zero and stream the complete packed frame. */
        if (x != 0 || w != epd->width) return;
        if (y == 0) EPD_WriteCmd(JD79xxx_DTM1);
        if (black)
            JD79xxx_WriteDataChunks(black, (uint32_t)wb * h);
        else
            JD79xxx_WriteRepeatData(0x55, (uint32_t)wb * h);
        return;
    }

    if (JD79xxx_Is398A1(epd)) {
        /* A1 is validated as one contiguous full-window DTM1 stream. Native
         * GUI strips are consecutive chunks of that stream, not independent
         * controller windows. */
        if (x != 0u || w != epd->width) return;
        if (y == 0u) EPD_WriteCmd(JD79xxx_DTM1);
        if (black)
            JD79xxx_WriteDataChunks(black, (uint32_t)wb * h);
        else
            JD79xxx_WriteRepeatData(0x55, (uint32_t)wb * h);
        return;
    }

    if (!is_a0) {
        JD79xxx_SetWindow(epd, x, y, w, h);
        EPD_WriteCmd(JD79xxx_DTM1);
        if (black)
            JD79xxx_WriteDataChunks(black, (uint32_t)wb * h);
        else
            JD79xxx_WriteRepeatData(0x55, (uint32_t)wb * h);
        return;
    }

    for (uint16_t i = 0; i < h; i++) {
        uint16_t controller_y = JD79xxx_A0ControllerY(y + i);
        JD79xxx_SetWindow(epd, x, controller_y, w, 1);
        EPD_WriteCmd(JD79xxx_DTM1);
        if (black)
            EPD_WriteData(&black[(uint32_t)i * wb], wb);
        else
            JD79xxx_WriteRepeatData(0x55, wb);
        if (((y + i) & 7u) == 7u) delay(1u);
    }
}

enum { JD79xxx_A0_ROW_BYTES = 768 / 4 };

typedef struct {
    uint16_t logical_row;
    uint16_t row_pos;
    uint8_t row[JD79xxx_A0_ROW_BYTES];
} jd79xxx_a0_stream_state_t;

#if defined(INKTAG_EPD_JD79XXX_A0_STREAM_STATE_BYTES)
_Static_assert(sizeof(jd79xxx_a0_stream_state_t) <=
                   INKTAG_EPD_JD79XXX_A0_STREAM_STATE_BYTES,
               "JD79xxx A0 stream state exceeds platform scratch");
#endif

#if defined(INKTAG_EPD_JD79XXX_A0_STREAM_STATE_SYMBOL)
/* RAM-executed targets may provide a transaction-scoped scratch arena instead
 * of paying for a second permanent row buffer. The caller must serialize
 * direct-frame, stored-image replay and native-GUI work for the full lifetime
 * of a write_ram stream. */
extern uint32_t INKTAG_EPD_JD79XXX_A0_STREAM_STATE_SYMBOL[];
static jd79xxx_a0_stream_state_t* JD79xxx_A0StreamState(void) {
    return (jd79xxx_a0_stream_state_t*)(void*)
        INKTAG_EPD_JD79XXX_A0_STREAM_STATE_SYMBOL;
}
#else
static jd79xxx_a0_stream_state_t s_jd79xxx_a0_stream_state;
static jd79xxx_a0_stream_state_t* JD79xxx_A0StreamState(void) {
    return &s_jd79xxx_a0_stream_state;
}
#endif

static void JD79xxx_A0WriteRam(epd_model_t* epd, bool begin, uint8_t* data, uint8_t len) {
    jd79xxx_a0_stream_state_t* state = JD79xxx_A0StreamState();

    if (begin) {
        state->logical_row = 0;
        state->row_pos = 0;
    }

    uint16_t row_bytes = (epd->width + 3) / 4;
    if (row_bytes > JD79xxx_A0_ROW_BYTES) return;

    while (len > 0 && state->logical_row < epd->height) {
        uint16_t chunk = row_bytes - state->row_pos;
        if (chunk > len) chunk = len;
        memcpy(&state->row[state->row_pos], data, chunk);
        state->row_pos += chunk;
        data += chunk;
        len -= chunk;

        if (state->row_pos == row_bytes) {
            uint16_t controller_y = JD79xxx_A0ControllerY(state->logical_row);
            JD79xxx_SetWindow(epd, 0, controller_y, epd->width, 1);
            EPD_WriteCmd(JD79xxx_DTM1);
            EPD_WriteData(state->row, row_bytes);
            state->logical_row++;
            state->row_pos = 0;
            if ((state->logical_row & 7u) == 0u) delay(1u);
        }
    }
}

static void JD79xxx_WriteRam(epd_model_t* epd, bool begin, bool black, uint8_t* data, uint8_t len) {
    if (JD79xxx_Is398A0(epd)) {
        JD79xxx_A0WriteRam(epd, begin, data, len);
        return;
    }
    if (begin && black && !JD79xxx_IsFullFramePacked(epd) && !JD79xxx_Is398A1(epd))
        JD79xxx_SetWindow(epd, 0, 0, epd->width, epd->height);
    if (begin) EPD_WriteCmd(JD79xxx_DTM1);
    EPD_WriteData(data, len);
}

static void JD79xxx_Sleep(epd_model_t* epd) {
    if (!JD79xxx_IsHanshow420(epd)) {
        JD79xxx_PowerOff(epd);
        delay(100);
    }
    EPD_Write(JD79xxx_DSLP, 0xA5);
}

const epd_driver_t epd_drv_jd79xxx = {
    .init = JD79xxx_Init,
    .clear = JD79xxx_Clear,
    .write_image = JD79xxx_WriteImage,
    .write_ram = JD79xxx_WriteRam,
    .refresh = JD79xxx_Refresh,
    .sleep = JD79xxx_Sleep,
    .read_temp = JD79xxx_ReadTemp,
    .read_busy = JD79xxx_ReadBusy,
};

// JD79668 400x300 Black/White/Red/Yellow
