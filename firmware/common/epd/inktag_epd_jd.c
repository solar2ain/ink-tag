#include "inktag_epd_driver.h"

#include <string.h>

static bool JD79xxx_IsLenmory364(epd_model_t* epd);
static void JD79xxx_Init(epd_model_t* epd);
static void JD79xxx_Refresh(epd_model_t* epd);
static void JD79xxx_Clear(epd_model_t* epd, bool refresh);
static void JD79xxx_WriteRam(epd_model_t* epd, bool begin, bool black, uint8_t* data, uint8_t len);
static void JD79xxx_Sleep(epd_model_t* epd);

#include "inktag_epd_jd_shared.inc"

static bool JD79xxx_IsLenmory364(epd_model_t* epd) {
    (void)epd;
    return false;
}

bool JD79xxx_OperationOK(epd_model_t* epd) {
    if (JD79xxx_Is398A0(epd)) return s_huawei_a0_ok && !EPD_AbortRequested();
    if (JD79xxx_Is398A1(epd))
        return !s_huawei_a1_wait_failed && !EPD_AbortRequested();
#if INKTAG_EPD_ENABLE_HANSHOW_SMALL_Q || INKTAG_EPD_ENABLE_HANSHOW_STELLAR_Q154
    if (JD79xxx_IsSmallQ(epd) || JD79xxx_IsStellarQ154(epd))
        return s_small_q_ok && !EPD_AbortRequested();
#endif
    (void)epd;
    return true;
}

static void JD79xxx_Init(epd_model_t* epd) {
    if (JD79xxx_Is398A0(epd)) s_huawei_a0_ok = true;
    if (JD79xxx_Is398A1(epd)) s_huawei_a1_wait_failed = false;
    int8_t temperature;
    if (JD79xxx_IsHanshow437(epd)) {
        s_hanshow437_auto_complete = false;
        /* Exact stock reset at 0047D972: H2/L2/H20/L2/H2 ms.
         * Each helper preserves cancellation and maintenance callbacks. */
        EPD_ResetPulse(2u, 2u, 20u);
        EPD_ResetPulse(0u, 2u, 2u);
        static const uint8_t commands[] = {
            0x4D, 1u, 0x78, 0xAE, 1u, 0xAF, 0xBD, 1u, 0x80,
            0xB6, 1u, 0x0F, 0x00, 2u, 0x07, 0x29,
            0x01, 2u, 0x07, 0x00, 0x03, 3u, 0x10, 0x54, 0x44,
            0x06, 7u, 0x0F, 0x03, 0x3F, 0x15, 0x37, 0x25, 0x29,
            0x30, 1u, 0x08, 0x50, 1u, 0x57,
            0xF0, 1u, 0x7D, 0x61, 4u, 0x00, 0xB0, 0x01, 0xE0,
            0xE3, 1u, 0x22, 0xE7, 1u, 0x1C, 0xE9, 1u, 0x01,
            0x41, 1u, 0x00,
        };
        EPD_WriteInitCommands(commands, sizeof(commands));
        (void)JD79xxx_ReadTemp(epd);
        /* The stock internal0x13 path powers/refreshes using AUTO A5.
         * Do not borrow the TLSR420 manual PON/PTL/DRF sequence. */
        return;
    }
    if (JD79xxx_Is398A0(epd)) {
        /* The removable panel owns this reset, not the fitted SSD panel's
         * short board-level reset. Match the EPD Lab vendor A0 sequence. */
        EPD_ResetPulse(50u, 50u, 50u);
        JD79xxx_WaitBusy(2000u);
        if (!EPD_ReadBusy() || EPD_AbortRequested()) {
            s_huawei_a0_ok = false;
            return;
        }
        delay(30u);
    } else if (JD79xxx_Is398A1(epd)) {
        EPD_ResetPulse(20u, 2u, 20u);
        if (!EPD_ReadBusy()) {
            JD79xxx_WaitBusy(2000u);
            if (!EPD_ReadBusy()) {
                s_huawei_a1_wait_failed = true;
                return;
            }
        }
        if (EPD_AbortRequested()) {
            s_huawei_a1_wait_failed = true;
            return;
        }
        delay(30u);
        if (EPD_AbortRequested()) {
            s_huawei_a1_wait_failed = true;
            return;
        }
    } else {
        EPD_Reset(true, JD79xxx_IsHanshow420(epd) ? 200u : 50u);
    }
#if INKTAG_EPD_ENABLE_HANSHOW_STELLAR_Q154
    if (JD79xxx_IsStellarQ154(epd)) {
        JD79xxx_StellarQ154_Init(epd);
        return;
    }
#endif
#if INKTAG_EPD_ENABLE_HANSHOW_SMALL_Q
    if (JD79xxx_IsSmallQ(epd)) {
        JD79xxx_SmallQ_Init(epd);
        return;
    }
#endif
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
    if (JD79xxx_Is398A1(epd) && !JD79xxx_OperationOK(epd)) return;
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
    if ((JD79xxx_Is398A0(epd) || JD79xxx_Is398A1(epd)) &&
        !JD79xxx_OperationOK(epd)) return;
    if (JD79xxx_IsHanshow437(epd)) {
        s_hanshow437_auto_complete = false;
        EPD_Write(JD79xxx_AUTO, 0xA5);
        s_hanshow437_auto_complete =
            EPD_WaitBusyEdge(false, 500u, 60000u);
        return;
    }

#if INKTAG_EPD_ENABLE_HANSHOW_STELLAR_Q154
    if (JD79xxx_IsStellarQ154(epd)) {
        static const uint8_t prepare[] = {
            0xff,1,0xa5, 0xef,8,0x03,0x44,0x07,0x32,0x07,0x16,0x09,0x33,
            0xdc,1,0x01, 0xdd,1,0x06, 0xde,1,0x70,
            0xe8,1,0x00, 0xda,1,0x00, 0xff,1,0xe3,
        };
        if (!JD79xxx_OperationOK(epd)) return;
        EPD_WriteInitCommands(prepare, sizeof(prepare));
        EPD_WriteCmd(JD79xxx_PON);
        s_small_q_ok = JD79xxx_StellarQ154_Wait(5000u);
        if (!s_small_q_ok) return;
        EPD_Write(JD79xxx_DRF, 0x00);
        s_small_q_ok = JD79xxx_StellarQ154_Wait(60000u);
        return;
    }
#endif
#if INKTAG_EPD_ENABLE_HANSHOW_SMALL_Q
    if (JD79xxx_IsSmallQ(epd)) {
        if (!s_small_q_ok || EPD_AbortRequested()) return;
        EPD_Write(JD79xxx_AUTO, 0xa5);
        s_small_q_ok = EPD_WaitBusyEdge(false, 500u, 60000u);
        return;
    }
#endif
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
    if (JD79xxx_Is398A0(epd)) {
        s_huawei_a0_ok = EPD_WaitBusyEdge(false, 500u, 60000u) &&
                         !EPD_AbortRequested();
    } else if (JD79xxx_Is398A1(epd)) {
        delay(2u);
        s_huawei_a1_wait_failed =
            !EPD_WaitBusyEdge(false, 500u, 60000u) || EPD_AbortRequested();
    } else {
        delay(100);
        JD79xxx_WaitBusy(UINT16_MAX);
    }
    EPD_DEBUG("refresh end");
}

