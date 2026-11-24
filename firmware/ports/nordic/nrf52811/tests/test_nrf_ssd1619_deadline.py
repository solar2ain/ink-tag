"""Compile shipping SSD1619 refresh/BUSY/completion C with simulated GPIO time.

The actual small and generic large refresh, legacy release waiter, edge waiter,
diagnostic reset/getters and service completion predicate are compiled. GPIO,
SPI, clock, reset response and temperature bytes are host sinks. This does not
qualify electrical behavior, a large physical panel, the whole dispatcher or
notification/persistence side effects. Old release-only C is a red control.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
INPUTS = ['compat/common/epd/inktag_epd_ssd.c', 'compat/common/epd/inktag_epd_driver.c',
          'compat/common/epd/inktag_epd_driver.h', 'compat/common/epd/inktag_epd_service.c',
          'compat/common/epd/inktag_protocol_v2.h',
          '../../../boards/nrf52811_aesl.h',
          'tests/test_nrf_ssd1619_deadline.py']
INCLUDES = ['compat/common/epd', 'compat/common/gui', 'compat/common/ble',
            'compat/common/input', 'compat/common/protocol']


def function(source, name):
    match = re.search(r'^(?:static )?[^\n;{}]*\b' + name +
                      r'\([^;{]*\)\s*\{', source, re.M)
    if not match:
        raise ValueError('Missing shipping C function: ' + name)
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "inktag_epd_driver.h"
#include "inktag_protocol_v2.h"
#undef EPD_DEBUG
/* Exercise argument evaluation too: debug logging must not add a temperature
 * activation before an edge-qualified refresh. */
static void test_debug(const char *fmt,...) {(void)fmt;}
#define EPD_DEBUG(...) test_debug(__VA_ARGS__)
#define EPD_BUSY_TRACE(x) ((void)0)
#define EPD_RECORD_BUSY_FAILURE(x) ((void)0)
#define EPD_WAIT_SERVICE_MASK 63u
#define HIGH 1
#define LOW 0
#define EPD_RST_PIN 4u
#define EPD_LED_PIN 255u
static uint32_t elapsed,assert_at,release_at,cancel_at,refresh_start;
static uint32_t refresh_delay,refresh_active,m_busy_diag_assert_delay_ms,m_busy_diag_active_ms;
static bool m_busy_diag_asserted,cancelled,m_epd_cancel_requested;
static unsigned commands[64],command_count,data_bytes[64],data_count,window_count;
static unsigned temp_reads,temp_activations,reset_edges,refresh_launches,maintenance;
static uint8_t last_command,update_seq;
static void (*m_maintenance_callback)(void);
static void arm(uint32_t delay_ms,uint32_t duration) {
 assert_at=delay_ms==UINT32_MAX?UINT32_MAX:elapsed+delay_ms;
 release_at=duration==UINT32_MAX?UINT32_MAX:assert_at+duration;
}
void app_feed_wdt(void) {maintenance++;}
void delay(uint32_t n) {elapsed+=n;if(elapsed>=cancel_at)cancelled=true;}
bool EPD_ReadBusy(void) {return elapsed>=assert_at&&elapsed<release_at;}
bool EPD_AbortRequested(void) {return cancelled;}
uint32_t digitalRead(uint32_t p) {(void)p;return HIGH;}
void digitalWrite(uint32_t p,uint32_t v) {
 assert(p==EPD_RST_PIN);reset_edges++;
 if(v==LOW)arm(0,25);
}
void EPD_LED_ON(void) {}
void EPD_LED_OFF(void) {}
void EPD_WriteCmd(uint8_t v) {
 assert(command_count<64);commands[command_count++]=v;last_command=v;
 if(v==SSD1xxx_MASTER_ACTIVATE) {
  if(update_seq==0xb1) {temp_activations++;arm(0,7);}
  else {assert(update_seq==0xf7);refresh_launches++;refresh_start=elapsed;
        arm(refresh_delay,refresh_active);}
 }
}
void EPD_WriteData(uint8_t *p,uint8_t n) {
 for(unsigned i=0;i<n;i++) {assert(data_count<64);data_bytes[data_count++]=p[i];}
 if(last_command==SSD1xxx_DISP_CTRL2) {assert(n==1);update_seq=p[0];}
}
void EPD_ReadData(uint8_t *p,uint8_t n) {assert(n==2);p[0]=25;p[1]=0;temp_reads++;}
static bool SSD1xxx_IsSSD1675_154(epd_model_t *p) {(void)p;return false;}
static bool SSD1xxx_IsHanshowNowa154(epd_model_t *p) {(void)p;return false;}
static bool SSD1680_Is213Bwr(epd_model_t *p) {(void)p;return false;}
static bool SSD1xxx_IsSSD1683(epd_model_t *p) {(void)p;return false;}
static bool SSD1677_Is426(epd_model_t *p) {(void)p;return false;}
static int8_t SSD1675_154_ReadTemperature(void) {assert(false);return 0;}
static void SSD1675_154_SetPointer(epd_model_t *p,uint16_t y) {(void)p;(void)y;assert(false);}
static uint16_t SSD1675_154_StreamY(uint16_t y) {return 151u-y;}
static void SSD1680_WaitBusy(epd_model_t *p,uint16_t t) {(void)p;(void)t;assert(false);}
static void SSD1xxx_SetWindow(epd_model_t *p,uint16_t x,uint16_t y,uint16_t w,uint16_t h) {
 assert(!x&&!y&&w==p->width&&h==p->height);window_count++;
}
typedef struct {epd_model_t *epd;} ble_epd_t;
'''

TEST = r'''
static const epd_driver_t fake_driver={.read_busy=SSD1xxx_ReadBusy};
static epd_model_t panel;
static unsigned scenarios;
static void prepare(bool large,uint32_t onset,uint32_t duration,uint32_t cancel,bool pollution) {
 elapsed=0;cancelled=m_epd_cancel_requested=false;cancel_at=UINT32_MAX;
 assert_at=release_at=UINT32_MAX;EPD_BusyDiagReset();
 command_count=data_count=window_count=temp_reads=temp_activations=0;
 reset_edges=refresh_launches=maintenance=0;update_seq=last_command=0;
 memset(&panel,0,sizeof panel);panel.ic=DRV_IC_SSD1619;
 panel.driver_id=EPD_SCREEN_DRIVER_SSD1619;panel.color=COLOR_BWR;panel.drv=&fake_driver;
 panel.panel_recipe=large?0:INKTAG_RECIPE_SSD1619_HEMA_213_BWR;
 panel.width=large?400:128;panel.height=large?300:250;
 if(pollution) {
  /* Real reset and release wait observe an earlier reset BUSY. Then a real
   * init-like edge leaves asserted=true. Final refresh must discard both. */
  EPD_Reset(true,10);EPD_WaitBusy(true,100);assert(reset_edges==3&&m_busy_diag_active_ms>0);
  arm(0,3);assert(EPD_WaitBusyEdge(true,5,20));assert(EPD_BusyDiagAsserted());
 }
 refresh_delay=onset;refresh_active=duration;
 cancel_at=cancel==UINT32_MAX?UINT32_MAX:elapsed+cancel;
}
static void trace(bool large) {
 assert(refresh_launches==1&&window_count==1);
 if(!large) {
  const unsigned cmd[]={0x21,0x22,0x20,0x83};const unsigned data[]={0x80,0,0xf7};
  assert(command_count==4&&data_count==3);
  assert(memcmp(commands,cmd,sizeof cmd)==0&&memcmp(data_bytes,data,sizeof data)==0);
  assert(!temp_reads&&!temp_activations);
 } else {
  const unsigned cmd[]={0x21,0x22,0x20};
  const unsigned data[]={0x80,0,0xf7};
  assert(command_count==3&&data_count==3);
  assert(memcmp(commands,cmd,sizeof cmd)==0&&memcmp(data_bytes,data,sizeof data)==0);
  assert(!temp_reads&&!temp_activations);
 }
}
static void finish(bool large,inktag_status_v2_t expected) {
 trace(large);
 /* Use the real service terminal predicate for every refresh entry class. */
 const nrf_epd_operation_type_t types[]={NRF_EPD_OP_REFRESH,NRF_EPD_OP_GUI,
                                      NRF_EPD_OP_IMAGE_SHOW,NRF_EPD_OP_CLEAR};
 for(unsigned i=0;i<sizeof types/sizeof types[0];i++)
  assert(shipping_completion(&panel,types[i],1)==expected);
 scenarios++;
}
int main(int argc,char **argv) {
 unsigned first=argc>1&&!strcmp(argv[1],"large-only")?1:0;
 for(unsigned large=first;large<2;large++) {
  uint32_t budget=large?UINT16_MAX:INKTAG_EPD_SSD1619_SMALL_REFRESH_TIMEOUT_MS;
  prepare(large,UINT32_MAX,UINT32_MAX,UINT32_MAX,false);
  SSD1xxx_Refresh(&panel);
  /* This is the observed old-SPI false-DONE case. Old release-only + old
   * predicate must fail here, rather than pass by fixture text matching. */
  finish(large,INKTAG_STATUS_TIMEOUT);
  assert(elapsed-refresh_start==1000&&!EPD_BusyDiagAsserted()&&!EPD_BusyDiagActiveMs());
  prepare(large,50,12,UINT32_MAX,false);SSD1xxx_Refresh(&panel);finish(large,INKTAG_STATUS_OK);
  assert(elapsed-refresh_start==62&&EPD_BusyDiagAssertDelayMs()==50&&EPD_BusyDiagActiveMs()==12);
  prepare(large,1000,3,UINT32_MAX,false);SSD1xxx_Refresh(&panel);finish(large,INKTAG_STATUS_OK);
  assert(elapsed-refresh_start==1003&&EPD_BusyDiagAssertDelayMs()==1000);
  prepare(large,1001,3,UINT32_MAX,false);SSD1xxx_Refresh(&panel);finish(large,INKTAG_STATUS_TIMEOUT);
  assert(elapsed-refresh_start==1000&&!EPD_BusyDiagAsserted());
  const uint32_t durations[]={0,1,29999,30000,30001,45000,60000,65535,65536,UINT32_MAX};
  for(unsigned i=0;i<sizeof durations/sizeof durations[0];i++) {
   uint32_t active=durations[i];prepare(large,0,active,UINT32_MAX,false);SSD1xxx_Refresh(&panel);
   finish(large,active&&active<=budget?INKTAG_STATUS_OK:INKTAG_STATUS_TIMEOUT);
   assert(elapsed-refresh_start==(!active?1000:(active<budget?active:budget)));
   assert(EPD_BusyDiagAsserted()==(active!=0));
   if(active)assert(EPD_BusyDiagActiveMs()==(active<budget?active:budget));
  }
  prepare(large,100,10,20,false);SSD1xxx_Refresh(&panel);finish(large,INKTAG_STATUS_CANCELLED);
  assert(cancelled&&elapsed-refresh_start>=20&&elapsed-refresh_start<=84);
  prepare(large,0,UINT32_MAX,100,false);SSD1xxx_Refresh(&panel);finish(large,INKTAG_STATUS_CANCELLED);
  assert(cancelled&&elapsed-refresh_start>=100&&elapsed-refresh_start<=164&&EPD_ReadBusy());
  prepare(large,UINT32_MAX,UINT32_MAX,UINT32_MAX,true);
  assert(EPD_BusyDiagAsserted());SSD1xxx_Refresh(&panel);finish(large,INKTAG_STATUS_TIMEOUT);
  assert(!EPD_BusyDiagAsserted()&&EPD_BusyDiagAssertDelayMs()==1000&&!EPD_BusyDiagActiveMs());
  prepare(large,7,9,UINT32_MAX,true);SSD1xxx_Refresh(&panel);finish(large,INKTAG_STATUS_OK);
  assert(EPD_BusyDiagAssertDelayMs()==7&&EPD_BusyDiagActiveMs()==9);
 }
 assert(!epd_requires_busy_edge(NULL));
 printf("PASS actual SSD1619 small+large refresh: %u scenarios, real release/assert deadlines, cancellation, stale init/reset/temperature BUSY and terminal predicates\n",scenarios);
 return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence-dir', type=Path)
    args = parser.parse_args()
    if os.name != 'nt':
        import resource
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    sanitized = bool(os.environ.get('INKTAG_TEST_SANITIZERS'))
    def hashes():
        return {p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in INPUTS}
    report = dict(status='running', sanitizers=sanitized, scope=__doc__,
                  sourceInputs=hashes(), steps=[])
    with tempfile.TemporaryDirectory() as temporary:
        output = args.evidence_dir or Path(temporary)
        output.mkdir(parents=True, exist_ok=not bool(args.evidence_dir))
        def run(name, argv, red=False):
            start = datetime.now(timezone.utc).isoformat()
            result = subprocess.run(list(map(str, argv)), cwd=ROOT, capture_output=True)
            data = result.stdout + result.stderr
            log = output/(name+'.log'); log.write_bytes(data)
            ok = result.returncode != 0 and b'Assertion' in data and b'failed' in data if red else result.returncode == 0
            report['steps'].append(dict(name=name, argv=list(map(str, argv)),
                startedUtc=start, completedUtc=datetime.now(timezone.utc).isoformat(),
                exitCode=result.returncode, expected='assertion failure' if red else 'zero',
                passed=ok, log=log.name, logSha256=hashlib.sha256(data).hexdigest()))
            if not ok:
                raise AssertionError(name+'\n'+data.decode('utf-8', errors='replace'))
            print('PASS '+name, flush=True)
        try:
            ssd = (ROOT/INPUTS[0]).read_text(encoding='utf-8')
            driver = (ROOT/INPUTS[1]).read_text(encoding='utf-8')
            service = (ROOT/INPUTS[3]).read_text(encoding='utf-8')
            board = (ROOT/INPUTS[5]).read_text(encoding='utf-8')
            bound = int(re.search(r'#define INKTAG_EPD_SSD1619_SMALL_REFRESH_TIMEOUT_MS (\d+)u', board)[1])
            if bound != 65535:
                raise ValueError('Board release deadline changed; review the bound explicitly')
            dispatch = function(service, 'epd_operation_dispatch')
            start = dispatch.index('    bool cancelled =')
            end = dispatch.index('#if !defined(INKTAG_COMPACT_BUSY_DIAG)', start)
            terminal = dispatch[start:end]
            types = re.search(r'typedef enum \{\s*NRF_EPD_OP_NONE[\s\S]*?} nrf_epd_operation_type_t;', service).group()
            types += '\n'+re.search(r'typedef struct \{\s*ble_epd_t\* p_epd;[\s\S]*?} nrf_epd_operation_t;', service).group()
            completion = '''\nstatic inktag_status_v2_t shipping_completion(epd_model_t *model,nrf_epd_operation_type_t type,uint8_t arg) {
 ble_epd_t state={.epd=model};ble_epd_t *p_epd=&state;
 nrf_epd_operation_t operation={.type=type,.arg=arg};bool started=true,io_failed=false;
'''+terminal+'\n return completion_status;\n}\n'
            driver_c = '\n'.join(function(driver,n) for n in ('epd_reset_phase','EPD_Reset','epd_poll_held','EPD_BusyDiagAsserted',
                'EPD_BusyDiagAssertDelayMs','EPD_BusyDiagActiveMs','EPD_BusyDiagReset','EPD_WaitBusy','EPD_WaitBusyEdge'))
            controller_c = '\n'.join(function(ssd,n) for n in ('SSD1xxx_ReadBusy','SSD1xxx_WaitBusy',
                'SSD1619_IsSmall213','SSD1619_SmallWaitBusy','SSD1xxx_Update','SSD1xxx_ReadTemp','SSD1xxx_Refresh'))
            predicate = function(service, 'epd_requires_busy_edge')
            # Only isolated C copies revert the two new waits and the controller
            # admission predicate. Production files remain untouched.
            old_controller, n = re.subn(r'\(void\)EPD_WaitBusyEdge\(true,\s*1000u?,\s*timeout\);',
                                        'EPD_WaitBusy(true, timeout);', controller_c, count=1)
            old_controller, m = re.subn(r'if \(edge\) \(void\)EPD_WaitBusyEdge\(true,\s*1000u?,\s*timeout\);',
                                        'if (edge) SSD1xxx_WaitBusy(timeout);', old_controller, count=1)
            old_predicate, p = re.subn(r'\s*epd->ic == DRV_IC_SSD1619 \|\|', '', predicate, count=1)
            if (n,m,p) != (1,1,1):
                raise ValueError('Shipping correction shape changed; review old-release control')
            report['shippingCompletionBlock'] = terminal
            options = ['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',
                       '-fno-sanitize-recover=all','-no-pie'] if sanitized else ['-O2']
            for limit in (30000, bound):
                for compact in (False, True):
                    for label, control, pred, red in [('fixed',controller_c,predicate,False),
                        ('old-release-only',old_controller,old_predicate,True)]:
                        name=f'{label}-{limit}-'+('compact' if compact else 'normal')
                        source=output/(name+'.c');binary=output/name
                        source.write_text(PREFIX+'\n'+types+'\n'+driver_c+'\n'+control+'\n'+pred+completion+TEST,encoding='utf-8')
                        run('compile-'+name,[os.environ.get('CC','cc'),'-std=gnu99',
                            '-Werror=implicit-function-declaration',*options,'-DINKTAG_TELINK=1',
                            '-DINKTAG_NRF_PROTOCOL_V2=1','-DINKTAG_NRF_IMAGE_STORE_ENABLED=1',
                            *(['-DINKTAG_COMPACT_BUSY_DIAG=1'] if compact else []),
                            f'-DINKTAG_EPD_SSD1619_SMALL_REFRESH_TIMEOUT_MS={limit}',
                            *['-I'+str(ROOT/q) for q in INCLUDES],source,'-o',binary])
                        run(name,[binary],red=red)
                        if red:
                            # The small red case aborts before the large path.
                            # Start at large explicitly so both old waits are
                            # independently shown to admit a false completion.
                            run(name+'-large',[binary,'large-only'],red=True)
            report['sourceInputsUnchanged'] = report['sourceInputs'] == hashes()
            if not report['sourceInputsUnchanged']:
                raise RuntimeError('Production inputs changed during test; retain and rerun')
            report['status']='passed'
        except Exception:
            report['status']='failed'
            raise
        finally:
            report['completedUtc']=datetime.now(timezone.utc).isoformat()
            (output/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')


if __name__ == '__main__':
    main()
