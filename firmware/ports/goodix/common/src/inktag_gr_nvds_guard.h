#ifndef INKTAG_GR_NVDS_GUARD_H
#define INKTAG_GR_NVDS_GUARD_H

#include <stdbool.h>
#include <stdint.h>

uint8_t inktag_gr_nvds_init(uint32_t start, uint8_t sectors);
bool inktag_gr_nvds_ready(void);
/* Independent of, and ANDed with, the existing physical voltage gate.
 * Erases include every sector actually touched, not only the requested bytes. */
bool inktag_gr_nvds_mutation_allowed(uint32_t address, uint32_t size, bool erase);

#endif
