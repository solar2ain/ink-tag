#include "inktag_om_external_flash.h"

#include <string.h>

#include "om.h"
#include "om_driver.h"
#include "inktag_om_board_identity.h"
#include "inktag_om_health.h"

/* ZKC42B v1.0 wiring. PAD8..PAD11 form one complete SPI0 pinmux group and do
 * not overlap the qualified SPI1 panel bus or I2C0 NFC bus. Availability is
 * still fail-closed on the persisted exact-board selector plus live identity. */
#define EXTERNAL_FLASH_MISO_PIN INKTAG_BOARD_EXTERNAL_FLASH_MISO_PIN
#define EXTERNAL_FLASH_MOSI_PIN INKTAG_BOARD_EXTERNAL_FLASH_MOSI_PIN
#define EXTERNAL_FLASH_CS_PIN   INKTAG_BOARD_EXTERNAL_FLASH_CS_PIN
#define EXTERNAL_FLASH_SCLK_PIN INKTAG_BOARD_EXTERNAL_FLASH_SCLK_PIN
#define EXTERNAL_FLASH_SPI      OM_SPI0
#define EXTERNAL_FLASH_TIMEOUT_MS 1000u
#define EXTERNAL_FLASH_IO_BYTES (4u + INKTAG_OM_EXTERNAL_FLASH_PAGE_SIZE)
#define EXTERNAL_FLASH_PROBE_ATTEMPTS 3u

static uint8_t s_tx[EXTERNAL_FLASH_IO_BYTES];
static uint8_t s_rx[EXTERNAL_FLASH_IO_BYTES];
static int8_t s_available = -1;
static inktag_om_external_flash_probe_t s_probe;

_Static_assert(INKTAG_OM_EXTERNAL_FLASH_STORAGE_OFFSET %
                   INKTAG_OM_EXTERNAL_FLASH_SECTOR_SIZE == 0u,
               "external image-store offset must be sector aligned");
_Static_assert(INKTAG_OM_EXTERNAL_FLASH_STORAGE_OFFSET <
                   INKTAG_OM_EXTERNAL_FLASH_CAPACITY,
               "external image-store offset must leave usable capacity");

static bool board_candidate(void) {
    return inktag_om_board_identity() == INKTAG_OM_BOARD_ZKC42B;
}

static bool bus_open(int32_t *status) {
    const pin_config_t pins[] = {
        {EXTERNAL_FLASH_MISO_PIN, {PINMUX_PAD8_SPI0_DI_CFG},
         PMU_PIN_MODE_PU, PMU_PIN_DRIVER_CURRENT_NORMAL},
        {EXTERNAL_FLASH_MOSI_PIN, {PINMUX_PAD9_SPI0_DO_CFG},
         PMU_PIN_MODE_PU, PMU_PIN_DRIVER_CURRENT_NORMAL},
        {EXTERNAL_FLASH_CS_PIN, {PINMUX_PAD10_SPI0_CS_CFG},
         PMU_PIN_MODE_PU, PMU_PIN_DRIVER_CURRENT_NORMAL},
        {EXTERNAL_FLASH_SCLK_PIN, {PINMUX_PAD11_SPI0_SCK_CFG},
         PMU_PIN_MODE_PD, PMU_PIN_DRIVER_CURRENT_NORMAL},
    };
    spi_config_t config = {
        .freq = 1000000u,
        .mode = SPI_MODE_0,
        .role = SPI_ROLE_MASTER,
        .wire = SPI_WIRE_4,
        .first_bit = SPI_MSB_FIRST,
        .cs_valid = SPI_CS_LOW,
    };
    om_error_t result;

    if (!board_candidate()) {
        if (status) *status = OM_ERROR_PARAMETER;
        return false;
    }
    drv_pin_init(pins, sizeof(pins) / sizeof(pins[0]));
    result = drv_spi_init(EXTERNAL_FLASH_SPI, &config);
    if (result == OM_ERROR_OK) {
        /* The fitted NOR requires /CS high through power-up, then a fresh
         * high-to-low transition before accepting its first command. GR's
         * fitted-NOR bus does the same explicit idle setup. */
        (void)drv_spi_control(EXTERNAL_FLASH_SPI, SPI_CONTROL_CSN_HIGH, NULL);
        drv_dwt_delay_us(60u);
    }
    if (status) *status = result;
    return result == OM_ERROR_OK;
}

