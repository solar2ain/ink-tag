/* Application-only, single-bank activation for InkTag Nordic Recovery. */
#include <stdbool.h>
#include <stdint.h>

#include "crc32.h"
#include "inktag_nrf_dfu_epoch.h"
#include "inktag_nrf_boot_integrity.h"
#include "nrf_assert.h"
#include "nrf_bootloader_info.h"
#include "nrf_bootloader_fw_activation.h"
#include "nrf_dfu_flash.h"
#include "nrf_dfu_settings.h"
#include "nrf_dfu_utils.h"
#include "nrf_error.h"
#include "nrf_power.h"

#if !NRF_DFU_SINGLE_BANK_APP_UPDATES
#error "InkTag Nordic Recovery activation requires single-bank application updates"
#endif

static volatile bool m_settings_write_done;

ret_code_t inktag_nrf_dfu_settings_init_original(bool sd_irq_initialized);

/* The application requests this through a retained register only after its
 * service and advertising path is ready. Commit the pending native version in
 * Recovery so both DFU settings pages retain the confirmed epoch atomically
 * under the same per-write voltage gate as every other Recovery mutation. */
ret_code_t nrf_dfu_settings_init(bool sd_irq_initialized)
{
    ret_code_t result =
        inktag_nrf_dfu_settings_init_original(sd_irq_initialized);
    uint32_t pending_version;

    if (result != NRF_SUCCESS ||
        nrf_power_gpregret_get() != INKTAG_NRF_DFU_CONFIRM_GPREGRET)
    {
        return result;
    }
    pending_version = s_dfu_settings.bootloader_version;
    if (pending_version == INKTAG_NRF_DFU_NO_PENDING_VERSION)
    {
        nrf_power_gpregret_set(0u);
        return result;
    }
    if (!inktag_nrf_boot_integrity_valid(s_dfu_settings.bank_0.image_size, s_dfu_settings.bank_0.image_crc))
        return result; /* Do not confirm a corrupt/torn pending App. */
    s_dfu_settings.app_version = pending_version;
    s_dfu_settings.bootloader_version = INKTAG_NRF_DFU_NO_PENDING_VERSION;
    result = nrf_dfu_settings_write_and_backup(NULL);
    if (result == NRF_SUCCESS)
    {
        nrf_power_gpregret_set(0u);
    }
    return result;
}

static void settings_write_callback(void * p_context)
{
    (void)p_context;
    m_settings_write_done = true;
}

nrf_bootloader_fw_activation_result_t nrf_bootloader_fw_activate(void)
{
    uint32_t const app_start = nrf_dfu_bank0_start_addr();
    uint32_t const image_size = s_dfu_settings.bank_1.image_size;
    uint32_t const image_crc = s_dfu_settings.bank_1.image_crc;

    if (s_dfu_settings.bank_1.bank_code == NRF_DFU_BANK_INVALID)
    {
        return ACTIVATION_NONE;
    }
    if ((s_dfu_settings.bank_1.bank_code != NRF_DFU_BANK_VALID_APP) ||
        (s_dfu_settings.progress.update_start_address != app_start) ||
        (image_size == 0U) ||
        (image_size > (DFU_REGION_END(BOOTLOADER_START_ADDR) - app_start)))
    {
        return ACTIVATION_ERROR;
    }
    if (crc32_compute((uint8_t const *)app_start, image_size, NULL) != image_crc ||
        !inktag_nrf_boot_integrity_valid(image_size, image_crc))
    {
        return ACTIVATION_ERROR;
    }

    s_dfu_settings.bank_0.bank_code = NRF_DFU_BANK_VALID_APP;
    s_dfu_settings.bank_0.image_crc = image_crc;
    s_dfu_settings.bank_0.image_size = image_size;
    nrf_dfu_bank_invalidate(&s_dfu_settings.bank_1);

    m_settings_write_done = false;
    if (nrf_dfu_settings_write_and_backup(settings_write_callback) != NRF_SUCCESS)
    {
        return ACTIVATION_ERROR;
    }
    ASSERT(m_settings_write_done);
    return ACTIVATION_SUCCESS;
}
