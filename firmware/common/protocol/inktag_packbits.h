#ifndef INKTAG_PACKBITS_H
#define INKTAG_PACKBITS_H

#include <stdbool.h>
#include <stdint.h>

typedef bool (*inktag_packbits_sink_t)(void *context,
                                      uint8_t *decoded,
                                      uint16_t decoded_length);

/* Validate and stream one independently framed PackBits block. Repeat
 * controls encode 3..130 copies in bit-7 form; literal controls encode
 * 1..128 following bytes. Validation consumes the complete encoded block and
 * must prove the exact caller-declared decoded length before any sink-visible
 * mutation occurs. Streaming then uses at most one 130-byte decoded buffer, so
 * a BLE-sized encoded block may represent up to 65535 decoded bytes. */
bool inktag_packbits_validate_block(const uint8_t *source,
                                    uint8_t source_length,
                                    uint16_t decoded_length);
bool inktag_packbits_stream_block(const uint8_t *source,
                                  uint8_t source_length,
                                  uint16_t decoded_length,
                                  inktag_packbits_sink_t sink,
                                  void *context);

#endif
