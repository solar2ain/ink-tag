#include "inktag_da_recovery.h"

#include <stdbool.h>
#include <stddef.h>

#include "arch.h"
#include "arch_wdg.h"
#include "inktag_da_health.h"
#include "inktag_da_ota_policy.h"
#if defined(INKTAG_DA_FIXED_RECOVERY)
#include "inktag_da_ota_security.h"
#endif
#include "spi_flash.h"

#define INKTAG_DA_SECONDARY_HEADER_SIZE 8u
#define INKTAG_DA_SECONDARY_RAW_MAX_SIZE 0xA00u
#define INKTAG_DA_SECONDARY_RAM_ADDRESS 0x07FC0000u
#define INKTAG_DA_SECONDARY_STACK_ADDRESS 0x07FD8000u
#define INKTAG_DA_SECONDARY_RESET_ADDRESS 0x07FC012Du

/* App and Recovery both use SDK6's 0xC8 * 10.24 ms watchdog. Reload around
 * bounded journal/CRC I/O in either role; never extend or disable the timer. */
#if defined(INKTAG_DA_FIXED_RECOVERY)
#define INKTAG_DA_RECOVERY_WDG_RELOAD() wdg_reload(WATCHDOG_DEFAULT_PERIOD)
#else
/* Keep repeated register/literal setup out of the SRAM-bound App's callers. */
__attribute__((noinline)) static void application_recovery_feed_watchdog(void)
{
    wdg_reload(WATCHDOG_DEFAULT_PERIOD);
}
#define INKTAG_DA_RECOVERY_WDG_RELOAD() application_recovery_feed_watchdog()
#endif

static bool recovery_range(uint32_t address, uint32_t length)
{
    bool in_a = address >= INKTAG_DA_RECOVERY_CONTROL_A &&
                address <= INKTAG_DA_RECOVERY_CONTROL_A + INKTAG_DA_RECOVERY_SECTOR_SIZE &&
                length <= INKTAG_DA_RECOVERY_CONTROL_A + INKTAG_DA_RECOVERY_SECTOR_SIZE - address;
    bool in_b = address >= INKTAG_DA_RECOVERY_CONTROL_B &&
                address <= INKTAG_DA_RECOVERY_CONTROL_B + INKTAG_DA_RECOVERY_SECTOR_SIZE &&
                length <= INKTAG_DA_RECOVERY_CONTROL_B + INKTAG_DA_RECOVERY_SECTOR_SIZE - address;
    return in_a || in_b;
}

static bool recovery_read(void *context, uint32_t address,
                          void *data, uint32_t length)
{
    uint32_t actual = 0u;
    bool ok;
    (void)context;
    INKTAG_DA_RECOVERY_WDG_RELOAD();
    ok = recovery_range(address, length) &&
         spi_flash_read_data((uint8_t *)data, address, length, &actual) ==
             SPI_FLASH_ERR_OK && actual == length;
    INKTAG_DA_RECOVERY_WDG_RELOAD();
    return ok;
}

static bool recovery_write(void *context, uint32_t address,
                           const void *data, uint32_t length)
{
    uint32_t actual = 0u;
    bool ok;
    (void)context;
    INKTAG_DA_RECOVERY_WDG_RELOAD();
    /* Shared with App trial/epoch confirmation: this is physical metadata
     * mutation, not new OTA admission. UNKNOWN still fails the strict API. */
    ok = recovery_range(address, length) &&
         inktag_da_health_operation_allowed(
             INKTAG_POWER_OPERATION_FLASH, false) &&
         spi_flash_write_data((uint8_t *)data, address, length, &actual) ==
             SPI_FLASH_ERR_OK && actual == length;
    INKTAG_DA_RECOVERY_WDG_RELOAD();
    return ok;
}

