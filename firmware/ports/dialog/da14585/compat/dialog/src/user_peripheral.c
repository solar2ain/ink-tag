/* DA normal source projection v1 */
#include "user_peripheral.h"

#include "inktag_epd_config.h"

#include <string.h>

#include "app_mid.h"
#include "custs1_task.h"
#include "gattc_task.h"
#include "gap.h"
#include "gapm.h"
#include "user_config.h"
#include "user_custs1_def.h"
#include "user_custs1_impl.h"
#include "inktag_ble_power.h"
#include "inktag_da_ble_sdk_address.h"
#include "inktag_board.h"

#define DEVICE_NAME_PREFIX "IKT_DA"
#define DEVICE_NAME_MAX_LEN 12
#define DEVICE_ADV_MAX_LEN (DEVICE_NAME_MAX_LEN + 7)

uint8_t app_connection_idx __SECTION_ZERO("retention_mem_area0");
/* ROM appends the three-byte Flags field. The 11-byte factory name and six-byte
 * InkTag marker fit in the remaining primary-advertisement budget. */
static uint8_t s_adv_data[DEVICE_ADV_MAX_LEN]
    __SECTION_ZERO("retention_mem_area0");
static uint8_t s_adv_len __SECTION_ZERO("retention_mem_area0");
static bool s_trial_confirmed __SECTION_ZERO("retention_mem_area0");


static const uint8_t s_scan_rsp_data[] = {
    0x11,
    GAP_AD_TYPE_COMPLETE_LIST_128_BIT_UUID,
    0xEC, 0x5A, 0x67, 0x1C, 0xC1, 0xB6, 0x46, 0xFB,
    0x8D, 0x91, 0x28, 0xD8, 0x01, 0x00, 0x75, 0x62,
};
static const uint8_t s_marker[] = {
    0x05,
    GAP_AD_TYPE_MANU_SPECIFIC_DATA,
    0xF7, 0x04, 0x05, 0x03,
};

static void apply_device_name(const uint8_t *name, uint8_t name_len)
{
    if (name == NULL || name_len == 0u || name_len >= DEVICE_NAME_MAX_LEN) return;
    s_adv_data[0] = name_len + 1u;
    s_adv_data[1] = GAP_AD_TYPE_COMPLETE_NAME;
    memcpy(&s_adv_data[2], name, name_len);
    memcpy(&s_adv_data[2u + name_len], s_marker, sizeof(s_marker));
    s_adv_len = (uint8_t)(name_len + 2u + sizeof(s_marker));
    device_info.dev_name.length = name_len <= GAP_MAX_NAME_SIZE ? name_len : GAP_MAX_NAME_SIZE;
    memcpy(device_info.dev_name.name, name, device_info.dev_name.length);
    device_info.dev_name.name[device_info.dev_name.length] = '\0';
}

static void init_device_name(void)
{
    static const char hex[] = "0123456789ABCDEF";
    char generated[DEVICE_NAME_MAX_LEN] = DEVICE_NAME_PREFIX "_0000";
    /* DA14585 is permanently Free-only. Never revive a mutable name left by
     * development or older firmware; the factory identity is authoritative. */
    generated[sizeof(DEVICE_NAME_PREFIX)] = hex[gapm_env.addr.addr[1] >> 4];
    generated[sizeof(DEVICE_NAME_PREFIX) + 1] = hex[gapm_env.addr.addr[1] & 0x0F];
    generated[sizeof(DEVICE_NAME_PREFIX) + 2] = hex[gapm_env.addr.addr[0] >> 4];
    generated[sizeof(DEVICE_NAME_PREFIX) + 3] = hex[gapm_env.addr.addr[0] & 0x0F];
    apply_device_name((const uint8_t *)generated,
                      (uint8_t)(sizeof(generated) - 1u));
}

const char *inktag_da_device_name(void)
{
    return device_info.dev_name.length != 0u ?
        (const char *)device_info.dev_name.name : DEVICE_NAME_PREFIX;
}

bool inktag_da_get_address(uint8_t address[6])
{
    inktag_ble_address_t selected;
    if (address == NULL || !inktag_da_ble_sdk_address(&selected)) return false;
    memcpy(address, selected.bytes, sizeof(selected.bytes));
    return true;
}

