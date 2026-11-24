#ifndef INKTAG_NRF_FACTORY_DEFAULTS_H
#define INKTAG_NRF_FACTORY_DEFAULTS_H
#include "inktag_epd_config.h"
#include "inktag_nrf_io_config.h"
#include "inktag_settings_journal.h"
#include "inktag_release_v1.h"
/* Full-only ID1 NFP1: magic4/routing16/AUX12/screen17/FNV4. */
#define INKTAG_NRF_FACTORY_PROFILE_SIZE INKTAG_RELEASE_NRF_FACTORY_BYTES
bool inktag_nrf_factory_io(inktag_nrf_io_config_t *io);
inktag_settings_journal_read_result_t inktag_nrf_factory_screen_read(epd_screen_config_t *screen);
inktag_settings_journal_read_result_t inktag_nrf_screen_read(epd_screen_config_t *screen);
bool inktag_nrf_boot_screen(epd_screen_config_t *screen);
void inktag_nrf_io_project(const inktag_nrf_io_config_t *io, epd_config_t *pins);
bool inktag_nrf_board_profile_read(epd_config_t *config);
bool inktag_nrf_board_profile_write(const epd_config_t *config);
bool inktag_nrf_factory_screen(epd_screen_config_t *screen);
#endif
