#ifndef INKTAG_TLSR_AUX_IO_H
#define INKTAG_TLSR_AUX_IO_H

#include <stdbool.h>
#include <stdint.h>

#include "inktag_tlsr_build_profile.h"
#include "inktag_input.h"
#include "inktag_aux_io.h"

/* V2 carries an eight-bit, platform-local GPIO number.  Telink encodes it as
 * bank * 8 + bit (PA0=0, PB0=8, PC0=16, PD0=24).  255 always means disabled. */
#define INKTAG_TLSR_AUX_GPIO_UNUSED 255u

typedef inktag_aux_io_t inktag_tlsr_aux_io_config_t;

typedef struct {
    uint8_t button_gpio;
    uint8_t reserved[3];
    bool qualified;
} inktag_tlsr_aux_io_route_t;

void inktag_tlsr_aux_io_defaults(inktag_tlsr_aux_io_config_t *config);
void inktag_tlsr_aux_io_init(const inktag_tlsr_aux_io_route_t *route,
                             const inktag_tlsr_aux_io_config_t *config);
bool inktag_tlsr_aux_io_supported(void);
bool inktag_tlsr_aux_io_validate(const inktag_tlsr_aux_io_config_t *config,
                                 const char **error);
bool inktag_tlsr_aux_io_apply(const inktag_tlsr_aux_io_config_t *config,
                              const char **error);
void inktag_tlsr_aux_io_process(uint32_t now_ms);
void inktag_tlsr_aux_io_resume(void);
void inktag_tlsr_aux_io_park(void);
#if INKTAG_BUILD_PRO
void inktag_tlsr_aux_io_disable(void);
#endif
bool inktag_tlsr_aux_io_take_button(inktag_button_gesture_t *gesture);
bool inktag_tlsr_aux_io_simulate_button(inktag_button_gesture_t gesture);
/* Pure route admission from the effective port backend; never touches GPIO. */
bool inktag_tlsr_aux_gpio_allowed(uint8_t pin);
bool inktag_tlsr_aux_config_allowed(const inktag_tlsr_aux_io_config_t *config);
bool inktag_tlsr_aux_backend_ready(void);

#endif
