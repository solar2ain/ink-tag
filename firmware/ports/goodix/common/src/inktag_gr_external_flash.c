#include "inktag_gr_external_flash.h"

#include <string.h>

#if defined(INKTAG_GOODIX_GR5515) && !defined(INKTAG_GR5513_INTERNAL_IMAGES)
#include "app_io.h"
#include "gr55xx_sys.h"
#include "inktag_gr_health.h"
#include "inktag_spi_nor_sfdp.h"

#define EXTERNAL_FLASH_IO_MUX APP_IO_MUX_7
#define EXTERNAL_FLASH_LEGACY_JEDEC_ID 0xEB6013u
#define EXTERNAL_FLASH_CAPACITY 0x00080000u
#define EXTERNAL_FLASH_PAGE_SIZE 256u
#define EXTERNAL_FLASH_SECTOR_SIZE 4096u

static uint32_t s_nor_id;
static uint16_t s_program_chunk;

typedef struct {
    uint32_t capacity_bytes;
    uint16_t program_chunk;
} flash_geometry_t;

typedef struct {
    app_io_type_t type;
    uint8_t pin;
} soft_gpio_t;

typedef struct {
    soft_gpio_t clk;
    soft_gpio_t mosi;
    soft_gpio_t miso;
    soft_gpio_t cs;
} soft_spi_bus_t;

static uint32_t pin_mask(uint8_t pin) {
    return 1u << pin;
}

static uint16_t pin_init(
    const soft_gpio_t *gpio,
    app_io_mode_t mode,
    app_io_pull_t pull) {
    app_io_init_t init;
    memset(&init, 0, sizeof(init));
    init.pin = pin_mask(gpio->pin);
    init.mux = EXTERNAL_FLASH_IO_MUX;
    init.mode = mode;
    init.pull = pull;
    return app_io_init(gpio->type, &init);
}

static void pin_write(const soft_gpio_t *gpio, app_io_pin_state_t state) {
    app_io_write_pin(gpio->type, pin_mask(gpio->pin), state);
}

static uint8_t pin_read(const soft_gpio_t *gpio) {
    return app_io_read_pin(gpio->type, pin_mask(gpio->pin)) == APP_IO_PIN_SET;
}

static void pin_deinit(const soft_gpio_t *gpio) {
    app_io_deinit(gpio->type, pin_mask(gpio->pin));
}

static uint8_t spi_byte(const soft_spi_bus_t *bus, uint8_t output) {
    uint8_t input = 0;
    for (uint8_t bit = 0; bit < 8u; bit++) {
        pin_write(&bus->clk, APP_IO_PIN_RESET);
        pin_write(&bus->mosi, (output & 0x80u) ? APP_IO_PIN_SET : APP_IO_PIN_RESET);
        pin_write(&bus->clk, APP_IO_PIN_SET);
        input = (uint8_t)((input << 1) | pin_read(&bus->miso));
        output <<= 1;
    }
    pin_write(&bus->clk, APP_IO_PIN_RESET);
    return input;
}

static void spi_select(const soft_spi_bus_t *bus, bool selected) {
    pin_write(&bus->cs, selected ? APP_IO_PIN_RESET : APP_IO_PIN_SET);
    sys_delay_us(1u);
}

static const soft_spi_bus_t s_td40_bus = {
    .clk = {.type = APP_IO_TYPE_NORMAL, .pin = 25u},
    .mosi = {.type = APP_IO_TYPE_NORMAL, .pin = 16u},
    .miso = {.type = APP_IO_TYPE_AON, .pin = 0u},
    .cs = {.type = APP_IO_TYPE_AON, .pin = 2u},
};

static bool package_has_td40(void) {
    package_type_t package = PACKAGE_GR5515IGND;
    (void)sys_device_package_get(&package);
    return package == PACKAGE_GR5513BEND || package == PACKAGE_GR5513BENDU;
}

static bool flash_bus_open(const soft_spi_bus_t *bus) {
    bool cs_ready = pin_init(&bus->cs, APP_IO_MODE_OUTPUT, APP_IO_PULLUP) ==
                    APP_DRV_SUCCESS;
    bool clk_ready = cs_ready &&
                     pin_init(&bus->clk, APP_IO_MODE_OUTPUT, APP_IO_PULLDOWN) ==
                         APP_DRV_SUCCESS;
    bool mosi_ready = clk_ready &&
                      pin_init(&bus->mosi, APP_IO_MODE_OUTPUT, APP_IO_PULLUP) ==
                          APP_DRV_SUCCESS;
    bool miso_ready = mosi_ready &&
                      pin_init(&bus->miso, APP_IO_MODE_INPUT, APP_IO_PULLUP) ==
                          APP_DRV_SUCCESS;
    if (!miso_ready) {
        if (mosi_ready) pin_deinit(&bus->mosi);
        if (clk_ready) pin_deinit(&bus->clk);
        if (cs_ready) pin_deinit(&bus->cs);
        return false;
    }

    pin_write(&bus->cs, APP_IO_PIN_SET);
    pin_write(&bus->clk, APP_IO_PIN_RESET);
    pin_write(&bus->mosi, APP_IO_PIN_SET);
    spi_select(bus, true);
    (void)spi_byte(bus, 0xABu);
    (void)spi_byte(bus, 0u);
    (void)spi_byte(bus, 0u);
    (void)spi_byte(bus, 0u);
    (void)spi_byte(bus, 0u);
    spi_select(bus, false);
    sys_delay_us(50u);
    return true;
}

