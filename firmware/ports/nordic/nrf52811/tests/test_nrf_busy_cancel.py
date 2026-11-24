"""Run the shipping BUSY edge waiter in normal and compact diagnostic modes."""
from pathlib import Path
import os,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'compat/common/epd/inktag_epd_driver.c').read_text(encoding='utf-8')
start=source.index('__attribute__((noinline)) bool EPD_WaitBusyEdge(')
end=source.index('{',start)+1;depth=1
while depth:
    if source[end]=='{':depth+=1
    if source[end]=='}':depth-=1
    end+=1
prefix=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define EPD_WAIT_SERVICE_MASK 63u
#define EPD_BUSY_TRACE(x) ((void)0)
#define EPD_RECORD_BUSY_FAILURE(x) ((void)0)
static bool m_busy_diag_asserted,current,cancelled,release_busy,cancel_at_release;
static uint32_t m_busy_diag_assert_delay_ms,delayed,waits;
static void (*m_maintenance_callback)(void);
static void EPD_BusyDiagReset(void){m_busy_diag_asserted=false;m_busy_diag_assert_delay_ms=0;}
static bool EPD_ReadBusy(void){return current;}
static bool EPD_AbortRequested(void){return cancelled;}
static void delay(unsigned n){delayed+=n;}
static void app_feed_wdt(void){}
static void EPD_WaitBusy(bool status,uint16_t timeout){
 (void)timeout;waits++;current=release_busy?!status:status;cancelled|=cancel_at_release;
}
'''
main=r'''
int main(void){
 for(unsigned polarity=0;polarity<2;polarity++)for(unsigned release=0;release<2;release++)
 for(unsigned cancel=0;cancel<2;cancel++){
  current=polarity;release_busy=release;cancel_at_release=cancel;cancelled=false;waits=delayed=0;
  assert(EPD_WaitBusyEdge(polarity,3,20)==(release&&!cancel));assert(waits==1&&!delayed);
 }
 for(unsigned polarity=0;polarity<2;polarity++){
  current=!polarity;cancelled=cancel_at_release=false;waits=delayed=0;
  assert(!EPD_WaitBusyEdge(polarity,3,20));assert(!waits&&delayed==3);
  current=!polarity;cancelled=true;waits=delayed=0;
  assert(!EPD_WaitBusyEdge(polarity,64,20));assert(!waits&&!delayed);
  current=polarity;cancelled=true;release_busy=true;cancel_at_release=false;waits=0;
  assert(!EPD_WaitBusyEdge(polarity,3,20));
 }
}
'''
flags=['-O2'] if not os.environ.get('INKTAG_TEST_SANITIZERS') else ['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-sanitize-recover=all','-no-pie']
with tempfile.TemporaryDirectory() as d:
    p=Path(d);c=p/'wait.c';c.write_text(prefix+source[source.index('static bool epd_poll_held('):source.index('void EPD_WaitBusy(')]+source[start:end]+main,encoding='utf-8')
    for compact in (False,True):
        binary=p/('compact' if compact else 'normal')
        subprocess.run([os.environ.get('CC','cc'),'-std=c99',*flags,*(['-DINKTAG_COMPACT_BUSY_DIAG=1'] if compact else []),str(c),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
print('PASS production BUSY edge cancellation: normal+compact, both polarities, release/stuck/no-edge')