static void JD79xxx_Clear(epd_model_t* epd, bool refresh) {
    uint16_t row_bytes = (epd->width + 3) / 4;
    if ((JD79xxx_IsStellarQ154(epd) || JD79xxx_Is398A1(epd)) &&
        !JD79xxx_OperationOK(epd)) return;
    if (!JD79xxx_IsFullFramePacked(epd) && !JD79xxx_Is398A1(epd))
        JD79xxx_SetWindow(epd, 0, 0, epd->width, epd->height);
    EPD_WriteCmd(JD79xxx_DTM1);
    JD79xxx_WriteRepeatData(0x55, (uint32_t)row_bytes * epd->height);
    if (refresh) JD79xxx_Refresh(epd);
}

static void JD79xxx_WriteRam(epd_model_t* epd, bool begin, bool black, uint8_t* data, uint8_t len) {
    if ((JD79xxx_IsStellarQ154(epd) || JD79xxx_Is398A1(epd)) &&
        !JD79xxx_OperationOK(epd)) return;
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
#if INKTAG_EPD_ENABLE_HANSHOW_STELLAR_Q154
    if (JD79xxx_IsStellarQ154(epd)) {
        if (JD79xxx_OperationOK(epd)) {
            EPD_Write(JD79xxx_POF, 0x00);
            s_small_q_ok = JD79xxx_StellarQ154_Wait(5000u);
            if (s_small_q_ok) {
                EPD_Write(JD79xxx_DSLP, 0xa5);
                return;
            }
        }
        s_small_q_ok = false;
        EPD_Reset(true, 50u);
        return;
    }
#endif
    if (JD79xxx_IsHanshow437(epd) && s_hanshow437_auto_complete) {
        /* Stock selected13 state2 at0047D906 waits200000us then07,A5.
         * POF has already been included in AUTO17,A5. */
        delay(200u);
        s_hanshow437_auto_complete = false;
    } else if (!JD79xxx_IsHanshow420(epd) && !JD79xxx_IsSmallQ(epd)) {
        JD79xxx_PowerOff(epd);
        delay(100);
    }
    EPD_Write(JD79xxx_DSLP, 0xA5);
}

const epd_driver_t epd_drv_jd79xxx = INKTAG_JD_DRIVER_INITIALIZER;
