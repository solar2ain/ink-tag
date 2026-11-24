#ifndef INKTAG_NRF_PANEL_TEMPERATURE_H
#define INKTAG_NRF_PANEL_TEMPERATURE_H
#include "inktag_epd_driver.h"
bool inktag_nrf_panel_temperature_requires_awake(const epd_model_t *epd);
bool inktag_nrf_panel_temperature_sample(epd_model_t *epd, int16_t *sixteenths);
bool inktag_nrf_panel_temperature_probe(epd_model_t *epd, int16_t *sixteenths);
#endif