static bool transfer(const uint8_t *tx, uint8_t *rx, uint16_t length,
                     int32_t *status);

static void bus_close(void) {
    const uint8_t deep_power_down = 0xB9u;
    const uint32_t input_mask = 1u << EXTERNAL_FLASH_MISO_PIN;
    const uint32_t high_mask = (1u << EXTERNAL_FLASH_MOSI_PIN) |
                               (1u << EXTERNAL_FLASH_CS_PIN);
    const uint32_t low_mask = 1u << EXTERNAL_FLASH_SCLK_PIN;
    const pin_config_t idle_pins[] = {
        {EXTERNAL_FLASH_MISO_PIN, {PINMUX_GPIO_MODE_CFG},
         PMU_PIN_MODE_FLOAT, PMU_PIN_DRIVER_CURRENT_NORMAL},
        {EXTERNAL_FLASH_MOSI_PIN, {PINMUX_GPIO_MODE_CFG},
         PMU_PIN_MODE_PP, PMU_PIN_DRIVER_CURRENT_NORMAL},
        {EXTERNAL_FLASH_CS_PIN, {PINMUX_GPIO_MODE_CFG},
         PMU_PIN_MODE_PP, PMU_PIN_DRIVER_CURRENT_NORMAL},
        {EXTERNAL_FLASH_SCLK_PIN, {PINMUX_GPIO_MODE_CFG},
         PMU_PIN_MODE_PP, PMU_PIN_DRIVER_CURRENT_NORMAL},
    };
    if (s_probe.matched) {
        (void)transfer(&deep_power_down, NULL, 1u, NULL);
    }
    (void)drv_spi_control(EXTERNAL_FLASH_SPI, SPI_CONTROL_CSN_HIGH, NULL);
    if (s_probe.matched) drv_dwt_delay_us(10u);
    (void)drv_spi_control(EXTERNAL_FLASH_SPI, SPI_CONTROL_CLK_DISABLE, NULL);
    /* GR's fitted-NOR regression showed that handing the pins back to
     * generic reset/pulls can create an external pad-current path. Preserve
     * CS/MOSI high, CLK low and MISO as an unpulled input between accesses. */
    drv_gpio_write(OM_GPIO0, high_mask, GPIO_LEVEL_HIGH);
    drv_gpio_write(OM_GPIO0, low_mask, GPIO_LEVEL_LOW);
    drv_gpio_set_dir(OM_GPIO0, high_mask | low_mask, GPIO_DIR_OUTPUT);
    drv_gpio_set_dir(OM_GPIO0, input_mask, GPIO_DIR_INPUT);
    drv_pin_init(idle_pins, sizeof(idle_pins) / sizeof(idle_pins[0]));
}

static bool transfer(const uint8_t *tx, uint8_t *rx, uint16_t length,
                     int32_t *status) {
    om_error_t result = drv_spi_transfer(
        EXTERNAL_FLASH_SPI, tx, length, rx, length,
        EXTERNAL_FLASH_TIMEOUT_MS);
    if (status) *status = result;
    return result == OM_ERROR_OK;
}

static bool wake_flash(int32_t *status) {
    const uint8_t command[5] = {0xABu, 0u, 0u, 0u, 0u};
    uint8_t response[sizeof(command)];
    if (!transfer(command, response, sizeof(command), status)) return false;
    drv_dwt_delay_us(50u);
    return true;
}

static uint32_t read_jedec_id(int32_t *status) {
    const uint8_t command[4] = {0x9Fu, 0xFFu, 0xFFu, 0xFFu};
    uint8_t response[sizeof(command)] = {0};
    if (!transfer(command, response, sizeof(command), status)) return 0u;
    return ((uint32_t)response[1] << 16) |
           ((uint32_t)response[2] << 8) | response[3];
}