static void flash_bus_close(const soft_spi_bus_t *bus) {
    /* TH25D-40HA-compatible parts power up in standby and only enter their
     * microamp state after Deep Power-down. Every open issues 0xAB and waits
     * 50 us, so pair it with 0xB9 after the last read/program/erase completes
     * instead of leaving the GR5513 TD40 awake between product operations. */
    spi_select(bus, true);
    (void)spi_byte(bus, 0xB9u);
    spi_select(bus, false);
    sys_delay_us(3u);
    /* Do not hand these fitted TD40 nets back to the SDK's generic reset
     * state.  On GR5513 ZKC42VM that state leaves a measurable pad-current
     * path after an array read.  Park the outputs at the serial-NOR idle
     * levels and remove the MISO pull once the DPD entry delay has elapsed. */
    pin_write(&bus->cs, APP_IO_PIN_SET);
    pin_write(&bus->clk, APP_IO_PIN_RESET);
    pin_write(&bus->mosi, APP_IO_PIN_SET);
    (void)pin_init(&bus->miso, APP_IO_MODE_INPUT, APP_IO_NOPULL);
}

static void flash_send_address(const soft_spi_bus_t *bus, uint32_t address) {
    (void)spi_byte(bus, (uint8_t)(address >> 16));
    (void)spi_byte(bus, (uint8_t)(address >> 8));
    (void)spi_byte(bus, (uint8_t)address);
}

static uint32_t flash_read_jedec_id(const soft_spi_bus_t *bus) {
    spi_select(bus, true);
    (void)spi_byte(bus, 0x9Fu);
    uint32_t id = (uint32_t)spi_byte(bus, 0xFFu) << 16;
    id |= (uint32_t)spi_byte(bus, 0xFFu) << 8;
    id |= spi_byte(bus, 0xFFu);
    spi_select(bus, false);
    return id;
}

static void flash_read_sfdp(const soft_spi_bus_t *bus, uint32_t address,
                            uint8_t *data, uint8_t length) {
    spi_select(bus, true);
    (void)spi_byte(bus, 0x5au);
    flash_send_address(bus, address);
    (void)spi_byte(bus, 0u);
    for (uint8_t i = 0; i < length; ++i) data[i] = spi_byte(bus, 0xffu);
    spi_select(bus, false);
}

static bool flash_geometry(const soft_spi_bus_t *bus, uint32_t id,
                            flash_geometry_t *out) {
    uint8_t header[16];
    flash_read_sfdp(bus, 0, header, sizeof(header));
    uint32_t address = 0;
    uint8_t length = inktag_spi_nor_bfpt_location(header, &address);
    if (length) {
        uint8_t table[INKTAG_SPI_NOR_BFPT_MAX];
        flash_read_sfdp(bus, address, table, length);
        return inktag_spi_nor_bfpt_geometry(table, length,
            EXTERNAL_FLASH_CAPACITY, &out->capacity_bytes, &out->program_chunk);
    }
    /* Preserve the physically qualified pre-SFDP path for the original part,
     * not a fallback for unknown IDs or contradictory valid SFDP tables. */
    if (id != EXTERNAL_FLASH_LEGACY_JEDEC_ID || !memcmp(header, "SFDP", 4))
        return false;
    out->capacity_bytes = EXTERNAL_FLASH_CAPACITY;
    out->program_chunk = EXTERNAL_FLASH_PAGE_SIZE;
    return true;
}

static uint8_t flash_read_status(const soft_spi_bus_t *bus) {
    spi_select(bus, true);
    (void)spi_byte(bus, 0x05u);
    uint8_t status = spi_byte(bus, 0xFFu);
    spi_select(bus, false);
    return status;
}

static bool flash_wait_ready(const soft_spi_bus_t *bus, uint32_t timeout_ms) {
    for (uint32_t elapsed = 0; elapsed < timeout_ms; elapsed++) {
        if ((flash_read_status(bus) & 0x01u) == 0u) return true;
        sys_delay_ms(1u);
    }
    return false;
}

static bool flash_write_enable(const soft_spi_bus_t *bus) {
    spi_select(bus, true);
    (void)spi_byte(bus, 0x06u);
    spi_select(bus, false);
    return (flash_read_status(bus) & 0x02u) != 0u;
}


bool inktag_gr_external_flash_available(void) {
    s_nor_id = 0u;
    s_program_chunk = 0u;
    if (!package_has_td40() || !flash_bus_open(&s_td40_bus)) return false;
    uint32_t id = flash_read_jedec_id(&s_td40_bus);
    flash_geometry_t geometry = {0};
    bool valid = id != 0u && id != 0xffffffu &&
        flash_read_jedec_id(&s_td40_bus) == id &&
        flash_geometry(&s_td40_bus, id, &geometry);
    s_nor_id = valid ? id : 0u;
    s_program_chunk = valid ? geometry.program_chunk : 0u;
    flash_bus_close(&s_td40_bus);
    return valid;
}

