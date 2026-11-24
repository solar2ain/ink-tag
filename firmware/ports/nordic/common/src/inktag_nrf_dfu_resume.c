/* nRF52832 pilot: safe exit from fixed SecureDFU, not a force-boot path. */
#include "inktag_nrf_dfu_resume.h"

#include <stddef.h>
#include <string.h>
#include "app_scheduler.h"
#include "crc32.h"
#include "inktag_nrf_dfu_epoch.h"
#include "inktag_nrf_boot_integrity.h"
#include "inktag_nrf_dfu_power.h"
#include "nrf.h"
#include "nrf_bootloader_info.h"
#include "nrf_dfu_settings.h"
#include "nrf_dfu_req_handler.h"
#include "nrf_dfu_types.h"
#include "nrf_dfu_utils.h"
#include "nrf_fstorage.h"
#include "nrf_gpio.h"
#include "nrf_power.h"

#if !NRF_DFU_PROTOCOL_REDUCED || !NRF_DFU_SINGLE_BANK_APP_UPDATES
#error "Guarded resume pilot requires the qualified reduced single-bank DFU"
#endif

/* SDK nrf_dfu_settings.c uses this exact bounded CRC span; it is private there. */
#define DFU_SETTINGS_BOOT_VALIDATION_SIZE (4u + 3u * sizeof(boot_validation_t))

static bool s_dirty;
static bool s_pending;
static bool s_ack_confirmed;
static uint16_t s_notifications;
static uint16_t s_ack_remaining;

static nrf_dfu_settings_t const * settings_page(uint32_t address)
{
    return (nrf_dfu_settings_t const *)(uintptr_t)address;
}

static bool settings_valid(nrf_dfu_settings_t const * settings)
{
    return settings->settings_version == NRF_DFU_SETTINGS_VERSION &&
        settings->crc != UINT32_MAX &&
        settings->crc == crc32_compute((uint8_t const *)settings + 4u,
            offsetof(nrf_dfu_settings_t, init_command) - 4u, NULL) &&
        settings->boot_validation_crc == crc32_compute(
            (uint8_t const *)&settings->boot_validation_softdevice,
            DFU_SETTINGS_BOOT_VALIDATION_SIZE - 4u, NULL);
}

static bool settings_match(nrf_dfu_settings_t const * settings)
{
    return settings_valid(settings) &&
        memcmp(settings, &s_dfu_settings,
               offsetof(nrf_dfu_settings_t, peer_data)) == 0;
}

bool inktag_nrf_dfu_settings_pair_matches(void)
{
    return settings_match(settings_page(BOOTLOADER_SETTINGS_ADDRESS)) &&
        settings_match(settings_page(BOOTLOADER_SETTINGS_BACKUP_ADDRESS));
}

bool inktag_nrf_dfu_resume_app_write_allowed(void)
{
    nrf_dfu_settings_t const * primary =
        settings_page(BOOTLOADER_SETTINGS_ADDRESS);
    nrf_dfu_settings_t const * backup =
        settings_page(BOOTLOADER_SETTINGS_BACKUP_ADDRESS);
    /* Queued writes are checked at the physical submission, after preceding
     * metadata operations. A torn/failed backup must not retain an old App
     * authority that a later power cycle could resurrect. */
    return s_dfu_settings.bank_0.bank_code == NRF_DFU_BANK_INVALID &&
        settings_valid(primary) && settings_valid(backup) &&
        primary->bank_0.bank_code == NRF_DFU_BANK_INVALID &&
        backup->bank_0.bank_code == NRF_DFU_BANK_INVALID;
}

