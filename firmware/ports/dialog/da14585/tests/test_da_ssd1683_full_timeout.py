"""Exercise the real SSD sequencer and DA foreground BUSY deadline."""
import re
import pytest
from da_host_helpers import ROOT, compile_run, function


@pytest.mark.parametrize("debug", [False, True])
@pytest.mark.parametrize("dialog", [False, True])
def test_full_sequence_and_actual_da_foreground_wait(tmp_path, debug, dialog):
    ssd = (ROOT / "compat/common/epd/inktag_epd_ssd.c").read_text(encoding="utf-8")
    app = (ROOT / "compat/dialog/src/user_custs1_impl.c").read_text(encoding="utf-8")
    deferred = ssd.replace("bool SSD1xxx_RefreshDeferred(", "static bool SSD1xxx_RefreshDeferred(", 1)
    functions = "\n".join(function(ssd, n) for n in (
        "SSD1xxx_WaitBusy(", "SSD1680_WaitBusy(", "SSD1619_SmallWaitBusy(",
        "SSD1xxx_Update(", "SSD1xxx_IsSSD1683("))
    functions += "\n" + function(deferred, "SSD1xxx_RefreshDeferred(")
    functions += "\n" + function(ssd, "SSD1xxx_Refresh(")
    constants = "\n".join(re.findall(r"^#define INKTAG_DA_REFRESH_(?:ASSERT|RELEASE)_TIMEOUT_MS[^\n]*", app, re.M))
    unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
enum {COLOR_BW=1,COLOR_BWR=2,COLOR_GRAY4=3,DRV_IC_SSD1680=1,
 DRV_IC_SSD1683=2,DRV_IC_SSD1619=3,SSD1xxx_MASTER_ACTIVATE=0x20,
 SSD1xxx_DISP_CTRL1=0x21,SSD1xxx_DISP_CTRL2=0x22,
 EPD_SCREEN_DRIVER_SSD1683=0x24};
