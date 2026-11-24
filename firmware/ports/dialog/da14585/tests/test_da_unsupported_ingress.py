"""Exercise actual GATT admission, bounded RX queue and status wire encoding."""
import pytest

from da_host_helpers import ROOT, SOURCE, compile_run, function


@pytest.mark.parametrize('sanitize', [False, True])
@pytest.mark.parametrize('initial_count', [0, 1])
def test_omitted_requests_never_enter_or_displace_the_actual_rx_queue(tmp_path, sanitize, initial_count):
    protocol = SOURCE.read_text(encoding='utf-8')
    app = (SOURCE.parent/'user_custs1_impl.c').read_text(encoding='utf-8')
    transport = (SOURCE.parent/'inktag_da_notifications.inc').read_text(encoding='utf-8')
    # Preserve the public production function body; static is only for the
    # extraction helper, not a substitute implementation of the entry point.
    ingress = function(app.replace('void inktag_da_write_value(',
                                  'static void inktag_da_write_value('),
                       'inktag_da_write_value(')
    harness = r'''
#include <assert.h>
#include <string.h>
#include "inktag_protocol_core.h"
#define GAP_INVALID_CONIDX 255u
#define INKTAG_IDX_EPD_NTF_CFG 2u
#define INKTAG_IDX_EPD_VAL 1u
#define INKTAG_DA_PROTOCOL_PACKET_BYTES 220u
#define INKTAG_DA_MAX_DATA_LEN 220u
#define INKTAG_DA_IMAGE_QUEUE_DEPTH 1u
typedef struct {uint16_t length;uint8_t data[220];} packet_t;
static packet_t s_image_queue[1];
static uint8_t s_image_count,s_image_head,s_connection_idx=4u;
static bool s_in_gatt_callback,s_notify_enabled;
struct custs1_val_ntf_ind_req {uint16_t length;uint8_t value[220];};
static struct custs1_val_ntf_ind_req response;
static unsigned replies;
static bool notify_available(void){assert(s_in_gatt_callback);return true;}
static struct custs1_val_ntf_ind_req *notify_allocate(uint16_t n){
    assert(n==3);response.length=n;return &response;
}
static void notify_commit(struct custs1_val_ntf_ind_req *r){assert(r==&response);++replies;}
'''
    harness += '\n'.join(function(transport, n) for n in
                         ('send_v2_response(', 'send_v2_status('))
    harness += '\n' + function(protocol, 'v2_free_only_command_unsupported(')
    harness += '\n' + function(protocol, 'v2_free_only_request_unsupported(')
    harness += '\n' + function(protocol, 'v2_unsupported(')
    harness += '\n' + function(app, 'image_enqueue(') + '\n' + ingress
    harness += f'\n#define INITIAL_QUEUE_COUNT {initial_count}u\n'
    harness += r'''
static void unchanged(const packet_t *before,unsigned count){
    assert(s_image_count==count && s_image_head==0);
    assert(!memcmp(s_image_queue,before,sizeof *before));
}
int main(void){
    /* These are registered omitted facilities, not an unknown-opcode probe.
     * No DA AUTH backend exists; no positive Pro/NFC implementation is linked. */
    const uint8_t omitted[]={INKTAG_CMD_GET_GUI_ASSET_INFO,
        INKTAG_CMD_SET_RESOURCE_ALIAS,INKTAG_CMD_SET_IMAGE_METADATA,
        INKTAG_CMD_GET_INPUT_CONFIG,INKTAG_CMD_SET_INPUT_CONFIG,
        INKTAG_CMD_GET_INDICATOR_CONFIG,INKTAG_CMD_SET_INDICATOR_CONFIG,
        INKTAG_CMD_GET_NFC_CAPABILITIES,INKTAG_CMD_GET_NFC_STATUS,
        INKTAG_CMD_GET_NFC_NDEF,INKTAG_CMD_SET_NFC_NDEF,
        INKTAG_CMD_GET_LAYOUT_CAPABILITIES,INKTAG_CMD_VALIDATE_LAYOUT,
        INKTAG_CMD_ACTIVATE_LAYOUT,INKTAG_CMD_GET_LAYOUT_STATUS,
        INKTAG_CMD_SET_ACTIVATION_CODE,INKTAG_CMD_GET_SECURITY_INFO,
        INKTAG_CMD_GET_ACTIVATION_REQUEST,INKTAG_CMD_GET_OWNER_CHALLENGE,
        INKTAG_CMD_AUTHORIZE_OWNER_ACTION,INKTAG_CMD_GET_DEVICE_PASSWORD_STATUS,
        INKTAG_CMD_GET_DEVICE_PASSWORD_CHALLENGE,INKTAG_CMD_AUTHORIZE_DEVICE_PASSWORD,
        INKTAG_CMD_SHOW_PASSWORD_DEVICE_INFO,
        INKTAG_CMD_GET_FIND_CAPABILITIES,INKTAG_CMD_GET_FIND_CONFIG,
        INKTAG_CMD_SET_FIND_CONFIG,INKTAG_CMD_FIND_ACTION,INKTAG_CMD_GET_FIND_OWNER_STATUS,
        INKTAG_CMD_BEGIN_FIND_OWNER_ROTATION,INKTAG_CMD_STAGE_FIND_OWNER_ROTATION,
        INKTAG_CMD_COMMIT_FIND_OWNER_ROTATION,INKTAG_CMD_ABORT_FIND_OWNER_ROTATION,
        INKTAG_CMD_OPEN_DEBUG_SESSION,INKTAG_CMD_DEBUG_EXECUTE,INKTAG_CMD_DEBUG_READ,
        INKTAG_CMD_CLOSE_DEBUG_SESSION,INKTAG_CMD_ABORT_DEBUG_OPERATION};
    uint8_t wire[221]={0,39,0x10};
    for(unsigned count=INITIAL_QUEUE_COUNT;count<=INITIAL_QUEUE_COUNT;count++)
      for(unsigned i=0;i<sizeof omitted;i++)
      for(unsigned flags=0;flags<256;flags++)for(unsigned n=0;n<=221;n++){
        memset(s_image_queue,0xA5,sizeof s_image_queue);
        packet_t before=s_image_queue[0];s_image_count=count;s_image_head=0;
        wire[0]=omitted[i];wire[2]=flags;replies=0;
        inktag_da_write_value(4,1,wire,n);
        assert(!s_in_gatt_callback);
        unchanged(&before,count);
        assert(replies==(n>=2));
        if(n>=2){
            assert(response.length==3 && response.value[0]==wire[0] && response.value[1]==39);
            assert(response.value[2]==((n>=3&&n<=220&&(flags&0xfc)==0x10)
                ?INKTAG_STATUS_UNSUPPORTED:INKTAG_STATUS_INVALID_REQUEST));
        }
      }
    /* Valid omitted system actions also refuse before queue pressure. */
    for(unsigned count=0;count<=1;count++)for(unsigned action=2;action<=3;action++){
        packet_t before=s_image_queue[0];s_image_count=count;s_image_head=0;
        wire[0]=INKTAG_CMD_SYSTEM_ACTION;wire[2]=0x10;wire[3]=action;replies=0;
        inktag_da_write_value(4,1,wire,4);
        assert(replies==1 && response.value[2]==INKTAG_STATUS_UNSUPPORTED);
        unchanged(&before,count);
    }
    /* Custom AUX GPIO, legacy input/indicator settings and synthetic button/
     * buzzer tests are unavailable before queue pressure or any worker work. */
    const uint8_t ops[]={INKTAG_CMD_SET_IO_CONFIG,INKTAG_CMD_SET_SETTINGS,
                        INKTAG_CMD_GET_SETTINGS,INKTAG_CMD_RUN_TEST,INKTAG_CMD_RUN_TEST};
    const uint8_t selectors[]={INKTAG_RELEASE_AUX_GET_SELECTOR,INKTAG_SETTINGS_SECTION_INPUT,
                              INKTAG_SETTINGS_SECTION_INDICATOR,INKTAG_TEST_BUZZER,
                              INKTAG_TEST_INPUT_SIMULATE};
    const uint8_t sizes[]={17,13,4,6,5};
    for(unsigned j=0;j<sizeof ops;j++){
        memset(s_image_queue,0xA5,sizeof s_image_queue);
        packet_t before=s_image_queue[0];s_image_count=INITIAL_QUEUE_COUNT;s_image_head=0;
        wire[0]=ops[j];wire[1]=39;wire[2]=0x10;wire[3]=selectors[j];replies=0;
        inktag_da_write_value(4,1,wire,sizes[j]);
        assert(replies==1 && response.value[2]==INKTAG_STATUS_UNSUPPORTED);
        unchanged(&before,INITIAL_QUEUE_COUNT);
    }
    /* Transaction zero and wrong connection remain parser/connection errors. */
    wire[0]=INKTAG_CMD_GET_NFC_CAPABILITIES;wire[1]=0;replies=0;
    inktag_da_write_value(4,1,wire,3);
    assert(replies==1 && response.value[2]==INKTAG_STATUS_INVALID_REQUEST);
    replies=0;inktag_da_write_value(3,1,wire,3);assert(replies==0);
    /* A supported command retains byte ownership, tag normalization and BUSY. */
    wire[0]=INKTAG_CMD_GET_TIME;wire[1]=39;wire[2]=0x13;
    s_image_count=0;replies=0;
    inktag_da_write_value(4,1,wire,3);
    assert(!replies && s_image_count==1 && s_image_queue[0].data[2]==3 && wire[2]==0x13);
    packet_t before=s_image_queue[0];
    inktag_da_write_value(4,1,wire,3);
    assert(replies==1 && response.value[2]==INKTAG_STATUS_BUSY);
    unchanged(&before,1);
    return 0;
}
'''
    # The classifier is part of the production closure even before the fix,
    # but the red baseline ingress did not yet call it or v2_unsupported.
    source = tmp_path/'actual-unsupported-ingress.c'
    source.write_text(harness, encoding='utf-8')
    compile_run(tmp_path, ['-Icompat/common/epd', '-Wno-unused-function'], [source],
                sanitize=sanitize)
