"""Run the shipping startup order with real health initialization and no ADC.

SDK, journal reads and GAP/services are counted host boundaries. This is not
radio, peripheral-concurrency or complete boot-resource qualification.
"""
import ast
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PORT = ROOT / 'compat/nordic/src'


def function(source, name):
    match = re.search(r'^(?:static )?[^\n]+\b' + name + r'\([^;{]*\)\s*\{', source, re.M)
    assert match, name
    depth, end = 1, match.end()
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


# Reuse mock boundaries only; never import or run the other test suite here.
tree = ast.parse((ROOT/'tests/test_nrf_health_diagnostics.py').read_text())
prefix = next(ast.literal_eval(node.value) for node in tree.body
              if isinstance(node, ast.Assign) and any(
                  isinstance(t, ast.Name) and t.id == 'PREFIX' for t in node.targets))
health = (PORT/'inktag_nrf_health.c').read_text()
health = health[:health.index('/* The naked handler')]
health = re.sub(r'^#include "(?:inktag_epd_driver|nrf|nrf_error|nrf_soc)\.h"\n', '', health, flags=re.M)
startup = function((PORT/'main.c').read_text(), 'main')
startup = startup[:startup.index('    for (;;) {')] + '    return boot_display_pending ? 0 : 2;\n}\n'
startup = startup.replace('int main(void)', 'static int shipping_startup(void)', 1)
config_init = function((PORT/'inktag_nrf_native_v1.inc').read_text(), 'epd_config_init')
guard = (PORT/'inktag_nrf_buttonless_guard.c').read_text()
guard = re.sub(r'^#include[^\n]*\n', '', guard, flags=re.M)

boundaries = r'''
#include <setjmp.h>
#include "inktag_epd_config.h"
typedef struct { unsigned reload_value; } nrf_drv_wdt_config_t;
#define NRF_DRV_WDT_DEAFULT_CONFIG {0}
#define NRF_LOG_DEBUG(...) ((void)0)
#define INKTAG_NRF_SDK17 1
#define INKTAG_DEVICE_SETTINGS_ENABLED 1
#define DFU_OP_ENTER_BOOTLOADER 1
#define DFU_OP_SET_ADV_NAME 2
#define DFU_RSP_OPERATION_FAILED 4
typedef unsigned ble_dfu_buttonless_op_code_t;
typedef struct { unsigned len; uint8_t data[2]; } ble_gatts_evt_write_t;
static unsigned forwarded, refused;
static void inktag_nrf_sdk_buttonless_on_ctrl_pt_write(ble_gatts_evt_write_t const *p) {
    assert(p); forwarded++;
}
static unsigned ble_dfu_buttonless_resp_send(unsigned op, unsigned response) {
    assert(op == 1 || op == 2); assert(response == 4); refused++; return 0;
}
static struct { uint32_t RESETREAS; } power_registers;
#define NRF_POWER (&power_registers)
static uint32_t m_resetreas, m_timestamp, m_time_checkpoint, m_wdt_channel_id;
static bool m_time_valid, m_time_checkpoint_valid;
static ble_epd_t m_epd;
static unsigned advertising, journal_reads, gap, service, stack, timer_started;
static bool route_error;
static jmp_buf rejected;
#define APP_ERROR_CHECK(x) do { if ((x) != 0) longjmp(rejected, 1); } while (0)
static void wdt_event_handler(void) {}
static unsigned nrf_drv_wdt_init(nrf_drv_wdt_config_t *cfg, void (*handler)(void)) {
    assert(cfg->reload_value == 60000 && handler == wdt_event_handler); return 0;
}
static unsigned nrf_drv_wdt_channel_alloc(uint32_t *channel) { *channel=0; return 0; }
static void nrf_drv_wdt_enable(void) {}
static void log_init(void) {}
static void timers_init(void) {}
static void power_management_init(void) {}
static void ble_stack_init(void) { stack++; }
static void scheduler_init(void) {}
static void gap_params_init(void) { assert(stack && journal_reads); gap++; }
static void ble_dfu_buttonless_async_svci_init(void) {}
static void services_init(void) { assert(gap); APP_ERROR_CHECK(route_error); service++; }
bool epd_time_read(uint32_t *value) { (void)value; return false; }
static void advertising_init(void) { assert(service); }
static void application_timers_start(void) { timer_started++; }
static void advertising_start(void) { assert(service && timer_started); advertising++; }
void inktag_nrf_startup_ready_begin(void) {}
static bool inktag_nrf_boot_display_context(bool *first, bool *ota) {
    assert(advertising); *first=false; *ota=false; return true;
}
'''

