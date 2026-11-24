#include "inktag_om_board_identity.h"

#include <string.h>

#include "inktag_epd_config.h"
#include "nvds.h"
#include "inktag_board.h"
#include "inktag_om_pin_layout.h"

#define INKTAG_BOARD_NVDS_TAG ((nvds_tag_id_t)(NVDS_TAG_USER_BEGIN + 8))
#define INKTAG_BOARD_RECORD_MAGIC 0x3144424Fu /* OBD1 */
#define INKTAG_BOARD_RECORD_VERSION 1u
#define INKTAG_FACTORY_SELECTOR_VERSION 1u

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t version;
    uint8_t board;
    uint8_t reserved;
    uint8_t checksum;
} inktag_om_board_record_t;

typedef struct __attribute__((packed)) {
    uint8_t magic[8];
    uint8_t version;
    uint8_t board;
    uint8_t guard_a;
    uint8_t guard_b;
    uint32_t checksum;
} inktag_om_factory_selector_t;

/* Offline Full patches this native envelope. Ordinary boot never gains
 * authority to create or replace persisted factory data. */
static const inktag_om_factory_selector_t s_factory_selector
    __attribute__((used, section(".inktag_factory_selector"))) = {
        .magic = {'O', 'M', 'B', 'D', 'S', 'E', 'L', '1'},
        .version = INKTAG_FACTORY_SELECTOR_VERSION,
        .board = 0xFFu,
        .guard_a = 0xA5u,
        .guard_b = 0x5Au,
        .checksum = 0x001DF79Fu,
    };

static inktag_om_board_t s_board = INKTAG_OM_BOARD_UNKNOWN;

static uint8_t record_checksum(const inktag_om_board_record_t *record) {
    const uint8_t *bytes = (const uint8_t *)record;
    uint8_t checksum = 0u;
    for (uint8_t i = 0u; i + 1u < sizeof(*record); i++) checksum ^= bytes[i];
    return checksum;
}

static uint32_t selector_checksum(const inktag_om_factory_selector_t *selector) {
    const uint8_t *bytes = (const uint8_t *)selector;
    uint32_t hash = 2166136261u;
    for (uint8_t i = 0u; i < 12u; i++) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

static bool board_valid(uint8_t board) {
    return board == INKTAG_OM_BOARD_ZKC21B ||
           board == INKTAG_OM_BOARD_ZKC42B ||
           board == INKTAG_OM_BOARD_ZKC21V;
}

static void factory_selector_read(inktag_om_factory_selector_t *selector) {
    const volatile uint8_t *source =
        (const volatile uint8_t *)(uintptr_t)&s_factory_selector;
    uint8_t *destination = (uint8_t *)selector;
    for (uint8_t i = 0u; i < sizeof(*selector); i++) destination[i] = source[i];
}

static bool board_load(inktag_om_board_t *board) {
    uint8_t raw[INKTAG_OM_PIN_FACTORY_SIZE], id;
    inktag_pin_config_t pins;
    nvds_tag_len_t n=sizeof(raw);
    if(nvds_get(INKTAG_BOARD_NVDS_TAG,&n,raw)!=NVDS_OK)return false;
    if(n==sizeof(raw)) {
        if(!inktag_om_pin_factory_decode(raw,n,&id,&pins))return false;
        *board=(inktag_om_board_t)id;return true;
    }
    inktag_om_board_record_t record;
    if(n!=sizeof(record))return false;
    memcpy(&record,raw,sizeof(record));
    if (record.magic != INKTAG_BOARD_RECORD_MAGIC || record.reserved != 0u ||
        record.version != INKTAG_BOARD_RECORD_VERSION || !board_valid(record.board) ||
        record.checksum != record_checksum(&record)) return false;
    *board = (inktag_om_board_t)record.board;
    return true;
}

void inktag_om_board_identity_init(void) {
    inktag_om_factory_selector_t selector;
    s_board = INKTAG_OM_BOARD_UNKNOWN;
    if(!board_load(&s_board))return;
    /* Read post-link bytes, not a compiler-folded initializer. A configured
     * selector constrains seeded identity; it cannot migrate stale NVDS. */
    factory_selector_read(&selector);
    if(memcmp(selector.magic,"OMBDSEL1",8u) ||
       selector.version!=INKTAG_FACTORY_SELECTOR_VERSION ||
       selector.guard_a!=0xA5u || selector.guard_b!=0x5Au ||
       selector.checksum!=selector_checksum(&selector) ||
       (selector.board!=0xFFu &&
        (!board_valid(selector.board) || selector.board!=(uint8_t)s_board)))
        s_board=INKTAG_OM_BOARD_UNKNOWN;
}

inktag_om_board_t inktag_om_board_identity(void) { return s_board; }

const char *inktag_om_board_id(void) {
    if (s_board == INKTAG_OM_BOARD_ZKC21B) return "om6626_zkc21b";
    if (s_board == INKTAG_OM_BOARD_ZKC42B) return "om6626_zkc42b";
    if (s_board == INKTAG_OM_BOARD_ZKC21V) return "om6626_zkc21v";
    return "om6626_zkcxxb";
}

bool inktag_om_board_factory_provisioning_active(void) {
    return false; /* Full seeding is offline, never an App boot migration. */
}

bool inktag_om_board_default_screen(epd_screen_config_t *screen) {
    if (!screen) return false;
    if (s_board == INKTAG_OM_BOARD_ZKC21B) {
        const epd_screen_config_t value = {
            EPD_SCREEN_SCHEMA_VERSION, EPD_SCREEN_DRIVER_JD79661,
            INKTAG_RECIPE_JD79661_ZKC21B_FACTORY_B2,
            EPD_SCREEN_COLOR_BWRY, 9u, 128u, 250u, 0u, 0u, 122u, 250u,
        };
        *screen = value;
        return true;
    }
    if (s_board == INKTAG_OM_BOARD_ZKC21V) {
        const epd_screen_config_t value = {
            EPD_SCREEN_SCHEMA_VERSION, EPD_SCREEN_DRIVER_SSD1680,
            INKTAG_RECIPE_SSD1680_ZKC21V_FACTORY,
            EPD_SCREEN_COLOR_BWR, 9u, 128u, 250u, 0u, 0u, 122u, 250u,
        };
        *screen = value;
        return true;
    }
    if (s_board == INKTAG_OM_BOARD_ZKC42B) {
        const epd_screen_config_t value = {
            EPD_SCREEN_SCHEMA_VERSION, EPD_SCREEN_DRIVER_JD79668AA,
            INKTAG_RECIPE_JD79668AA_ZKC42B_FACTORY_B3,
            EPD_SCREEN_COLOR_BWRY, 6u, 400u, 300u, 0u, 0u, 400u, 300u,
        };
        *screen = value;
        return true;
    }
    memset(screen, 0, sizeof(*screen));
    return false;
}

bool inktag_om_board_requires_default_screen(void) {
    return false;
}