typedef struct model model;
typedef struct {bool (*read_busy)(model *);} driver;
struct model {unsigned ic,color,width,height,driver_id;driver *drv;};
typedef model epd_model_t;
static uint32_t tick,duration,assert_at;
static unsigned last_assert,last_release,activations,controls,temp_reads,restores,sequence;
static bool candidate,abort_now;
static bool SSD1xxx_IsDaA33(const model *p){(void)p;return candidate;}
static bool SSD1xxx_IsSSD1675_154(model *p){(void)p;return false;}
static bool SSD1xxx_IsHanshowNowa154(model *p){(void)p;return false;}
static bool SSD1680_Is213Bwr(model *p){(void)p;return false;}
static bool SSD1677_Is426(model *p){(void)p;return false;}
static bool SSD1619_IsSmall213(model *p){return p->width<=128;}
static unsigned SSD1675_154_StreamY(unsigned y){return y;}
static void SSD1675_154_SetPointer(model *p,unsigned y){(void)p;(void)y;}
static void SSD1xxx_SetWindow(model *p,unsigned x,unsigned y,unsigned w,unsigned h){
 (void)p;(void)x;(void)y;(void)w;(void)h;++restores;
}
static int SSD1xxx_ReadTemp(model *p){(void)p;++temp_reads;return 22;}
static void EPD_WriteCmd(unsigned c){assert(c==0x20||c==0x83);if(c==0x20)++activations;}
static void reg(unsigned c,const uint8_t *p,unsigned n){
 if(c==0x21){assert(n==2);++controls;}else{assert(c==0x22&&n==1);sequence=p[0];}
}
#define EPD_Write(c,...) reg(c,(uint8_t[]){__VA_ARGS__},sizeof((uint8_t[]){__VA_ARGS__}))
#if TEST_DEBUG
static void log_message(const char *fmt,...){(void)fmt;}
#define EPD_DEBUG(...) log_message(__VA_ARGS__)
#else
#define EPD_DEBUG(...) do{}while(0)
#endif
static bool EPD_WaitBusyEdge(bool level,uint16_t a,uint16_t b){
 assert(level);last_assert=a;last_release=b;tick=duration<b?duration:b;return duration<b;
}
static void EPD_WaitBusy(bool level,uint16_t t){(void)EPD_WaitBusyEdge(level,t<500?t:500,t);}
@@SEQUENCER@@
static bool s_refresh_waiting,s_refresh_seen_busy;
static uint32_t s_refresh_assert_ms,s_refresh_busy_ms;
static unsigned s_active_work_type=9,s_active_work_arg=7;
static model *s_epd;
static unsigned finishes,finish_tick,window_finishes;
static bool succeeded;
static void app_feed_wdt(void){}
static bool epd_abort_requested(void){return abort_now;}
static bool EPD_AbortRequested(void){return false;}
static uint32_t refresh_elapsed_ms(void){return tick;}
static bool read_busy(model *p){(void)p;return tick>=assert_at&&tick<duration;}
static void SSD1xxx_FinishDeferredRefresh(model *p){assert(p==s_epd);++window_finishes;}
static void finish_epd_work(unsigned type,unsigned arg,bool released,bool ok){
 assert(type==9&&arg==7&&released);++finishes;succeeded=ok;finish_tick=tick;s_refresh_waiting=false;
}
@@CONSTANTS@@
@@WORKER@@
static void wait_case(model *p,uint32_t end,uint32_t assertion,bool ok,uint32_t terminal){
 s_epd=p;s_refresh_waiting=true;s_refresh_seen_busy=false;
 s_refresh_assert_ms=s_refresh_busy_ms=0;finishes=window_finishes=0;
 abort_now=false;duration=end;assert_at=assertion;
 for(tick=0;tick<=terminal&&s_refresh_waiting;tick++)process_refresh_wait();
 assert(finishes==1&&succeeded==ok&&finish_tick==terminal);
 assert(window_finishes==(ok?1u:0u));
}
int main(void){
 driver d={read_busy};model m={DRV_IC_SSD1683,COLOR_BWR,400,300,EPD_SCREEN_DRIVER_SSD1683,&d};
 for(unsigned color=COLOR_BW;color<=COLOR_BWR;color++){
  m.color=color;static const uint32_t busy[]={35000,65500,70000};
  for(unsigned i=0;i<3;i++){
   tick=0;duration=busy[i];activations=controls=temp_reads=restores=0;last_release=0;
   SSD1xxx_Refresh(&m);
   assert(activations==1&&controls==1&&sequence==0xf7&&!temp_reads);
#if defined(INKTAG_DIALOG)
   assert(tick==0&&last_release==0&&restores==0); /* Native worker owns the wait. */
#else
   assert(last_assert==1000&&last_release==65535&&restores==1);
   assert(tick==(duration<65535?duration:65535));
#endif
  }
 }
 /* Execute the unchanged production DA owner, not a synchronous wait substitute. */
#if defined(INKTAG_DIALOG)
 wait_case(&m,35000,50,true,35000);
 wait_case(&m,65500,50,true,65500);
 wait_case(&m,70000,50,false,65535);
 wait_case(&m,70000,2000,false,1000); /* No assertion remains bounded. */
 candidate=true;m.ic=DRV_IC_SSD1619;m.driver_id=0x20;
 wait_case(&m,44999,50,true,44999);wait_case(&m,46000,50,false,45000);
 candidate=false; /* Non-SSD deferred drivers retain their own 210s/3s limits. */
 wait_case(&m,210001,50,false,210000);wait_case(&m,210001,4000,false,3000);
 s_refresh_waiting=true;abort_now=true;finishes=0;tick=123;process_refresh_wait();
 assert(finishes==1&&!succeeded&&finish_tick==123);
#endif
 /* Other generic fulls retain their existing debug-triggered temperature read. */
 candidate=false;tick=0;duration=100;temp_reads=0;m.width=400;m.ic=DRV_IC_SSD1619;
 SSD1xxx_Refresh(&m);assert(temp_reads==TEST_DEBUG);
 return 0;
}
'''
    worker = function(app, "process_refresh_wait(")
    unit = unit.replace("@@SEQUENCER@@", functions).replace("@@CONSTANTS@@", constants).replace("@@WORKER@@", worker)
    path = tmp_path / "ssd1683_full.c"
    path.write_text(unit, encoding="utf-8")
    args = ["-DINKTAG_EPD_ENABLE_SSD1683=1", f"-DTEST_DEBUG={int(debug)}", "-Wno-unused-function"]
    if dialog: args += ["-DINKTAG_DIALOG=1"]
    compile_run(tmp_path, args, [path], sanitize=True)
