#ifndef INKTAG_OM_EXTERNAL_FLASH_H
#define INKTAG_OM_EXTERNAL_FLASH_H

#include <stdbool.h>
#include <stdint.h>

#include "inktag_om_external_flash_board.h"

#define INKTAG_OM_EXTERNAL_FLASH_JEDEC_ID INKTAG_BOARD_EXTERNAL_FLASH_JEDEC_ID
#define INKTAG_OM_EXTERNAL_FLASH_CAPACITY INKTAG_BOARD_EXTERNAL_FLASH_CAPACITY
#define INKTAG_OM_EXTERNAL_FLASH_STORAGE_OFFSET \
    INKTAG_BOARD_EXTERNAL_FLASH_STORAGE_OFFSET
#define INKTAG_OM_EXTERNAL_FLASH_PAGE_SIZE 256u
#define INKTAG_OM_EXTERNAL_FLASH_SECTOR_SIZE 4096u

typedef struct {
    uint32_t jedec_id;
    uint8_t sfdp[8];
    uint8_t miso_pin;
    uint8_t mosi_pin;
    uint8_t cs_pin;
    uint8_t sclk_pin;
    int32_t init_status;
    int32_t transfer_status;
    bool board_candidate;
    bool matched;
} inktag_om_external_flash_probe_t;

/* The shared OM application must never infer the optional data Flash from the
 * MCU family. Only the persisted exact-board selector may open this bus. */
void inktag_om_external_flash_init(void);
bool inktag_om_external_flash_probe(inktag_om_external_flash_probe_t *result);
bool inktag_om_external_flash_available(void);
uint32_t inktag_om_external_flash_capacity(void);
uint32_t inktag_om_external_flash_storage_capacity(void);
uint32_t inktag_om_external_flash_read(
    uint32_t offset, uint8_t *data, uint32_t length);
uint32_t inktag_om_external_flash_write(
    uint32_t offset, const uint8_t *data, uint32_t length);
bool inktag_om_external_flash_erase(uint32_t offset, uint32_t length);

#endif