static bool recovery_erase(void *context, uint32_t address, uint32_t length)
{
    bool ok;
    (void)context;
    INKTAG_DA_RECOVERY_WDG_RELOAD();
    ok = length == INKTAG_DA_RECOVERY_SECTOR_SIZE &&
         (address == INKTAG_DA_RECOVERY_CONTROL_A ||
          address == INKTAG_DA_RECOVERY_CONTROL_B) &&
         inktag_da_health_operation_allowed(
             INKTAG_POWER_OPERATION_FLASH, false) &&
         spi_flash_block_erase(address, SPI_FLASH_OP_SE) == SPI_FLASH_ERR_OK;
    INKTAG_DA_RECOVERY_WDG_RELOAD();
    return ok;
}

static inktag_recovery_t recovery_context(void)
{
    inktag_recovery_t recovery = {
        .layout = {
            .control_a = INKTAG_DA_RECOVERY_CONTROL_A,
            .control_b = INKTAG_DA_RECOVERY_CONTROL_B,
            .sector_size = INKTAG_DA_RECOVERY_SECTOR_SIZE,
            .layout_signature = INKTAG_DA_RECOVERY_LAYOUT_SIGNATURE,
        },
        .io = {
            .context = NULL,
            .read = recovery_read,
            .write = recovery_write,
            .erase = recovery_erase,
        },
    };
    return recovery;
}

static bool flash_begin(void)
{
#if !defined(INKTAG_DA_APPLICATION_MEMORY_MAP)
    /* Fixed Recovery has no application initializer. Capture policy before
     * its first external-Flash wake; the application samples even earlier. */
    inktag_da_health_init();
#endif
    return spi_flash_release_from_power_down() == SPI_FLASH_ERR_OK;
}

static void flash_end(void)
{
    (void)spi_flash_power_down();
}