uint32_t inktag_gr_external_flash_capacity(void) {
    return inktag_gr_external_flash_available() ? EXTERNAL_FLASH_CAPACITY : 0u;
}

uint32_t inktag_gr_external_flash_read(
    uint32_t offset, uint8_t *data, uint32_t length) {
    if (!data || length == 0u || offset > EXTERNAL_FLASH_CAPACITY ||
        length > EXTERNAL_FLASH_CAPACITY - offset || !package_has_td40() ||
        !flash_bus_open(&s_td40_bus)) {
        return 0u;
    }
    if (!s_program_chunk || flash_read_jedec_id(&s_td40_bus) != s_nor_id) {
        flash_bus_close(&s_td40_bus);
        return 0u;
    }

    spi_select(&s_td40_bus, true);
    (void)spi_byte(&s_td40_bus, 0x03u);
    flash_send_address(&s_td40_bus, offset);
    for (uint32_t i = 0; i < length; i++) data[i] = spi_byte(&s_td40_bus, 0xFFu);
    spi_select(&s_td40_bus, false);
    flash_bus_close(&s_td40_bus);
    return length;
}

uint32_t inktag_gr_external_flash_write(
    uint32_t offset, const uint8_t *data, uint32_t length) {
    if (!data || length == 0u || offset > EXTERNAL_FLASH_CAPACITY ||
        length > EXTERNAL_FLASH_CAPACITY - offset || !package_has_td40() ||
        !flash_bus_open(&s_td40_bus)) {
        return 0u;
    }
    if (!s_program_chunk || flash_read_jedec_id(&s_td40_bus) != s_nor_id) {
        flash_bus_close(&s_td40_bus);
        return 0u;
    }

    uint32_t written = 0u;
    while (written < length) {
        uint32_t page_remaining = s_program_chunk -
                                  ((offset + written) % s_program_chunk);
        uint32_t chunk = length - written;
        if (chunk > page_remaining) chunk = page_remaining;
        if (!inktag_gr_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH,
                                                 true)) break;
        if (!flash_write_enable(&s_td40_bus)) break;
        spi_select(&s_td40_bus, true);
        (void)spi_byte(&s_td40_bus, 0x02u);
        flash_send_address(&s_td40_bus, offset + written);
        for (uint32_t i = 0; i < chunk; i++) {
            (void)spi_byte(&s_td40_bus, data[written + i]);
        }
        spi_select(&s_td40_bus, false);
        if (!flash_wait_ready(&s_td40_bus, 20u)) break;
        written += chunk;
    }
    flash_bus_close(&s_td40_bus);
    return written;
}

bool inktag_gr_external_flash_erase(uint32_t offset, uint32_t length) {
    if (length == 0u || (offset % EXTERNAL_FLASH_SECTOR_SIZE) != 0u ||
        (length % EXTERNAL_FLASH_SECTOR_SIZE) != 0u ||
        offset > EXTERNAL_FLASH_CAPACITY || length > EXTERNAL_FLASH_CAPACITY - offset ||
        !package_has_td40() || !flash_bus_open(&s_td40_bus)) {
        return false;
    }
    if (!s_program_chunk || flash_read_jedec_id(&s_td40_bus) != s_nor_id) {
        flash_bus_close(&s_td40_bus);
        return false;
    }

    bool ok = true;
    for (uint32_t erased = 0u; erased < length; erased += EXTERNAL_FLASH_SECTOR_SIZE) {
        if (!inktag_gr_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH,
                                                 true)) {
            ok = false;
            break;
        }
        if (!flash_write_enable(&s_td40_bus)) {
            ok = false;
            break;
        }
        spi_select(&s_td40_bus, true);
        (void)spi_byte(&s_td40_bus, 0x20u);
        flash_send_address(&s_td40_bus, offset + erased);
        spi_select(&s_td40_bus, false);
        if (!flash_wait_ready(&s_td40_bus, 1000u)) {
            ok = false;
            break;
        }
    }
    flash_bus_close(&s_td40_bus);
    return ok;
}

#else


bool inktag_gr_external_flash_available(void) {
    return false;
}

uint32_t inktag_gr_external_flash_capacity(void) {
    return 0u;
}

uint32_t inktag_gr_external_flash_read(
    uint32_t offset, uint8_t *data, uint32_t length) {
    (void)offset;
    (void)data;
    (void)length;
    return 0u;
}

uint32_t inktag_gr_external_flash_write(
    uint32_t offset, const uint8_t *data, uint32_t length) {
    (void)offset;
    (void)data;
    (void)length;
    return 0u;
}

bool inktag_gr_external_flash_erase(uint32_t offset, uint32_t length) {
    (void)offset;
    (void)length;
    return false;
}

#endif