bool inktag_nrf_dfu_resume_allowed(void)
{
    uint32_t const app_start = nrf_dfu_bank0_start_addr();
    dfu_progress_t const * progress = &s_dfu_settings.progress;

    if (s_dirty || nrf_fstorage_is_busy(NULL) ||
        app_start <= MBR_SIZE ||
        app_start >= DFU_REGION_END(BOOTLOADER_START_ADDR) ||
        s_dfu_settings.bank_0.bank_code != NRF_DFU_BANK_VALID_APP ||
        s_dfu_settings.bank_0.image_size == 0u ||
        s_dfu_settings.bank_0.image_size >
            DFU_REGION_END(BOOTLOADER_START_ADDR) - app_start ||
        s_dfu_settings.boot_validation_app.type == NO_VALIDATION ||
        s_dfu_settings.boot_validation_softdevice.type == NO_VALIDATION ||
        s_dfu_settings.sd_size == 0u ||
        s_dfu_settings.sd_size > app_start - MBR_SIZE ||
        s_dfu_settings.bank_1.bank_code != NRF_DFU_BANK_INVALID ||
        s_dfu_settings.bank_current != NRF_DFU_CURRENT_BANK_0 ||
        s_dfu_settings.bootloader_version != INKTAG_NRF_DFU_NO_PENDING_VERSION ||
        s_dfu_settings.enter_buttonless_dfu != 0u ||
        s_dfu_settings.write_offset != 0u ||
        progress->command_size != 0u || progress->command_offset != 0u ||
        progress->command_crc != 0u || progress->data_object_size != 0u ||
        progress->firmware_image_offset != 0u ||
        progress->firmware_image_offset_last != 0u ||
        progress->firmware_image_crc_last != 0u ||
        (progress->update_start_address != 0u &&
         progress->update_start_address != app_start) ||
        nrf_power_gpregret_get() != 0u)
        return false;
#if NRF_BL_DFU_ENTER_METHOD_BUTTON
    if (nrf_gpio_pin_read(NRF_BL_DFU_ENTER_METHOD_BUTTON_PIN) == 0u)
        return false;
#endif
    return inktag_nrf_dfu_settings_pair_matches() &&
        inktag_nrf_dfu_resume_boot_valid() &&
        inktag_nrf_dfu_power_allows_write();
}

void inktag_nrf_dfu_resume_touch(void)
{
    /* Admission, not completion: failed, queued or disconnected transactions
     * never restore the permission during this Recovery residence. */
    s_dirty = true;
}

bool inktag_nrf_dfu_resume_pending(void)
{
    return s_pending;
}

uint8_t inktag_nrf_dfu_resume_prepare(void)
{
    if (s_pending || !inktag_nrf_dfu_resume_allowed())
        return NRF_DFU_RES_CODE_OPERATION_NOT_PERMITTED;
    s_pending = true;
    s_ack_confirmed = false;
    return NRF_DFU_RES_CODE_SUCCESS;
}

static void resume_after_ack(void * data, uint16_t length)
{
    (void)data;
    (void)length;
    bool const allowed = s_pending && inktag_nrf_dfu_resume_allowed();
    s_pending = false;
    s_ack_confirmed = false;
    if (allowed) {
        /* GPREGRET was already cleared by the native DFU entry path. Leave
         * metadata, epoch, Settings and credentials unchanged; normal boot
         * validates the complete App/SD again. Never jump around validation. */
        NVIC_SystemReset();
    }
}

void inktag_nrf_dfu_resume_response_sent(uint8_t opcode, uint8_t result,
                                        uint32_t error)
{
    if (error == NRF_SUCCESS) {
        if (s_notifications == UINT16_MAX) s_dirty = true;
        else ++s_notifications;
    }
    if (opcode == NRF_DFU_OP_ABORT &&
        result == NRF_DFU_RES_CODE_SUCCESS && s_pending) {
        if (error == NRF_SUCCESS && !s_dirty)
            s_ack_remaining = s_notifications;
        else s_pending = false;
    }
}

void inktag_nrf_dfu_resume_tx_complete(uint16_t count)
{
    s_notifications = count < s_notifications
        ? (uint16_t)(s_notifications - count) : 0u;
    if (!s_pending || s_ack_remaining == 0u) return;
    if (count < s_ack_remaining) {
        s_ack_remaining = (uint16_t)(s_ack_remaining - count);
        return;
    }
    s_ack_remaining = 0u;
    s_ack_confirmed = true;
    if (app_sched_event_put(NULL, 0u, resume_after_ack) != NRF_SUCCESS) {
        s_pending = false;
        s_ack_confirmed = false;
    }
}

void inktag_nrf_dfu_resume_disconnect(void)
{
    /* The host may disconnect immediately after the accepted response. Only
     * an ACK still unconfirmed at the transport can cancel the continuation. */
    if (!s_ack_confirmed) s_pending = false;
    s_ack_remaining = s_notifications = 0u;
}