static void start_normal_advertising(void)
{
    app_advertise_undirected_start_op(user_adv_conf.addr_src,
                                      INKTAG_CONNECTABLE_ADV_INTERVAL_UNITS,
                                      user_adv_conf.channel_map,
                                      user_adv_conf.mode,
                                      user_adv_conf.adv_filt_policy,
                                      s_adv_data,
                                      s_adv_len,
                                      (uint8_t *)s_scan_rsp_data,
                                      sizeof(s_scan_rsp_data));
    /* This submits the GAPM request; it is not evidence that advertising is
     * reachable. Startup display planning is idempotent and independent of
     * OTA trial confirmation. */
    inktag_da_startup_ready();
    ke_state_set(TASK_APP, APP_CONNECTABLE);
}

void user_app_init(void)
{
    inktag_da_init();
    default_app_on_init();
}

arch_main_loop_callback_ret_t user_app_on_system_powered(void)
{
    return inktag_da_process() ? KEEP_POWERED : GOTO_SLEEP;
}

sleep_mode_t user_app_validate_sleep(sleep_mode_t sleep_mode)
{
    if ((sleep_mode == mode_ext_sleep ||
         sleep_mode == mode_ext_sleep_otp_copy) &&
        !inktag_da_allows_extended_sleep()) {
        return mode_idle;
    }
    return sleep_mode;
}

void user_app_adv_start(void)
{
    inktag_ble_address_t selected;
    /* Do not advertise an unproven SDK fallback or silently create an
     * identity. No setter, startup migration, UID hash or flash write. */
    if (!inktag_da_ble_sdk_address(&selected)) return;
    init_device_name();
    start_normal_advertising();
}

void user_app_connection(uint8_t connection_idx, struct gapc_connection_req_ind const *param)
{
    if (app_env[connection_idx].conidx != GAP_INVALID_CONIDX) {
        struct gapc_param_update_cmd *update;

        app_connection_idx = connection_idx;
        /* The SDK accepts the central's initial parameters unless the
         * peripheral explicitly asks for the preferred interval. Recovery
         * already does this. Build the one-shot SDK message directly so the
         * SRAM-bound app does not retain the generic helper's active-message
         * pointer merely to request the same 10-20 ms configuration. */
        update = app_param_update_msg_create(connection_idx);
        update->intv_min = user_connection_param_conf.intv_min;
        update->intv_max = user_connection_param_conf.intv_max;
        update->latency = user_connection_param_conf.latency;
        update->time_out = user_connection_param_conf.time_out;
        update->ce_len_min = user_connection_param_conf.ce_len_min;
        update->ce_len_max = user_connection_param_conf.ce_len_max;
        app_param_update_msg_send(update);
        /* DA SDK6 has no start-success callback for indefinite connectable
         * advertising. A real application connection is the first positive
         * proof that BLE, the InkTag GATT database and advertising are all
         * usable, so only this point may confirm an OTA trial. */
        if (!s_trial_confirmed)
            s_trial_confirmed = inktag_da_connection_reached();
        inktag_da_connected(connection_idx);
    } else {
        user_app_adv_start();
    }

    default_app_on_connection(connection_idx, param);
}

void user_app_adv_undirect_complete(uint8_t status)
{
    if (status == GAP_ERR_CANCELED) user_app_adv_start();
}

void user_app_disconnect(struct gapc_disconnect_ind const *param)
{
    (void)param;
    inktag_da_disconnected();

#if (BLE_SUOTA_RECEIVER)
    if (suota_state.reboot_requested) {
        suota_state.reboot_requested = 0;
        platform_reset(RESET_AFTER_SUOTA_UPDATE);
        return;
    }
#endif

    user_app_adv_start();
}

void user_catch_rest_hndl(ke_msg_id_t const msgid,
                          void const *param,
                          ke_task_id_t const dest_id,
                          ke_task_id_t const src_id)
{
    switch (msgid) {
        case CUSTS1_VAL_WRITE_IND:
            inktag_da_write_handler(msgid, (struct custs1_val_write_ind const *)param, dest_id, src_id);
            break;
        case CUSTS1_VAL_NTF_CFM:
            inktag_da_notify_cfm_handler((struct custs1_val_ntf_cfm const *)param);
            break;
        case CUSTS1_ATT_INFO_REQ:
            inktag_da_att_info_req_handler(msgid, (struct custs1_att_info_req const *)param, dest_id, src_id);
            break;
        case GATTC_EVENT_REQ_IND: {
            struct gattc_event_ind const *ind = (struct gattc_event_ind const *)param;
            struct gattc_event_cfm *cfm = KE_MSG_ALLOC(GATTC_EVENT_CFM, src_id, dest_id, gattc_event_cfm);
            cfm->handle = ind->handle;
            KE_MSG_SEND(cfm);
        } break;
        default:
            break;
    }
}