static bool read_sfdp(uint8_t sfdp[8], int32_t *status) {
    uint8_t command[13] = {0x5Au, 0u, 0u, 0u, 0u};
    uint8_t response[sizeof(command)] = {0};
    memset(&command[5], 0xFF, sizeof(command) - 5u);
    if (!transfer(command, response, sizeof(command), status)) return false;
    memcpy(sfdp, &response[5], 8u);
    return true;
}

static uint8_t read_status(void) {
    const uint8_t command[2] = {0x05u, 0xFFu};
    uint8_t response[sizeof(command)] = {0};
    return transfer(command, response, sizeof(command), NULL) ? response[1] : 0xFFu;
}

static bool wait_ready(uint32_t timeout_ms) {
    for (uint32_t elapsed = 0u; elapsed < timeout_ms; elapsed++) {
        if ((read_status() & 0x01u) == 0u) return true;
        drv_dwt_delay_ms(1u);
    }
    return false;
}

static bool write_enable(void) {
    const uint8_t command = 0x06u;
    return transfer(&command, NULL, 1u, NULL) &&
           (read_status() & 0x02u) != 0u;
}

void inktag_om_external_flash_init(void) {
    memset(&s_probe, 0, sizeof(s_probe));
    s_probe.miso_pin = EXTERNAL_FLASH_MISO_PIN;
    s_probe.mosi_pin = EXTERNAL_FLASH_MOSI_PIN;
    s_probe.cs_pin = EXTERNAL_FLASH_CS_PIN;
    s_probe.sclk_pin = EXTERNAL_FLASH_SCLK_PIN;
    s_probe.board_candidate = board_candidate();
    s_available = 0;
    if (!s_probe.board_candidate) return;
    /* A fitted NOR can return all 0xff on the first access after cold boot.
     * Do not permanently hide its image slots on that one read.
     * Keep retries bounded and require both JEDEC and SFDP on the same pass. */
    for (uint32_t attempt = 0u; attempt < EXTERNAL_FLASH_PROBE_ATTEMPTS;
         attempt++) {
        if (attempt != 0u) drv_dwt_delay_ms(10u);
        s_probe.jedec_id = 0u;
        memset(s_probe.sfdp, 0, sizeof(s_probe.sfdp));
        s_probe.matched = false;
        if (!bus_open(&s_probe.init_status)) continue;
        if (wake_flash(&s_probe.transfer_status)) {
            s_probe.jedec_id = read_jedec_id(&s_probe.transfer_status);
            (void)read_sfdp(s_probe.sfdp, &s_probe.transfer_status);
        }
        s_probe.matched =
            s_probe.jedec_id == INKTAG_OM_EXTERNAL_FLASH_JEDEC_ID &&
            memcmp(s_probe.sfdp, "SFDP", 4u) == 0;
        bus_close();
        if (s_probe.matched) break;
    }
    s_available = s_probe.matched ? 1 : 0;
}

bool inktag_om_external_flash_probe(inktag_om_external_flash_probe_t *result) {
    if (!result) return false;
    if (s_available < 0) inktag_om_external_flash_init();
    *result = s_probe;
    return s_probe.board_candidate;
}

bool inktag_om_external_flash_available(void) {
    if (s_available < 0) inktag_om_external_flash_init();
    return s_available == 1;
}

uint32_t inktag_om_external_flash_capacity(void) {
    return inktag_om_external_flash_available() ?
           INKTAG_OM_EXTERNAL_FLASH_CAPACITY : 0u;
}

uint32_t inktag_om_external_flash_storage_capacity(void) {
#if defined(INKTAG_OM_DIAGNOSTICS)
    /* Diagnostic images expose only the explicit read-only backup request;
     * image-store mutation stays disabled even after release qualification. */
    return 0u;
#else
    return inktag_om_external_flash_available() ?
           INKTAG_OM_EXTERNAL_FLASH_CAPACITY -
               INKTAG_OM_EXTERNAL_FLASH_STORAGE_OFFSET : 0u;
#endif
}

