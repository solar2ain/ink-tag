#ifndef INKTAG_NRF_IO_CONFIG_H
#define INKTAG_NRF_IO_CONFIG_H
#include "inktag_epd_screen.h"
#include "inktag_pin_config.h"
#include "inktag_aux_io.h"
#include "inktag_release_v1.h"
#include "inktag_settings_journal.h"

typedef struct {
    uint8_t pins[INKTAG_PIN_CONFIG_SIZE];
    uint8_t aux[INKTAG_AUX_IO_WIRE_SIZE];
} inktag_nrf_io_config_t;

void inktag_nrf_io_defaults(inktag_nrf_io_config_t *out);
inktag_settings_journal_read_result_t inktag_nrf_io_settings_read(inktag_nrf_io_config_t *out);
bool inktag_nrf_io_settings_write(const inktag_nrf_io_config_t *config);
bool inktag_nrf_io_valid(const inktag_nrf_io_config_t *config);
/* Pure recipe derivation; unknown leaves the complete snapshot untouched. */
bool inktag_nrf_io_derive(const epd_screen_config_t *screen,
                         inktag_nrf_io_config_t *config);
/* Trusted prepared candidate, RAM only. Independent IO remains available when
 * the screen is unknown; BUSY stays an opaque snapshot and panels refuse. */
void inktag_nrf_io_publish(const inktag_nrf_io_config_t *config,
                          const epd_screen_config_t *screen);
void inktag_nrf_io_invalidate(void);
const inktag_nrf_io_config_t *inktag_nrf_io_current(void);
bool inktag_nrf_io_busy_known(void);
bool inktag_nrf_io_read_busy(bool raw);
bool inktag_nrf_io_enable_low(void);
#endif
