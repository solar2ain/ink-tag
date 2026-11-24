/* DA normal profile: E6 implementations are not included. */
#if INKTAG_EPD_ENABLE_JD79665CA || INKTAG_EPD_ENABLE_OSPREY_E6
#error DA_source_source_requires_E6_disabled
#endif
#include "inktag_epd_driver.h"

#include <string.h>

#if INKTAG_EPD_ENABLE_UC8XXX
extern const epd_driver_t epd_drv_uc8xxx;
#endif
#if INKTAG_EPD_ENABLE_SSD1XXX
extern const epd_driver_t epd_drv_ssd1xxx;
#endif
#if INKTAG_EPD_ENABLE_JD79XXX
extern const epd_driver_t epd_drv_jd79xxx;
#endif

static epd_model_t s_runtime_model;

static bool resolve_driver(uint8_t driver_id, uint8_t panel_recipe,
                           const epd_driver_t **driver, epd_drv_ic_t *ic) {
    switch ((epd_screen_driver_id_t)driver_id) {
#if INKTAG_EPD_ENABLE_UC8XXX
        case EPD_SCREEN_DRIVER_UC8XXX:
        case EPD_SCREEN_DRIVER_UC8176:
        case EPD_SCREEN_DRIVER_UC8276:
            *driver = &epd_drv_uc8xxx;
            /* The currently qualified UC8276 panels share this full-refresh
             * command path. Their distinct protocol ID prevents UC8176 OTP/
             * external-LUT formats from being selected by name. */
            *ic = DRV_IC_UC8176;
            return true;
        case EPD_SCREEN_DRIVER_UC8151:
            *driver = &epd_drv_uc8xxx;
            *ic = DRV_IC_UC8151;
            return true;
#if INKTAG_EPD_ENABLE_UC8159
        case EPD_SCREEN_DRIVER_UC8159:
            *driver = &epd_drv_uc8xxx;
            *ic = DRV_IC_UC8159;
            return true;
#endif
#if INKTAG_EPD_ENABLE_UC8179
        case EPD_SCREEN_DRIVER_UC8179:
            *driver = &epd_drv_uc8xxx;
            *ic = DRV_IC_UC8179;
            return true;
#endif
#endif
#if INKTAG_EPD_ENABLE_SSD1XXX
        case EPD_SCREEN_DRIVER_SSD1619:
            *driver = &epd_drv_ssd1xxx;
            *ic = DRV_IC_SSD1619;
            return true;
#if INKTAG_EPD_ENABLE_SSD1675
        case EPD_SCREEN_DRIVER_SSD1675:
            *driver = &epd_drv_ssd1xxx;
            *ic = DRV_IC_SSD1675;
            return true;
#endif
#if INKTAG_EPD_ENABLE_SSD1677
        case EPD_SCREEN_DRIVER_SSD1677:
            *driver = &epd_drv_ssd1xxx;
            *ic = DRV_IC_SSD1677;
            return true;
#endif
        case EPD_SCREEN_DRIVER_SSD1680:
            *driver = &epd_drv_ssd1xxx;
            *ic = DRV_IC_SSD1680;
            return true;
#if INKTAG_EPD_ENABLE_SSD1683
        case EPD_SCREEN_DRIVER_SSD1683:
            *driver = &epd_drv_ssd1xxx;
            *ic = DRV_IC_SSD1683;
            return true;
#endif
#endif
#if INKTAG_EPD_ENABLE_JD79XXX
        case EPD_SCREEN_DRIVER_JD79665AA:
            *driver = &epd_drv_jd79xxx;
            *ic = DRV_IC_JD79665;
            return true;
        case EPD_SCREEN_DRIVER_JD79668AA:
            *driver = &epd_drv_jd79xxx;
            *ic = panel_recipe == INKTAG_RECIPE_JD79668AA_ZKC42B_FACTORY_B3
                ? DRV_IC_JD79XXX_ZKC : DRV_IC_JD79668;
            return true;
        case EPD_SCREEN_DRIVER_JD79661:
            *driver = &epd_drv_jd79xxx;
            *ic = DRV_IC_JD79XXX_ZKC;
            return true;
        case EPD_SCREEN_DRIVER_JD79XXX:
            *driver = &epd_drv_jd79xxx;
            *ic = DRV_IC_JD79665;
            return true;
#endif
        default:
            return false;
    }
}

epd_model_t *epd_find(const epd_screen_config_t *config) {
    epd_screen_t resolved;
    const epd_driver_t *driver = NULL;
    epd_drv_ic_t ic = 0;

    if (epd_screen_resolve(config, &resolved) != EPD_SCREEN_STATUS_OK ||
        !resolve_driver(config->driver_id, config->panel_recipe, &driver, &ic)) {
        return NULL;
    }

    memset(&s_runtime_model, 0, sizeof(s_runtime_model));
    s_runtime_model.driver_id = resolved.config.driver_id;
    s_runtime_model.panel_recipe = resolved.config.panel_recipe;
    s_runtime_model.color = (epd_color_t)resolved.config.color_id;
    s_runtime_model.drv = driver;
    s_runtime_model.ic = ic;
    s_runtime_model.transfer_format = resolved.transfer_format;
    s_runtime_model.transform = resolved.config.transform;
    s_runtime_model.width = resolved.config.width;
    s_runtime_model.height = resolved.config.height;
    s_runtime_model.visible_x = resolved.config.visible_x;
    s_runtime_model.visible_y = resolved.config.visible_y;
    s_runtime_model.visible_width = resolved.config.visible_width;
    s_runtime_model.visible_height = resolved.config.visible_height;
    return &s_runtime_model;
}

epd_model_t *epd_init(const epd_screen_config_t *config) {
    epd_model_t *epd = epd_find(config);
    if (epd != NULL) epd->drv->init(epd);
    return epd;
}

uint8_t epd_runtime_driver_id(void) { return s_runtime_model.driver_id; }
