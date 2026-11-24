#ifndef INKTAG_AUX_IO_H
#define INKTAG_AUX_IO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "inktag_release_v1.h"

/* Reserved AUX bytes retain their persisted/wire positions without a backend. */
#ifndef INKTAG_AUX_RESERVED_ENABLED
#define INKTAG_AUX_RESERVED_ENABLED 0
#endif
#if INKTAG_AUX_RESERVED_ENABLED != 0
#error "Reserved AUX output has no implementation"
#endif

#define INKTAG_AUX_IO_VERSION INKTAG_RELEASE_AUX_VERSION
#define INKTAG_AUX_IO_WIRE_SIZE 12u
#define INKTAG_AUX_BUTTON_COUNT 3u
#define INKTAG_AUX_PIN_UNUSED 255u

typedef struct {
    uint8_t gpio;
    uint8_t flags; /* bit0 active-low, bits1..2 pull: none/up/down = 0/1/2. */
} inktag_aux_button_t;

typedef struct {
    inktag_aux_button_t buttons[INKTAG_AUX_BUTTON_COUNT];
    uint8_t reserved[3];
    uint16_t reserved_word;
} inktag_aux_io_t;

/* Structural validation only. Vendor must also validate mux, protected pins,
 * existing screen/LED/Flash/NFC routes and qualified runtime backend availability.
 * GPIO assignment never implies permission to drive an arbitrary physical pin. */
bool inktag_aux_io_decode(const uint8_t *wire, size_t length, inktag_aux_io_t *out);
size_t inktag_aux_io_encode(const inktag_aux_io_t *config, uint8_t *wire, size_t capacity);
bool inktag_aux_io_validate(const inktag_aux_io_t *config);

#endif
