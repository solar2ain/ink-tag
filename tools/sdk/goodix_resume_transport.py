"""Adapt the existing GR Recovery transport; no App/reset/signature policy fork."""
from pathlib import Path
import re

MARKER = '/* InkTag Recovery return after actual response TX. */'

STATE = '''#if defined(BOOTLOADER_ENABLE) && BLE_SUPPORT
#include "inktag_gr_dfu_resume.h"
static inktag_gr_dfu_resume_tx_t s_inktag_resume_tx;
static void inktag_dfu_resume_poll(void);
#endif
'''

RESPONSE = '''/* InkTag Recovery return after actual response TX. */
#if defined(BOOTLOADER_ENABLE) && BLE_SUPPORT
__WEAK bool inktag_dfu_safe_resume_commit(void)
{
    return false;
}

static void inktag_dfu_resume_response(uint8_t *data, uint16_t len, uint16_t cmd)
{
    inktag_gr_resume_queued(&s_inktag_resume_tx, len);
    dfu_send_frame(data, len, cmd);
}

/* Leave the SDK's frame serializer and queue in place. Account before its
 * void enqueue call, including when it invokes ble_send_data synchronously. */
#define dfu_send_frame(data, len, cmd) inktag_dfu_resume_response(data, len, cmd)

static void inktag_dfu_resume_poll(void)
{
    if (__get_IPSR() != 0u || !inktag_gr_resume_take_ready(&s_inktag_resume_tx)) return;
    if (s_inktag_normal_transfer_active ||
        s_inktag_application_mutated_this_transfer ||
        !inktag_dfu_safe_resume_commit()) return;
    inktag_dfu_before_reset();
#if defined(INKTAG_GOODIX_GR5331)
    pwr_mgmt_set_wakeup_flag(COLD_BOOT);
#endif
    hal_nvic_system_reset();
}
#endif

'''

SCHEDULE = '''static bool inktag_dfu_schedule_safe_resume(void)
{
#if defined(BOOTLOADER_ENABLE) && BLE_SUPPORT
    return !s_inktag_normal_transfer_active &&
           !s_inktag_application_mutated_this_transfer &&
           inktag_dfu_safe_resume_allowed() &&
           inktag_gr_resume_prepare(&s_inktag_resume_tx);
#else
    return false;
#endif
}
'''

POST_SEND = '''#if defined(BOOTLOADER_ENABLE) && BLE_SUPPORT
    sdk_err_t sent = otas_notify_tx_data(s_ota_conn_index, p_data, length);
    inktag_gr_resume_sent(&s_inktag_resume_tx, length, sent == SDK_SUCCESS);
#else
    otas_notify_tx_data(s_ota_conn_index, p_data, length);
#endif'''

PRE_SEND = '''#if defined(BOOTLOADER_ENABLE) && BLE_SUPPORT
    /* Publish the notification before the SDK can report its completion.
     * A failed submit still invalidates admission before main-loop polling. */
    inktag_gr_resume_sent(&s_inktag_resume_tx, length, true);
    sdk_err_t sent = otas_notify_tx_data(s_ota_conn_index, p_data, length);
    if (sent != SDK_SUCCESS) inktag_gr_resume_fail(&s_inktag_resume_tx);
#else
    otas_notify_tx_data(s_ota_conn_index, p_data, length);
#endif'''

OLD_DISABLED_CASE = '''        case OTAS_EVT_TX_NOTIFICATION_DISABLED:
#if defined(BOOTLOADER_ENABLE) && BLE_SUPPORT
            if (!s_inktag_resume_tx.ready) inktag_gr_resume_cancel(&s_inktag_resume_tx);
#endif
            break;
'''

RECOVERY_DISABLED_CASE = '''#if defined(BOOTLOADER_ENABLE) && BLE_SUPPORT
        case OTAS_EVT_TX_NOTIFICATION_DISABLED:
            if (!s_inktag_resume_tx.ready) inktag_gr_resume_cancel(&s_inktag_resume_tx);
            break;
#endif
'''

def replace_one(text, old, new, path):
    if text.count(old) != 1:
        raise ValueError(f'Unknown GR Recovery transport shape in {path}: {old[:70]!r}')
    return text.replace(old, new, 1)