boundaries += r'''
static bool inktag_nrf_config_store_init(void) {
    assert(s_initialized); journal_reads++; return true;
}
'''
fixture = r'''
int main(void) {
    const uint16_t unavailable[] = {0,1499,6001};
    for (unsigned n=0; n<sizeof(unavailable)/sizeof(unavailable[0]); n++) {
        voltage=unavailable[n]; s_initialized=false;
        advertising=journal_reads=gap=service=stack=timer_started=0;
        route_error=false;
        assert(shipping_startup()==0 && advertising==1 && journal_reads==1);
        assert(!inktag_nrf_health_voltage_valid());
        assert(inktag_nrf_health_power_state()==INKTAG_POWER_UNKNOWN);
        for (unsigned op=0; op<3; op++) assert(inktag_nrf_health_operation_allowed(op,true));
        ble_gatts_evt_write_t write={1,{DFU_OP_ENTER_BOOTLOADER,0}};
        forwarded=refused=0; ble_dfu_buttonless_on_ctrl_pt_write(&write);
        write.data[0]=DFU_OP_SET_ADV_NAME; ble_dfu_buttonless_on_ctrl_pt_write(&write);
        assert(forwarded==2 && refused==0);
        assert(!inktag_nrf_health_voltage_valid());
    }
    voltage=2599; forwarded=refused=0;
    ble_gatts_evt_write_t write={1,{DFU_OP_ENTER_BOOTLOADER,0}};
    ble_dfu_buttonless_on_ctrl_pt_write(&write); assert(refused==1 && !forwarded);
    voltage=2600;ble_dfu_buttonless_on_ctrl_pt_write(&write);assert(forwarded==1);
    /* Unavailable ADC never grants a bad hardware route or malformed command. */
    voltage=0; advertising=journal_reads=gap=service=stack=timer_started=0;
    route_error=true;
    if (!setjmp(rejected)) { (void)shipping_startup(); assert(!"bad route accepted"); }
    assert(advertising==0 && gap==1);
    assert(!inktag_nrf_health_operation_allowed((inktag_power_operation_t)99,true));
    puts("PASS shipping startup reaches services/advertising with UNKNOWN ADC; native buttonless admission and valid-low/bad-route guards");
}
'''

with tempfile.TemporaryDirectory() as temporary:
    directory = Path(temporary)
    unit = directory/'boot.c'
    unit.write_text(prefix+health+boundaries+config_init+guard+startup+fixture)
    for name, flags in [('normal',['-O2']), ('sanitizers',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie'])]:
        binary = directory/name
        subprocess.run(['gcc','-std=c99',*flags,'-Wall','-Werror','-Wno-unused-function','-Wno-unused-variable','-DNRF52811_XXAA',
                        '-Itests/mocks/nrf_health','-Ifirmware/ports/nordic/common/src','-Ifirmware/common/epd',
                        '-Ifirmware/common/ble','-Ifirmware/common/input','-Ifirmware/common/protocol',
                        '-Ifirmware/common/storage','-Ifirmware/common/power','-Ifirmware/common/health',
                        '-Ifirmware/common/status','-Ifirmware/ports/nordic/common/config',
                        '-Ifirmware/boards',str(unit),'compat/common/power/inktag_power.c',
                        'compat/common/protocol/inktag_protocol_core.c','compat/common/status/inktag_status.c',
                        '-o',str(binary)],cwd=ROOT,check=True)
        subprocess.run([str(binary)],check=True)
