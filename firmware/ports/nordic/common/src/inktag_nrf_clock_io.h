#ifndef INKTAG_NRF_CLOCK_IO_H
#define INKTAG_NRF_CLOCK_IO_H
#include <stdbool.h>
#include <stdint.h>

/* Private display-owner bridge; not part of the shared public EPD ABI. */
bool EPD_NrfClockRead(uint8_t command,uint16_t skip,uint8_t *value,uint8_t length);
/* Defer only the legacy refresh light until the actual full/partial decision. */
void EPD_NrfClockGPIOInit(void);
#endif
