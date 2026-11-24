#ifndef INKTAG_TLSR_USER_FONT_STORAGE_H
#define INKTAG_TLSR_USER_FONT_STORAGE_H
#include <stdbool.h>
#include <stdint.h>
/* Vendor IO only. Shared admission plus the native adapter own AUTH/Pro,
 * render exclusion and header-last commit. Offset zero is the single32KiB
 * user region, never the retired A/B banks. */
bool inktag_tlsr_user_font_read(uint32_t offset, uint8_t *data, uint32_t length);
bool inktag_tlsr_user_font_program(uint32_t offset, const uint8_t *data, uint32_t length);
bool inktag_tlsr_user_font_erase(void);
#endif
