#ifndef INKTAG_GR_AUX_IO_H
#define INKTAG_GR_AUX_IO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "inktag_input.h"
#include "inktag_aux_io.h"
#include "inktag_board.h"

#if INKTAG_AUX_RESERVED_ENABLED
#error "Reserved AUX fields have no implementation"
#endif

/* A missing stock route is not authorization to select a candidate GPIO. */
#ifndef INKTAG_BOARD_AUX_BUTTON_GPIO
#define INKTAG_BOARD_AUX_BUTTON_GPIO 255u
#define INKTAG_BOARD_AUX_BUTTON_PIN_MASK 0u
#endif
#define INKTAG_GR_AUX_GPIO_UNUSED 255u
#define INKTAG_GR_AUX_BUTTON_GPIO INKTAG_BOARD_AUX_BUTTON_GPIO

#if INKTAG_BUILD_PRO && defined(INKTAG_GOODIX_GR5515) && \
    !defined(INKTAG_GR5513_INTERNAL_IMAGES)
#define INKTAG_GR_BUTTON_IRQ_ENABLED 1
bool inktag_gr_aux_io_take_button_irq(void);
bool inktag_gr_aux_io_gesture_active(void);
#endif

typedef struct __attribute__((packed)) {
    uint8_t button_gpio;
    uint8_t button_flags;
    uint8_t reserved[3];
    uint16_t reserved_word;
} inktag_gr_aux_io_config_t;

_Static_assert(sizeof(inktag_gr_aux_io_config_t) == 7u &&
               offsetof(inktag_gr_aux_io_config_t, reserved) == 2u &&
               offsetof(inktag_gr_aux_io_config_t, reserved_word) == 5u,
               "GR AUX ABI must preserve the button and reserved field offsets");

/* Existing qualified PCB exposes one physical button. The shared three-slot
 * wire is used unchanged; unused slots cannot authorize new GPIO backends. */
bool inktag_gr_aux_io_decode(const uint8_t *wire, uint16_t length,
                            inktag_gr_aux_io_config_t *out);
bool inktag_gr_aux_io_encode(const inktag_gr_aux_io_config_t *config,
                            uint8_t wire[INKTAG_AUX_IO_WIRE_SIZE]);

void inktag_gr_aux_io_init(bool board_supported);
bool inktag_gr_aux_io_supported(void);
bool inktag_gr_aux_io_busy(void);
bool inktag_gr_aux_io_reset(void);
void inktag_gr_aux_io_process(uint32_t now_ms);
void inktag_gr_aux_io_get(inktag_gr_aux_io_config_t *config);
bool inktag_gr_aux_io_set(const inktag_gr_aux_io_config_t *config,
                          const char **error);
bool inktag_gr_aux_io_take_button(inktag_button_gesture_t *gesture);
bool inktag_gr_aux_io_simulate_button(inktag_button_gesture_t gesture);

#endif