def patch_resume_transport(sdk: Path, repo: Path) -> bool:
    source = sdk/'components/libraries/dfu_port/dfu_port.c'
    profile = sdk/'components/profiles/otas/otas.c'
    text = source.read_text(encoding='utf-8')
    if MARKER not in text:
        text = replace_one(text, 'static bool                 s_inktag_safe_resume_pending = false;\n', STATE, source)
        anchor = 'static void get_info_replace(dfu_receive_frame_t *p_frame)\n'
        text = replace_one(text, anchor, RESPONSE + anchor, source)
        text, count = re.subn(r'static bool inktag_dfu_schedule_safe_resume\(void\)\n\{.*?\n\}\n',
                              lambda _: SCHEDULE, text, count=1, flags=re.S)
        if count != 1: raise ValueError('Missing native GR safe-resume guard')
        # Admission rejection must arm before the existing serializer may send
        # synchronously. The response and every native error byte stay unchanged.
        text = text.replace('dfu_send_frame(p_frame->data, reject_len, p_frame->cmd_type);\n'
                            '        (void)inktag_dfu_schedule_safe_resume();',
                            '(void)inktag_dfu_schedule_safe_resume();\n'
                            '        dfu_send_frame(p_frame->data, reject_len, p_frame->cmd_type);')
        disconnect = re.compile(r'            if \(s_inktag_safe_resume_pending\)\n            \{.*?\n            \}\n',re.S)
        text,count = disconnect.subn('''#if defined(BOOTLOADER_ENABLE) && BLE_SUPPORT
            inktag_gr_resume_disconnected(&s_inktag_resume_tx);
#endif
''',text,count=1)
        if count != 1: raise ValueError('Missing old GR disconnect return')
        text = replace_one(text, '        case OTAS_EVT_TX_NOTIFICATION_ENABLED:\n',
            '''        case OTAS_EVT_TX_NOTIFICATION_DISABLED:
#if defined(BOOTLOADER_ENABLE) && BLE_SUPPORT
            if (!s_inktag_resume_tx.ready) inktag_gr_resume_cancel(&s_inktag_resume_tx);
#endif
            break;

        case OTAS_EVT_TX_NOTIFICATION_ENABLED:
#if defined(BOOTLOADER_ENABLE) && BLE_SUPPORT
            s_inktag_resume_tx = (inktag_gr_dfu_resume_tx_t){0};
#endif
''',source)
        text = replace_one(text, '        case OTAS_EVT_RX_RECEIVE_DATA:\n',
            '''        case OTAS_EVT_RX_RECEIVE_DATA:
#if defined(BOOTLOADER_ENABLE) && BLE_SUPPORT
            inktag_gr_resume_cancel(&s_inktag_resume_tx);
#endif
''',source)
        text = replace_one(text, '        case OTAS_EVT_NOTIFY_COMPLETE:\n',
            '''        case OTAS_EVT_NOTIFY_COMPLETE:
#if defined(BOOTLOADER_ENABLE) && BLE_SUPPORT
            inktag_gr_resume_completed(&s_inktag_resume_tx);
#endif
''',source)
        text = replace_one(text, '    otas_notify_tx_data(s_ota_conn_index, p_data, length);',
            PRE_SEND,source)
        text = replace_one(text, '    normal_dfu_schedule();\n',
            '''    normal_dfu_schedule();
#if defined(BOOTLOADER_ENABLE) && BLE_SUPPORT
    inktag_dfu_resume_poll();
#endif
''',source)
        if 's_inktag_safe_resume_pending' in text:
            raise ValueError('Old GR disconnect-only return remains')
    # Existing materialized SDKs must receive the same correction on rebuild.
    if POST_SEND in text:
        text = replace_one(text, POST_SEND, PRE_SEND, source)
    if PRE_SEND not in text:
        raise ValueError(f'Unknown GR Recovery notification accounting in {source}')
    # Even an empty extra case can change the App's optimized dispatch table.
    # Confine the entire new event case, not only its body, to Recovery.
    if OLD_DISABLED_CASE in text:
        text = replace_one(text, OLD_DISABLED_CASE, RECOVERY_DISABLED_CASE, source)
    if RECOVERY_DISABLED_CASE not in text:
        raise ValueError(f'Unknown GR Recovery notification-disable case in {source}')
    # A notification on another characteristic is not this ACK's completion.
    event = profile.read_text(encoding='utf-8')
    exact = '''if(p_ntf_ind->type == BLE_GATT_NOTIFICATION
#ifdef BOOTLOADER_ENABLE
               && p_ntf_ind->handle == s_otas_env.start_hdl + OTAS_IDX_TX_VAL
#endif
              )'''
    if exact not in event:
        event = replace_one(event,'if(p_ntf_ind->type == BLE_GATT_NOTIFICATION)',exact,profile)
    header = repo/'firmware/ports/goodix/common/src/inktag_gr_dfu_resume.h'
    destination = source.with_name(header.name)
    changes = [(source,text),(profile,event),(destination,header.read_text(encoding='utf-8'))]
    changed = False
    for path,content in changes:
        if not path.exists() or path.read_text(encoding='utf-8') != content:
            path.write_text(content,encoding='utf-8',newline='\n')
            changed = True
    return changed