static uint32_t read_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data,
                             uint32_t length)
{
    while (length-- != 0u) {
        crc ^= *data++;
        for (uint8_t bit = 0u; bit < 8u; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc;
}

bool inktag_da_recovery_application_crc_valid(void)
{
    uint8_t header[12];
    uint8_t chunk[64];
    uint32_t size;
    uint32_t expected_crc;
    uint32_t address;
    uint32_t remaining;
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t actual = 0u;
    if (!flash_begin()) return false;
    if (spi_flash_read_data(header, INKTAG_DA_APPLICATION_START,
                            sizeof(header), &actual) != SPI_FLASH_ERR_OK ||
        actual != sizeof(header) || header[0] != 0x70u ||
        header[1] != 0x51u || header[2] != 0xAAu) {
        flash_end();
        return false;
    }
    size = read_u32_le(&header[4]);
    expected_crc = read_u32_le(&header[8]);
    if (size == 0u || size > INKTAG_DA_APPLICATION_RAW_LIMIT) {
        flash_end();
        return false;
    }
    address = INKTAG_DA_APPLICATION_START + 64u;
    remaining = size;
    while (remaining != 0u) {
        uint32_t take = remaining > sizeof(chunk) ? sizeof(chunk) : remaining;
        INKTAG_DA_RECOVERY_WDG_RELOAD();
        actual = 0u;
        if (spi_flash_read_data(chunk, address, take, &actual) !=
                SPI_FLASH_ERR_OK || actual != take) {
            flash_end();
            return false;
        }
        crc = crc32_update(crc, chunk, take);
        address += take;
        remaining -= take;
#if defined(INKTAG_DA_FIXED_RECOVERY)
        /* The read has deasserted CS. Keep the resume lock while draining
         * controller events, instead of accumulating a burst until erase. */
        inktag_da_recovery_service_ble();
#endif
    }
    INKTAG_DA_RECOVERY_WDG_RELOAD();
    flash_end();
    return ~crc == expected_crc;
}

static bool recovery_markers(uint8_t *request, uint8_t *mutation)
{
    uint8_t markers[5];
    uint32_t actual = 0u;
    bool ok;
    if (request == NULL || mutation == NULL || !flash_begin()) return false;
    ok = spi_flash_read_data(markers, INKTAG_DA_RECOVERY_REQUEST_ADDRESS,
                             sizeof(markers), &actual) == SPI_FLASH_ERR_OK &&
         actual == sizeof(markers);
    flash_end();
    if (!ok) return false;
    *request = markers[0];
    *mutation = markers[4];
    return true;
}

static bool clear_recovery_markers(void)
{
    uint8_t verify[5];
    uint32_t actual = 0u;
    bool ok;
#if defined(INKTAG_DA_FIXED_RECOVERY)
    if (!inktag_da_health_operation_allowed(
            INKTAG_POWER_OPERATION_FLASH, true)) {
        inktag_da_ota_reject_power(INKTAG_OTA_REJECT_STAGE_MODE_SWITCH,
            INKTAG_DA_RECOVERY_REQUEST_ADDRESS,
            INKTAG_POWER_POLICY_FLASH_MIN_MV, true);
        return false;
    }
    if (!flash_begin()) {
        inktag_da_ota_reject_set_default(INKTAG_OTA_REJECT_REASON_INTERNAL,
            INKTAG_OTA_REJECT_STAGE_MODE_SWITCH,
            INKTAG_OTA_REJECT_FLAG_DETAIL_IS_ADDRESS,
            INKTAG_DA_RECOVERY_REQUEST_ADDRESS);
        return false;
    }
#else
    if (!inktag_da_health_operation_allowed(
            INKTAG_POWER_OPERATION_FLASH, true) || !flash_begin()) return false;
#endif
#if defined(INKTAG_DA_FIXED_RECOVERY)
    if (!inktag_da_health_operation_allowed(
            INKTAG_POWER_OPERATION_FLASH, false)) {
        inktag_da_ota_reject_power(INKTAG_OTA_REJECT_STAGE_ERASE,
            INKTAG_DA_FIRST_DISPLAY_MARKER_ADDRESS,
            INKTAG_POWER_POLICY_FLASH_MIN_MV, true);
        flash_end();
        return false;
    }
    ok = spi_flash_block_erase(INKTAG_DA_FIRST_DISPLAY_MARKER_ADDRESS,
                              SPI_FLASH_OP_SE) == SPI_FLASH_ERR_OK;
    if (!ok)
        inktag_da_ota_reject_set_default(INKTAG_OTA_REJECT_REASON_ERASE_FAILED,
            INKTAG_OTA_REJECT_STAGE_ERASE,
            INKTAG_OTA_REJECT_FLAG_DETAIL_IS_ADDRESS,
            INKTAG_DA_FIRST_DISPLAY_MARKER_ADDRESS);
#else
    ok = inktag_da_health_operation_allowed(
             INKTAG_POWER_OPERATION_FLASH, false) &&
         spi_flash_block_erase(INKTAG_DA_FIRST_DISPLAY_MARKER_ADDRESS,
                               SPI_FLASH_OP_SE) == SPI_FLASH_ERR_OK;
#endif
    INKTAG_DA_RECOVERY_WDG_RELOAD();
    if (ok) {
        actual = 0u;
        ok = spi_flash_read_data(verify, INKTAG_DA_RECOVERY_REQUEST_ADDRESS,
                                 sizeof(verify), &actual) == SPI_FLASH_ERR_OK &&
         actual == sizeof(verify) && verify[0] == 0xFFu &&
             verify[4] == 0xFFu;
    }
    flash_end();
    return ok;
}

bool inktag_da_first_display_requested(uint8_t *factory_selector)
{
    uint32_t marker[2];
    uint32_t actual = 0u;
    bool ok;
    if (!flash_begin()) return false;
    ok = spi_flash_read_data((uint8_t *)marker,
                             INKTAG_DA_FIRST_DISPLAY_MARKER_ADDRESS,
                             sizeof(marker), &actual) == SPI_FLASH_ERR_OK &&
         actual == sizeof(marker);
    actual = 0u;
    *factory_selector = 0xFFu;
    if (spi_flash_read_data(factory_selector,
                            INKTAG_DA_STOCK_SELECTOR_ADDRESS,
                            sizeof(*factory_selector), &actual) !=
            SPI_FLASH_ERR_OK || actual != sizeof(*factory_selector))
        *factory_selector = 0xFFu;
    flash_end();
    return ok && marker[0] == INKTAG_DA_FIRST_DISPLAY_MARKER_MAGIC &&
           marker[1] == ~INKTAG_DA_FIRST_DISPLAY_MARKER_MAGIC;
}

bool inktag_da_first_display_consume(void)
{
    uint32_t consumed = 0u;
    uint32_t verify = UINT32_MAX;
    uint32_t actual = 0u;
    bool ok;
    if (!inktag_da_health_operation_allowed(
            INKTAG_POWER_OPERATION_FLASH, true) || !flash_begin()) return false;
    ok = inktag_da_health_operation_allowed(
             INKTAG_POWER_OPERATION_FLASH, false) &&
         spi_flash_write_data((uint8_t *)&consumed,
                              INKTAG_DA_FIRST_DISPLAY_MARKER_ADDRESS,
                              sizeof(consumed), &actual) == SPI_FLASH_ERR_OK &&
         actual == sizeof(consumed);
    actual = 0u;
    ok = ok && spi_flash_read_data((uint8_t *)&verify,
                                   INKTAG_DA_FIRST_DISPLAY_MARKER_ADDRESS,
                                   sizeof(verify), &actual) == SPI_FLASH_ERR_OK &&
         actual == sizeof(verify) && verify == consumed;
    flash_end();
    return ok;
}

bool inktag_da_application_info(uint32_t *raw_size, uint32_t *crc32)
{
    uint8_t header[12];
    uint32_t size;
    uint32_t checksum;
    uint32_t actual = 0u;
    bool ok;
    if (raw_size == NULL || crc32 == NULL || !flash_begin()) return false;
    ok = spi_flash_read_data(header, INKTAG_DA_APPLICATION_START,
                             sizeof(header), &actual) == SPI_FLASH_ERR_OK &&
         actual == sizeof(header);
    flash_end();
    if (!ok || header[0] != 0x70u || header[1] != 0x51u ||
        header[2] != 0xAAu || header[3] != 0x01u) return false;
    size = read_u32_le(&header[4]);
    checksum = read_u32_le(&header[8]);
    if (size == 0u || size > INKTAG_DA_APPLICATION_RAW_LIMIT) return false;
    *raw_size = size;
    *crc32 = checksum;
    return true;
}

bool inktag_da_enter_fixed_recovery(void)
{
    uint8_t active = INKTAG_DA_RECOVERY_MARKER_ACTIVE;
    uint8_t verify = 0xffu;
    uint32_t actual = 0u;
    bool ok;
    if (!inktag_da_health_operation_allowed(
            INKTAG_POWER_OPERATION_OTA, true) ||
        !flash_begin()) return false;
    /* Keep the current application intact. The InkTag secondary boot checks
     * this one-way request marker and selects fixed Recovery while it is zero.
     * Recovery erases the marker sector only when safe resume succeeds or a
     * completely authenticated candidate has been durably armed. */
    /* Entry was admitted at the OTA floor above. The durable request is an
     * actual Flash write and uses the same strict physical floor as metadata. */
    ok = inktag_da_health_operation_allowed(
             INKTAG_POWER_OPERATION_FLASH, false) &&
         spi_flash_write_data(&active, INKTAG_DA_RECOVERY_REQUEST_ADDRESS,
                              sizeof(active), &actual) == SPI_FLASH_ERR_OK &&
         actual == sizeof(active);
    actual = 0u;
    ok = ok && spi_flash_read_data(&verify, INKTAG_DA_RECOVERY_REQUEST_ADDRESS,
                                   sizeof(verify), &actual) == SPI_FLASH_ERR_OK &&
         actual == sizeof(verify) && verify == active;
    flash_end();
    if (ok) (void)inktag_da_restart_via_secondary_bootloader();
    return false;
}

__attribute__((naked, noreturn, noinline, used,
               section(".text.inktag_da_secondary_jump")))
static void secondary_bootloader_jump(void)
{
    __asm volatile(
        "cpsid i\n"
        "movs r0, #0\n"
        "msr control, r0\n"
        "ldr r0, =0x07FD8000\n"
        "msr msp, r0\n"
        "ldr r0, =0x07FC012D\n"
        "bx r0\n");
}

__attribute__((noinline, used,
               section(".text.inktag_da_secondary_handoff")))
bool inktag_da_restart_via_secondary_bootloader(void)
{
    uint32_t header[2];
    uint32_t raw_size;
    uint32_t actual = 0u;
    volatile const uint32_t *vectors =
        (volatile const uint32_t *)INKTAG_DA_SECONDARY_RAM_ADDRESS;

#if defined(INKTAG_DA_FIXED_RECOVERY)
    /* A just-closed SUOTA session can leave the external Flash in its power-
     * down transition for one wake/read attempt. Retry the non-destructive pP
     * header preflight before giving up and remaining in fixed Recovery. */
    {
        uint8_t attempt;
        for (attempt = 0u; attempt < 3u; ++attempt) {
            actual = 0u;
            if (flash_begin() &&
                spi_flash_read_data((uint8_t *)header, 0u, sizeof(header),
                                    &actual) == SPI_FLASH_ERR_OK &&
                actual == sizeof(header) && header[0] == 0x00005070u)
                break;
            flash_end();
            INKTAG_DA_RECOVERY_WDG_RELOAD();
            arch_asm_delay_us(100u);
        }
        if (attempt == 3u) return false;
    }
#else
    if (!flash_begin()) return false;
    if (spi_flash_read_data((uint8_t *)header, 0u, sizeof(header), &actual) !=
            SPI_FLASH_ERR_OK || actual != sizeof(header) ||
        header[0] != 0x00005070u) {
        flash_end();
        return false;
    }
#endif
    raw_size = __REV(header[1]);
    if (raw_size > INKTAG_DA_SECONDARY_RAW_MAX_SIZE) {
        flash_end();
        return false;
    }

    /* Application and fixed Recovery execute above the bounded destination
     * window. The build wrapper rejects any future link that moves this
     * handoff or its Flash-reader call chain into the bytes being replaced. */
    __disable_irq();
    wdg_freeze();
    actual = 0u;
    if (spi_flash_read_data((uint8_t *)INKTAG_DA_SECONDARY_RAM_ADDRESS,
                            INKTAG_DA_SECONDARY_HEADER_SIZE, raw_size,
                            &actual) != SPI_FLASH_ERR_OK || actual != raw_size ||
        vectors[0] != INKTAG_DA_SECONDARY_STACK_ADDRESS ||
        vectors[1] != INKTAG_DA_SECONDARY_RESET_ADDRESS) {
        /* Returning through an application whose first bytes may already be
         * replaced is unsafe. Let the hardware watchdog perform recovery. */
        wdg_reload(WATCHDOG_DEFAULT_PERIOD);
        wdg_resume();
        for (;;) { }
    }
    secondary_bootloader_jump();
}

bool inktag_da_recovery_mark_mutated(void)
{
    uint8_t active = INKTAG_DA_RECOVERY_MARKER_ACTIVE;
    uint8_t verify = INKTAG_DA_RECOVERY_MARKER_CLEAR;
    uint32_t actual = 0u;
    bool ok;
    if (!inktag_da_health_operation_allowed(
            INKTAG_POWER_OPERATION_FLASH, false) || !flash_begin()) return false;
    ok = spi_flash_write_data(&active, INKTAG_DA_RECOVERY_MUTATION_ADDRESS,
                              sizeof(active), &actual) == SPI_FLASH_ERR_OK &&
         actual == sizeof(active);
    actual = 0u;
    ok = ok && spi_flash_read_data(&verify,
                                   INKTAG_DA_RECOVERY_MUTATION_ADDRESS,
                                   sizeof(verify), &actual) == SPI_FLASH_ERR_OK &&
         actual == sizeof(verify) && verify == active;
    flash_end();
    return ok;
}

#if defined(INKTAG_DA_FIXED_RECOVERY)
static bool resume_rejected(inktag_ota_reject_reason_t reason,
                            inktag_ota_reject_stage_t stage, uint32_t address)
{
    inktag_da_ota_reject_set(reason, stage,
        INKTAG_OTA_REJECT_FLAG_DETAIL_IS_ADDRESS, address, 0u, 0u);
    return false;
}

bool inktag_da_recovery_resume_allowed(void)
{
    uint8_t request;
    uint8_t mutation;
    inktag_recovery_state_t state = INKTAG_RECOVERY_STATE_NONE;
    inktag_recovery_result_t status;
    uint32_t size;
    uint32_t checksum;
    if (!recovery_markers(&request, &mutation))
        return resume_rejected(INKTAG_OTA_REJECT_REASON_INTERNAL,
            INKTAG_OTA_REJECT_STAGE_MODE_SWITCH,
            INKTAG_DA_RECOVERY_REQUEST_ADDRESS);
    if (request != INKTAG_DA_RECOVERY_MARKER_ACTIVE ||
        mutation != INKTAG_DA_RECOVERY_MARKER_CLEAR)
        return resume_rejected(INKTAG_OTA_REJECT_REASON_IMAGE_INFO_INVALID,
            INKTAG_OTA_REJECT_STAGE_MODE_SWITCH,
            request != INKTAG_DA_RECOVERY_MARKER_ACTIVE ?
                INKTAG_DA_RECOVERY_REQUEST_ADDRESS :
                INKTAG_DA_RECOVERY_MUTATION_ADDRESS);
    status = inktag_da_recovery_status(&state, NULL);
    if (status != INKTAG_RECOVERY_OK &&
        status != INKTAG_RECOVERY_NOT_PENDING)
        return resume_rejected(INKTAG_OTA_REJECT_REASON_INTERNAL,
            INKTAG_OTA_REJECT_STAGE_MODE_SWITCH, INKTAG_DA_RECOVERY_CONTROL_A);
    if (state == INKTAG_RECOVERY_STATE_ARMED ||
        state == INKTAG_RECOVERY_STATE_TRIAL_STARTED ||
        state == INKTAG_RECOVERY_STATE_REQUIRED)
        return resume_rejected(INKTAG_OTA_REJECT_REASON_IMAGE_INFO_INVALID,
            INKTAG_OTA_REJECT_STAGE_MODE_SWITCH, INKTAG_DA_RECOVERY_CONTROL_A);
    if (!inktag_da_application_info(&size, &checksum))
        return resume_rejected(INKTAG_OTA_REJECT_REASON_IMAGE_INFO_INVALID,
            INKTAG_OTA_REJECT_STAGE_IMAGE_VERIFY, INKTAG_DA_APPLICATION_START);
    if (!inktag_da_health_operation_allowed(
            INKTAG_POWER_OPERATION_FLASH, true)) {
        inktag_da_ota_reject_power(INKTAG_OTA_REJECT_STAGE_MODE_SWITCH,
            INKTAG_DA_RECOVERY_REQUEST_ADDRESS,
            INKTAG_POWER_POLICY_FLASH_MIN_MV, true);
        return false;
    }
    return true;
}

bool inktag_da_recovery_resume_application(void)
{
    if (!inktag_da_recovery_resume_allowed()) return false;
    if (!inktag_da_recovery_application_crc_valid())
        return resume_rejected(INKTAG_OTA_REJECT_REASON_CHECKSUM_OR_SIGNATURE_FAILED,
            INKTAG_OTA_REJECT_STAGE_IMAGE_VERIFY, INKTAG_DA_APPLICATION_START);
    if (!inktag_da_health_operation_allowed(
            INKTAG_POWER_OPERATION_FLASH, true)) {
        inktag_da_ota_reject_power(INKTAG_OTA_REJECT_STAGE_MODE_SWITCH,
            INKTAG_DA_RECOVERY_REQUEST_ADDRESS,
            INKTAG_POWER_POLICY_FLASH_MIN_MV, true);
        return false;
    }
    if (!clear_recovery_markers()) {
        inktag_da_ota_reject_set_default(INKTAG_OTA_REJECT_REASON_INTERNAL,
            INKTAG_OTA_REJECT_STAGE_METADATA_COMMIT,
            INKTAG_OTA_REJECT_FLAG_DETAIL_IS_ADDRESS,
            INKTAG_DA_FIRST_DISPLAY_MARKER_ADDRESS);
        return false;
    }
    return true;
}

#endif /* INKTAG_DA_FIXED_RECOVERY: no security dependencies in the App */

bool inktag_da_recovery_release_request(void)
{
    return clear_recovery_markers();
}

/* Signature verification scratch must unwind before App settings repair;
 * LTO must not merge these disjoint startup phases into one large frame. */
__attribute__((noinline)) inktag_recovery_result_t inktag_da_recovery_boot(bool *ota_install_boot)
{
    inktag_recovery_t recovery = recovery_context();
    inktag_recovery_state_t state = INKTAG_RECOVERY_STATE_NONE;
    inktag_recovery_result_t result;
    if (ota_install_boot != NULL) *ota_install_boot = false;
    if (!flash_begin()) return INKTAG_RECOVERY_IO_ERROR;
    result = inktag_recovery_status(&recovery, &state, NULL);
    if (result != INKTAG_RECOVERY_OK && result != INKTAG_RECOVERY_NOT_PENDING) {
        flash_end();
        return result;
    }
    if (ota_install_boot != NULL && result == INKTAG_RECOVERY_OK &&
        state == INKTAG_RECOVERY_STATE_ARMED) {
        *ota_install_boot = true;
    }
    result = inktag_recovery_boot(&recovery);
    flash_end();
    return result;
}

inktag_recovery_result_t inktag_da_recovery_confirm(void)
{
    inktag_recovery_t recovery = recovery_context();
    inktag_recovery_result_t result;
    if (!flash_begin()) return INKTAG_RECOVERY_IO_ERROR;
    result = inktag_recovery_confirm(&recovery);
    flash_end();
    return result;
}

inktag_recovery_result_t inktag_da_recovery_confirm_epoch(
    uint32_t compatibility_epoch)
{
    inktag_recovery_t recovery = recovery_context();
    inktag_recovery_result_t result;
    if (!flash_begin()) return INKTAG_RECOVERY_IO_ERROR;
    result = inktag_recovery_confirm_compatibility_epoch(
        &recovery, compatibility_epoch);
    flash_end();
    return result;
}

inktag_recovery_result_t inktag_da_recovery_arm(void)
{
    inktag_recovery_t recovery = recovery_context();
    inktag_recovery_result_t result;
    if (!flash_begin()) return INKTAG_RECOVERY_IO_ERROR;
    result = inktag_recovery_arm(&recovery);
    flash_end();
    return result;
}

inktag_recovery_result_t inktag_da_recovery_arm_epoch(
    uint32_t compatibility_epoch)
{
    inktag_recovery_t recovery = recovery_context();
    inktag_recovery_result_t result;
    if (!flash_begin()) return INKTAG_RECOVERY_IO_ERROR;
    result = inktag_recovery_arm_compatibility_epoch(
        &recovery, compatibility_epoch);
    flash_end();
    return result;
}

inktag_recovery_result_t inktag_da_recovery_check_epoch(
    uint32_t compatibility_epoch)
{
    inktag_recovery_t recovery = recovery_context();
    inktag_recovery_result_t result;
    if (!flash_begin()) return INKTAG_RECOVERY_IO_ERROR;
    result = inktag_recovery_check_compatibility_epoch(
        &recovery, compatibility_epoch);
    flash_end();
    return result;
}

bool inktag_da_pin_epoch_confirmed(void)
{
    inktag_recovery_t recovery = recovery_context();
    bool confirmed;
    if (!flash_begin()) return false;
    confirmed = inktag_recovery_confirmed_epoch(
        &recovery, INKTAG_DA_OTA_COMPATIBILITY_EPOCH);
    flash_end();
    return confirmed;
}

inktag_recovery_result_t inktag_da_recovery_status(
    inktag_recovery_state_t *state, uint32_t *generation)
{
    inktag_recovery_t recovery = recovery_context();
    inktag_recovery_result_t result;
    if (!flash_begin()) return INKTAG_RECOVERY_IO_ERROR;
    result = inktag_recovery_status(&recovery, state, generation);
    flash_end();
    return result;
}
