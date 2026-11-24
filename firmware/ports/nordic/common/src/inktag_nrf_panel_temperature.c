#include "inktag_nrf_build_profile.h"
#include "inktag_nrf_panel_temperature.h"
#include "inktag_board.h"
#include "nrf_error.h"
#include <limits.h>
#include "inktag_epd_temperature.h"

bool inktag_nrf_panel_temperature_requires_awake(const epd_model_t *epd) {
    /* Also used for final cleanup when optional temperature reads are off. */
    if (epd == NULL) return false;
    if (epd->driver_id == EPD_SCREEN_DRIVER_JD79665AA &&
        epd->color == COLOR_BWRY && epd->width == 768u && epd->height == 552u &&
        (epd->panel_recipe == INKTAG_RECIPE_JD79665AA_HUAWEI_398_A0 ||
         epd->panel_recipe == INKTAG_RECIPE_JD79665AA_HUAWEI_398_A1)) return true;
#if INKTAG_BUILD_PRO
#define INKTAG_NRF_PRO_SECTION 3
#include "inktag_nrf_epd_e6_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#else
    return false;
#endif
}

static inline bool inktag_nrf_panel_temperature_supported(epd_model_t *epd) {
#if INKTAG_NRF_PANEL_TEMPERATURE_ENABLED && defined(INKTAG_NRF_BOARD_NRF52832_PRICETAG)
    return epd != NULL && epd->drv != NULL && epd->drv->read_busy != NULL &&
        ((epd->driver_id == EPD_SCREEN_DRIVER_SSD1675 &&
        epd->panel_recipe == INKTAG_RECIPE_SSD1675_STANDARD &&
        epd->width == 152u && epd->height == 152u && epd->color == COLOR_BW) ||
        (inktag_nrf_panel_temperature_requires_awake(epd) && epd->drv->read_temp != NULL));
#else
    (void)epd;
    return false;
#endif
}

bool inktag_nrf_panel_temperature_sample(epd_model_t *epd, int16_t *sixteenths) {
#if INKTAG_NRF_PANEL_TEMPERATURE_ENABLED && \
    defined(INKTAG_NRF_BOARD_NRF52832_PRICETAG)
    bool valid;
    if (!inktag_nrf_panel_temperature_supported(epd) || sixteenths == NULL ||
        !EPD_BoardIsPrepared() || EPD_SPI_LastError() != NRF_SUCCESS ||
        EPD_AbortRequested()) return false;
    if (epd->driver_id == EPD_SCREEN_DRIVER_JD79665AA
#if INKTAG_BUILD_PRO
#define INKTAG_NRF_PRO_SECTION 4
#include "inktag_nrf_epd_e6_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#endif
) {
        /* A common init may already have requested R40. Do not issue another
         * command while that exact active-low JD controller is busy. */
        if (epd->drv->read_busy(epd)) EPD_WaitBusy(false, 2000u);
        if (EPD_AbortRequested() || EPD_SPI_LastError() != NRF_SUCCESS ||
            epd->drv->read_busy(epd)) return false;
        uint8_t raw[2] = {0xffu, 0xffu};
        valid = EPD_DriverOperationOK(epd) &&
            EPD_ReadCommand(0x40u, raw, sizeof(raw)) &&
            inktag_jd_r40_temperature_sixteenths(true, raw, sixteenths);
        if (!EPD_AbortRequested() && EPD_SPI_LastError() == NRF_SUCCESS &&
            epd->drv->read_busy(epd)) EPD_WaitBusy(false, 2000u);
    }
#if INKTAG_BUILD_PRO
#define INKTAG_NRF_PRO_SECTION 5
#include "inktag_nrf_epd_e6_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#endif
    else {
        valid = SSD1675_ReadTemperature(epd, sixteenths);
    }
    if (EPD_SPI_LastError() != NRF_SUCCESS || EPD_AbortRequested() ||
        epd->drv->read_busy(epd)) {
        /* Acquisition also runs between init and RAM transfer. It must never
         * destroy that initialized controller. Its operation owner decides
         * failure/reset/sleep after inspecting the original result. */
        valid = false;
    }
    return valid;
#else
    (void)epd;
    (void)sixteenths;
    return false;
#endif
}

/* Caller has exclusive display ownership and a fresh valid supply sample.
 * Stock init resets the controller but does not write pixels or trigger DRF. */
bool inktag_nrf_panel_temperature_probe(epd_model_t *epd, int16_t *sixteenths) {
#if INKTAG_NRF_PANEL_TEMPERATURE_ENABLED && defined(INKTAG_NRF_BOARD_NRF52832_PRICETAG)
    bool valid;
    if (!inktag_nrf_panel_temperature_supported(epd) || sixteenths == NULL ||
        epd->drv->init == NULL || epd->drv->sleep == NULL ||
        EPD_BoardIsPrepared() || EPD_AbortRequested()) return false;
    EPD_GPIO_Init();
    EPD_BoardPrepare();
    EPD_SPI_ClearError();
    epd->drv->init(epd);
    valid = inktag_nrf_panel_temperature_sample(epd, sixteenths);
    if (EPD_SPI_LastError() != NRF_SUCCESS || EPD_AbortRequested() ||
        epd->drv->read_busy(epd)) EPD_Reset(true, 10u);
    if (!EPD_AbortRequested() || inktag_nrf_panel_temperature_requires_awake(epd))
        epd->drv->sleep(epd);
    valid = valid && !EPD_AbortRequested() && EPD_SPI_LastError() == NRF_SUCCESS;
    EPD_BoardRelease();
    EPD_GPIO_Uninit();
    return valid;
#else
    (void)epd; (void)sixteenths;
    return false;
#endif
}
