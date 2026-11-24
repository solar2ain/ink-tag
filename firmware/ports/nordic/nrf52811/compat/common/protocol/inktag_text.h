#ifndef INKTAG_TEXT_H
#define INKTAG_TEXT_H

#include <stdbool.h>
#include <stdint.h>

/* Strict UTF-8 text without ASCII controls. Zero codepoint limit is unlimited;
 * empty text is valid, with field-specific length/empty rules in the caller. */
bool inktag_text_utf8_valid(const uint8_t *text, uint8_t length,
                           uint8_t maximum_codepoints);

#endif
