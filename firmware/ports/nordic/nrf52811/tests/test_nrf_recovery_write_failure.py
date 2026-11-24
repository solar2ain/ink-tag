"""Execute shipping Flash queue + SDK write/PRN callbacks under queue pressure.

No BLE/hardware access. Keep generated C, binaries and output with --output.
--expect-old records the pre-fix silent-success reproduction, not a pass for
the corrected contract. SDK inputs are rendered through their locked producer.
"""
import argparse
import ast
import hashlib
import json
import os
from pathlib import Path
import re
import runpy
import subprocess
import tempfile

from nrf_sdk_host import _function

ROOT = Path(__file__).resolve().parents[1]


def render(sdk, expect_old):
    projection = runpy.run_path(ROOT / "build-support/sdk/materialize-nordic-dfu-resume.py")
    request_path = sdk / "components/libraries/bootloader/dfu/nrf_dfu_req_handler.c"
    ble_path = sdk / "components/libraries/bootloader/ble_dfu/nrf_dfu_ble.c"
    request = projection["materialize"](request_path)
    ble = projection["materialize"](ble_path)
    queue_path = ROOT / "compat/nordic/src/inktag_nrf_dfu_flash.c"
    queue = queue_path.read_text()
    # Reuse only the existing fake SOC/ADC boundary, not its test execution.
    old_test = (ROOT / "tests/test_nrf_recovery_flash_queue.py").read_text()
    prefix = next(ast.literal_eval(node.value) for node in ast.parse(old_test).body
                  if isinstance(node, ast.Assign)
                  and any(isinstance(t, ast.Name) and t.id == "prefix" for t in node.targets))
    definitions = "\n".join(re.findall(
        r"^#define (?:FLASH_PAGE_SIZE|FLASH_WRITE_CHUNK_BYTES|FLASH_QUEUE_SIZE|FLASH_RETRY_LIMIT) .*$",
        queue, re.M))
    state = queue[queue.index("typedef enum"):queue.index("static bool wait_for_event")]
    functions = "\n".join(_function(queue, declaration) for declaration in (
        "static bool power_operation_allowed(", "bool inktag_nrf_dfu_power_allows_write(",
        "static flash_op_t *current_op(", "static bool range_valid(",
        "static bool application_mutation_allowed(", "static void complete_current(",
        "static void submit_current(void)\n{", "static void flash_soc_event(",
        "ret_code_t nrf_dfu_flash_init(", "static ret_code_t queue_operation(",
        "ret_code_t nrf_dfu_flash_store(", "ret_code_t nrf_dfu_flash_erase(",
        "bool nrf_fstorage_is_busy("))
    header_path = sdk / "components/libraries/bootloader/dfu/nrf_dfu_req_handler.h"
    header = header_path.read_text()
    header = header.split("ANON_UNIONS_ENABLE;", 1)[1].split(
        "/**@brief  Function for initializing the request handling module.", 1)[0]
    declarations = r'''
#include <string.h>
#include "crc32.h"
#include "inktag_recovery_version.h"
#define BOOTLOADER_START_ADDR 0x2A000u
#define INKTAG_NRF_RECOVERY_FLASH_SIZE 0x4000u
#define ASSERT assert
#define NRF_LOG_DEBUG(...) ((void)0)
#define NRF_LOG_WARNING(...) ((void)0)
#define NRF_LOG_ERROR(...) ((void)0)
#define UNUSED_PARAMETER(x) ((void)(x))
#define MAX_RESPONSE_LEN 17
#define RESPONSE_HEADER_LEN 3
'''
    stubs = r'''
static struct {
    struct {
        uint32_t data_object_size,firmware_image_offset;
        uint32_t firmware_image_offset_last,firmware_image_crc;
    } progress;
    uint32_t write_offset;
} s_dfu_settings;
static uint32_t m_firmware_start_addr=0x19000;
static bool init_present=true;
static bool nrf_dfu_validation_init_cmd_present(void) {return init_present;}
static uint16_t m_pkt_notif_target=4,m_pkt_notif_target_cnt=4;
static uint8_t wire[17];
static unsigned wire_length,wire_count,resume_callbacks;
static uint8_t ext_error_get(void) {return 0;}
static unsigned ext_error_set(unsigned code) {(void)code;return 0;}
#define NRF_DFU_EXT_ERROR_NO_ERROR 0
static unsigned response_send(const uint8_t *data,unsigned length) {
    assert(length<=sizeof(wire));memcpy(wire,data,length);
    wire_length=length;wire_count++;return NRF_SUCCESS;
}
static void inktag_nrf_dfu_resume_response_sent(unsigned request,unsigned result,unsigned sent) {
    assert(request<=0xFF&&result<=0xFF&&sent==NRF_SUCCESS);resume_callbacks++;
}
static uint8_t uint32_encode(uint32_t value,uint8_t *out) {
    for(unsigned i=0;i<4;i++)out[i]=(uint8_t)(value>>(8*i));
    return 4;
}
static uint32_t get32(const uint8_t *in) {
    uint32_t out=0;for(unsigned i=0;i<4;i++)out|=(uint32_t)in[i]<<(8*i);return out;
}
'''
    body = prefix + definitions + "\n" + state + declarations + header + stubs
    body += _function((sdk / "components/libraries/crc32/crc32.c").read_text(),
                      "uint32_t crc32_compute(")
    body += "static void submit_current(void);\n" + functions
    for name in ("static uint32_t response_prepare(", "static uint32_t response_select_obj_add(",
                 "static uint32_t response_crc_add(", "static uint32_t response_ext_err_payload_add(",
                 "static void ble_dfu_req_handler_callback("):
        body += "\n" + _function(ble, name)
    body += "\n" + _function(request, "static void on_data_obj_write_request(")
    body += "\n" + _function(request, "static void on_data_obj_crc_request(")
    body += "\n#define EXPECT_OLD %d\n" % expect_old
    body += r'''
static uint32_t packets[32][61]; /* actual244-byte maximum packet buffers */
static unsigned failed_results;
static void reset_case(void) {
    memset(s_queue,0,sizeof(s_queue));s_queue_head=s_queue_count=s_retry_count=0;
    s_operation_accepted=false;s_softdevice_enabled=true;soft_busy=false;allowed=true;voltage=3300;
    writes=erases=callbacks=checks=wire_count=resume_callbacks=failed_results=0;
    wire_length=0;memset(wire,0,sizeof(wire));m_firmware_start_addr=0x19000;
    memset(&s_dfu_settings,0,sizeof(s_dfu_settings));s_dfu_settings.progress.data_object_size=4096;
    init_present=true;m_pkt_notif_target=m_pkt_notif_target_cnt=4;
    for(unsigned i=0;i<32;i++)for(unsigned j=0;j<61;j++)packets[i][j]=i*0x09070503u+j;
}
static nrf_dfu_response_t write_packet(unsigned index,unsigned length) {
    assert(index<32&&length<=244);
    nrf_dfu_request_t req={.request=NRF_DFU_OP_OBJECT_WRITE,
        .p_context=(void*)1,.callback={.write=completed},
        .write={.p_data=(uint8_t*)packets[index],.len=length}};
    /* Exact SDK request dispatcher initializes every response this way. */
    nrf_dfu_response_t res={.request=req.request,.result=NRF_DFU_RES_CODE_SUCCESS};
    on_data_obj_write_request(&req,&res);
    if(res.result!=NRF_DFU_RES_CODE_SUCCESS)failed_results++;
    ble_dfu_req_handler_callback(&res,req.p_context);return res;
}
static void drain(void) {
    soft_busy=false;
    for(unsigned i=0;s_queue_count&&i<128;i++)flash_soc_event(NRF_EVT_FLASH_OPERATION_SUCCESS,NULL);
    assert(!s_queue_count);
}
int main(void) {
    /* PRN is a queued offset, not physical completion. No SOC completion is
     * injected while filling all16 actual queue entries. */
    reset_case();soft_busy=true;
    for(unsigned i=0;i<16;i++) {
        nrf_dfu_response_t r=write_packet(i,20);assert(r.result==NRF_DFU_RES_CODE_SUCCESS);
    }
    assert(s_queue_count==16&&callbacks==0&&wire_count==4&&wire[2]==1);
    assert(get32(wire+3)==320&&s_dfu_settings.write_offset==320);
    unsigned previous_crc=s_dfu_settings.progress.firmware_image_crc;
    unsigned messages=wire_count;
    nrf_dfu_response_t rejected=write_packet(16,20);
    assert(callbacks==1&&last_callback_source==packets[16]);
    assert(s_queue_count==16&&s_dfu_settings.write_offset==320);
    assert(s_dfu_settings.progress.firmware_image_offset==320);
    assert(s_dfu_settings.progress.firmware_image_crc==previous_crc);
    if(EXPECT_OLD) {
        assert(rejected.result==NRF_DFU_RES_CODE_SUCCESS&&wire_count==messages);
        puts("REPRO old: full queue drops one20B write, response remainsSUCCESS, non-PRN callback suppressed");
    } else {
        assert(rejected.result==NRF_DFU_RES_CODE_INSUFFICIENT_RESOURCES);
        assert(wire_count==messages+1&&wire_length==3);
        assert(wire[0]==0x60&&wire[1]==NRF_DFU_OP_CRC_GET&&wire[2]==4);
        puts("PASS full queue: exact standard resource error even outsidePRN; unchangedCRC/offset; free once");
    }
    drain();assert(callbacks==17);

    /* A fresh object/counter after the failed object has no inherited error.
     * Exercise every PRN position, PRN disabled, and varied legal packet sizes. */
    for(unsigned boundary=0;boundary<4;boundary++) {
        reset_case();soft_busy=true;
        for(unsigned i=0;i<16;i++)write_packet(i,20);
        m_pkt_notif_target_cnt=(uint16_t)(4-boundary);
        messages=wire_count;rejected=write_packet(16,20);
        if(!EXPECT_OLD)assert(rejected.result==4&&wire_count==messages+1&&wire[2]==4);
        drain();assert(callbacks==17);
    }
    reset_case();soft_busy=true;m_pkt_notif_target=m_pkt_notif_target_cnt=0;
    for(unsigned i=0;i<16;i++)write_packet(i,20);
    assert(wire_count==0);rejected=write_packet(16,20);
    if(!EXPECT_OLD)assert(rejected.result==4&&wire_count==1&&wire_length==3&&wire[2]==4);
    drain();
    for(unsigned size=20;size<=244;size+=4) {
        reset_case();uint32_t reference=0;
        for(unsigned i=0;i<8;i++) {
            unsigned prior=wire_count;
            nrf_dfu_response_t r=write_packet(i,size);
            assert(r.result==1);reference=crc32_compute((uint8_t*)packets[i],size,&reference);
            if((i+1)%4==0) {
                assert(wire_count==prior+1&&wire_length==11&&wire[1]==3&&wire[2]==1);
                assert(get32(wire+3)==(i+1)*size&&get32(wire+7)==reference);
            } else assert(wire_count==prior);
            drain();
        }
        assert(callbacks==8&&failed_results==0);
        nrf_dfu_request_t req={0};nrf_dfu_response_t r={.request=3,.result=1};
        on_data_obj_crc_request(&req,&r);assert(r.crc.offset==8*size&&r.crc.crc==reference);
        ble_dfu_req_handler_callback(&r,(void*)1);assert(wire_length==11&&get32(wire+3)==8*size);
    }
    /* Non-resource physical admission failures cannot be reclassified as a
     * successful packet or advance the stream. Same callback is released once. */
    for(unsigned reason=0;reason<3;reason++) {
        reset_case();
        if(reason==0)allowed=false;
        if(reason==1)m_firmware_start_addr=0x30000;
        if(reason==2)s_dfu_settings.write_offset=1;
        rejected=write_packet(0,20);
        assert(!s_queue_count&&callbacks==1&&s_dfu_settings.progress.firmware_image_offset==0);
        if(!EXPECT_OLD)assert(rejected.result==10&&wire_count==1&&wire_length==3&&wire[2]==10);
    }
    reset_case();init_present=false;rejected=write_packet(0,20);
    assert(rejected.result==8&&s_dfu_settings.progress.firmware_image_offset==0);
    if(!EXPECT_OLD)assert(wire_count==1&&wire[2]==8);
    puts("PASS normal20..244B PRN/CRC, disabledPRN failures, allPRN positions, power/range/alignment failures, init rejection");
    return 0;
}
'''
    bindings = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in
                (request_path, ble_path, header_path, queue_path,
                 ROOT / "build-support/sdk/materialize-nordic-dfu-resume.py",
                 ROOT / "build-support/sdk/materialize-nordic-dfu-request.py",
                 ROOT / "compat/common/ota/inktag_recovery_version.h")}
    return body, bindings


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    primary = runpy.run_path(ROOT / "build-support/sdk/primary-checkout.py")["primary_checkout"](ROOT)
    parser.add_argument("--sdk-root", type=Path, default=Path(os.environ.get(
        "INKTAG_NRF_SDK_ROOT", primary / ".sdk/nordic/nRF5_SDK_17.1.0_ddde560")))
    parser.add_argument("--output", type=Path)
    parser.add_argument("--expect-old", action="store_true")
    args = parser.parse_args()
    temporary = tempfile.TemporaryDirectory() if args.output is None else None
    output = Path(temporary.name) if temporary else args.output
    output.mkdir(parents=True, exist_ok=True)
    body, bindings = render(args.sdk_root, args.expect_old)
    unit = output / "shipping-write-failure.c"
    unit.write_text(body)
    (output / "input-binding.json").write_text(json.dumps(bindings, indent=2) + "\n")
    for name, flags in (("normal", ["-O2"]), ("sanitizers", ["-O1", "-g",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            "-fno-sanitize-recover=all", "-no-pie"])):
        command = ["gcc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                   "-Wno-pointer-to-int-cast", "-Wno-int-to-pointer-cast", *flags,
                   "-I" + str(ROOT / "compat/common/power"),
                   "-I" + str(ROOT / "compat/common/ota"),
                   "-I" + str(args.sdk_root / "components/libraries/crc32"),
                   str(unit), str(ROOT / "compat/common/power/inktag_power.c"),
                   "-o", str(output / name)]
        (output / (name + "-argv.json")).write_text(json.dumps(command, indent=2) + "\n")
        subprocess.run(command, check=True)
        subprocess.run([str(output / name)], check=True)
    if temporary:
        temporary.cleanup()


if __name__ == "__main__":
    main()
