#ifndef INKTAG_NRF_RECORD_STORE_H
#define INKTAG_NRF_RECORD_STORE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    INKTAG_NRF_RECORD_ACTIVATION = 1,
    /* 2 is retired: public Find uses fresh atomic publication, not this
     * activation/health completion mailbox. Never renumber existing kinds. */
    INKTAG_NRF_RECORD_HEALTH = 3,
} inktag_nrf_record_kind_t;

typedef struct {
    inktag_nrf_record_kind_t kind;
    bool success;
    bool deleted;
} inktag_nrf_record_event_t;

bool inktag_nrf_record_store_init(void);
bool inktag_nrf_record_store_read(inktag_nrf_record_kind_t kind,
                                  void *output, uint16_t capacity,
                                  uint16_t *length);
bool inktag_nrf_record_store_write(inktag_nrf_record_kind_t kind,
                                   const void *data, uint16_t length);
bool inktag_nrf_record_store_delete(inktag_nrf_record_kind_t kind);
bool inktag_nrf_record_store_pending(inktag_nrf_record_kind_t kind);
bool inktag_nrf_record_store_take_event(inktag_nrf_record_event_t *event);
uint32_t inktag_nrf_record_store_last_error(void);

#endif
