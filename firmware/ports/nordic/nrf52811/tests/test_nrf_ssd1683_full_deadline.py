"""Execute the shipping InkTag SSD full sequencer and GPIO BUSY waiter.

Debug on/off must preserve the full command sequence and must not introduce
a B1 temperature/OTP operation. This is simulated time, not panel qualification.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import tempfile
from test_nrf_ssd1619_deadline import ROOT, INCLUDES, PREFIX, function

TEST = r'''
int main(void) {
 epd_model_t panel={0};panel.ic=DRV_IC_SSD1683;panel.width=400;panel.height=300;
 const unsigned busy[]={35000,65500,65535,70000};
 for(unsigned color=COLOR_BW;color<=COLOR_BWR;color++) {
  panel.color=color;
  for(unsigned i=0;i<sizeof busy/sizeof busy[0];i++) {
   elapsed=0;cancelled=false;cancel_at=UINT32_MAX;
   assert_at=release_at=UINT32_MAX;
   command_count=data_count=window_count=temp_reads=temp_activations=0;
   refresh_launches=maintenance=0;update_seq=last_command=0;
   refresh_delay=0;refresh_active=busy[i];
   SSD1xxx_Refresh(&panel);
   assert(elapsed==(busy[i]<UINT16_MAX?busy[i]:UINT16_MAX));
   assert(refresh_launches==1&&window_count==1);
   const unsigned cmd[]={0x21,0x22,0x20};
   const unsigned data[]={color==COLOR_BWR?0x00:0x40,0x00,0xf7};
   assert(command_count==3&&data_count==3);
   assert(!memcmp(commands,cmd,sizeof cmd)&&!memcmp(data_bytes,data,sizeof data));
   assert(!temp_reads&&!temp_activations);
   assert(EPD_ReadBusy()==(busy[i]>UINT16_MAX));
   assert(EPD_BusyDiagAsserted()&&EPD_BusyDiagAssertDelayMs()==0);
   assert(EPD_BusyDiagActiveMs()==elapsed);
  }
 }
 puts("PASS shipping SSD1683 full BW/BWR: 35s/65.5s/65535ms success, 70s cutoff; exact commands; no temperature/OTP side effect");
}
'''


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence-dir',type=Path)
    args=parser.parse_args()
    if os.name!='nt':
        import resource
        resource.setrlimit(resource.RLIMIT_CORE,(0,0))
    ssd=(ROOT/'compat/common/epd/inktag_epd_ssd.c').read_text(encoding='utf-8')
    driver=(ROOT/'compat/common/epd/inktag_epd_driver.c').read_text(encoding='utf-8')
    driver_c='\n'.join(function(driver,n) for n in ('epd_reset_phase','EPD_Reset','epd_poll_held',
        'EPD_BusyDiagAsserted','EPD_BusyDiagAssertDelayMs','EPD_BusyDiagActiveMs',
        'EPD_BusyDiagReset','EPD_WaitBusy','EPD_WaitBusyEdge'))
    controller='\n'.join(function(ssd,n) for n in ('SSD1xxx_ReadBusy','SSD1xxx_WaitBusy',
        'SSD1619_IsSmall213','SSD1619_SmallWaitBusy','SSD1xxx_Update','SSD1xxx_ReadTemp','SSD1xxx_Refresh'))
    prefix=PREFIX.replace('static bool SSD1xxx_IsSSD1683(epd_model_t *p) {(void)p;return false;}',
                          'static bool SSD1xxx_IsSSD1683(epd_model_t *p) {return p->ic==DRV_IC_SSD1683;}')
    assert prefix!=PREFIX
    rows=[]
    with tempfile.TemporaryDirectory() as tmp:
        out=args.evidence_dir or Path(tmp)
        out.mkdir(parents=True,exist_ok=not bool(args.evidence_dir))
        modes=[('fixed',controller,False)]
        # Freeze negative controls in this archive; never depend on local Git.
        old_timeout=controller.replace('control = epd->color == COLOR_BWR ? 0x00 : 0x40;\n        timeout = UINT16_MAX;',
                                       'control = epd->color == COLOR_BWR ? 0x00 : 0x40;\n        timeout = 30000;')
        old_debug=controller.replace(' && !edge)',' )')
        assert old_timeout!=controller and old_debug!=controller
        modes += [('old-timeout',old_timeout,True),('old-debug',old_debug,True)]
        sanitized=bool(os.environ.get('INKTAG_TEST_SANITIZERS'))
        flags=['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer'] if sanitized else ['-O2']
        for debug in (False,True):
            p=prefix if debug else prefix.replace('#define EPD_DEBUG(...) test_debug(__VA_ARGS__)',
                                                  '#define EPD_DEBUG(...) ((void)0)')
            for label,body,red in modes:
                if label=='old-debug' and not debug:continue
                name=label+'-debug'+str(int(debug));c=out/(name+'.c');binary=out/name
                c.write_text(p+driver_c+body+TEST,encoding='utf-8')
                command=[os.environ.get('CC','cc'),'-std=gnu99',*flags,'-fno-pie','-no-pie',
                         '-DINKTAG_TELINK=1','-DINKTAG_EPD_SSD1619_SMALL_REFRESH_TIMEOUT_MS=65535',
                         *['-I'+str(ROOT/q) for q in INCLUDES],str(c),'-o',str(binary)]
                compile_result=subprocess.run(command,cwd=ROOT,capture_output=True)
                (out/(name+'-compile.log')).write_bytes(compile_result.stdout+compile_result.stderr)
                assert compile_result.returncode==0,compile_result.stderr.decode(errors='replace')
                result=subprocess.run([str(binary)],capture_output=True)
                log=result.stdout+result.stderr;(out/(name+'.log')).write_bytes(log)
                ok=(result.returncode!=0 and b'Assertion' in log) if red else result.returncode==0
                rows.append({'case':name,'expectedFailure':red,'exit':result.returncode,'passed':ok,
                             'logSha256':hashlib.sha256(log).hexdigest(),'argv':command})
                assert ok,log.decode(errors='replace')
                print('PASS '+name+(' (expected old-code failure)' if red else ''))
        (out/'report.json').write_text(json.dumps({'sanitized':sanitized,'steps':rows},indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':main()