uint32_t inktag_om_external_flash_read(
    uint32_t offset, uint8_t *data, uint32_t length) {
    uint32_t total = 0u;
    if (!data || length == 0u || offset > INKTAG_OM_EXTERNAL_FLASH_CAPACITY ||
        length > INKTAG_OM_EXTERNAL_FLASH_CAPACITY - offset ||
        !inktag_om_external_flash_available() || !bus_open(NULL) ||
        !wake_flash(NULL)) return 0u;

    while (total < length) {
        uint32_t chunk = length - total;
        if (chunk > INKTAG_OM_EXTERNAL_FLASH_PAGE_SIZE) {
            chunk = INKTAG_OM_EXTERNAL_FLASH_PAGE_SIZE;
        }
        s_tx[0] = 0x03u;
        s_tx[1] = (uint8_t)((offset + total) >> 16);
        s_tx[2] = (uint8_t)((offset + total) >> 8);
        s_tx[3] = (uint8_t)(offset + total);
        memset(&s_tx[4], 0xFF, chunk);
        if (!transfer(s_tx, s_rx, (uint16_t)(4u + chunk), NULL)) break;
        memcpy(&data[total], &s_rx[4], chunk);
        total += chunk;
    }
    bus_close();
    return total;
}

uint32_t inktag_om_external_flash_write(
    uint32_t offset, const uint8_t *data, uint32_t length) {
    uint32_t total = 0u;
    if (!data || length == 0u || offset > INKTAG_OM_EXTERNAL_FLASH_CAPACITY ||
        length > INKTAG_OM_EXTERNAL_FLASH_CAPACITY - offset ||
        !inktag_om_external_flash_available() || !bus_open(NULL) ||
        !wake_flash(NULL)) return 0u;

    while (total < length) {
        uint32_t page_remaining = INKTAG_OM_EXTERNAL_FLASH_PAGE_SIZE -
                                  ((offset + total) % INKTAG_OM_EXTERNAL_FLASH_PAGE_SIZE);
        uint32_t chunk = length - total;
        if (chunk > page_remaining) chunk = page_remaining;
        if (!inktag_om_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH,
                                                 true)) break;
        if (!write_enable()) break;
        s_tx[0] = 0x02u;
        s_tx[1] = (uint8_t)((offset + total) >> 16);
        s_tx[2] = (uint8_t)((offset + total) >> 8);
        s_tx[3] = (uint8_t)(offset + total);
        memcpy(&s_tx[4], &data[total], chunk);
        if (!transfer(s_tx, NULL, (uint16_t)(4u + chunk), NULL) ||
            !wait_ready(20u)) break;
        total += chunk;
    }
    bus_close();
    return total;
}

bool inktag_om_external_flash_erase(uint32_t offset, uint32_t length) {
    bool ok = true;
    if (length == 0u || offset % INKTAG_OM_EXTERNAL_FLASH_SECTOR_SIZE != 0u ||
        length % INKTAG_OM_EXTERNAL_FLASH_SECTOR_SIZE != 0u ||
        offset > INKTAG_OM_EXTERNAL_FLASH_CAPACITY ||
        length > INKTAG_OM_EXTERNAL_FLASH_CAPACITY - offset ||
        !inktag_om_external_flash_available() || !bus_open(NULL) ||
        !wake_flash(NULL)) return false;
    for (uint32_t erased = 0u; erased < length;
         erased += INKTAG_OM_EXTERNAL_FLASH_SECTOR_SIZE) {
        uint32_t address = offset + erased;
        const uint8_t command[4] = {
            0x20u, (uint8_t)(address >> 16),
            (uint8_t)(address >> 8), (uint8_t)address,
        };
        if (!inktag_om_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH,
                                                 true) ||
            !write_enable() || !transfer(command, NULL, sizeof(command), NULL) ||
            !wait_ready(1000u)) {
            ok = false;
            break;
        }
    }
    bus_close();
    return ok;
}
