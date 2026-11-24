/* Application-only Secure DFU version and target validation. */
#include <stdbool.h>
#include <stdint.h>

#include "dfu-cc.pb.h"
#include "inktag_nrf_dfu_epoch.h"
#include "nrf_assert.h"
#include "nrf_bootloader_info.h"
#include "nrf_dfu_settings.h"
#include "nrf_dfu_types.h"
#include "nrf_dfu_ver_validation.h"

#define EXT_ERR(error) \
    ((nrf_dfu_result_t)((uint32_t)NRF_DFU_RES_CODE_EXT_ERROR + (uint32_t)(error)))
static bool softdevice_requirement_ok(dfu_init_command_t const * p_init)
{
    uint16_t const installed_fwid = SD_FWID_GET(MBR_SIZE);

    if (p_init->sd_req_count == 0U)
    {
        return false;
    }
    for (uint8_t i = 0U; i < p_init->sd_req_count; ++i)
    {
        if (p_init->sd_req[i] == installed_fwid)
        {
            return true;
        }
    }
    return false;
}

nrf_dfu_result_t nrf_dfu_ver_validation_check(dfu_init_command_t const * p_init)
{
    ASSERT(p_init != NULL);

    if (!p_init->has_type || (p_init->type != DFU_FW_TYPE_APPLICATION) ||
        (p_init->hash.hash_type != DFU_HASH_TYPE_SHA256) ||
        (p_init->has_is_debug && p_init->is_debug) ||
        !p_init->has_hw_version)
    {
        return EXT_ERR(NRF_DFU_EXT_ERROR_INIT_COMMAND_INVALID);
    }
    if (p_init->hw_version != NRF_DFU_HW_VERSION)
    {
        return EXT_ERR(NRF_DFU_EXT_ERROR_HW_VERSION_FAILURE);
    }
    if (!softdevice_requirement_ok(p_init))
    {
        return EXT_ERR(NRF_DFU_EXT_ERROR_SD_VERSION_FAILURE);
    }
    if (!p_init->has_fw_version ||
        !inktag_nrf_dfu_version_allowed(p_init->fw_version,
                                        s_dfu_settings.app_version))
    {
        return EXT_ERR(NRF_DFU_EXT_ERROR_FW_VERSION_FAILURE);
    }
    return NRF_DFU_RES_CODE_SUCCESS;
}
