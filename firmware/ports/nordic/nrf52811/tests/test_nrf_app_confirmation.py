"""Exercise shipping App confirmation, image reporting and Recovery commit.

SDK scheduler, storage and power are explicit host boundaries. This verifies
software ordering, not installed-device settings or peripheral/radio behavior.
"""
from pathlib import Path
import argparse,json,re,subprocess,tempfile,resource
from test_nrf_auto_refresh_continuity import function,BOOT,BOOT_TEST
ROOT=Path(__file__).resolve().parents[1]
PORT=ROOT/'compat/nordic/src'
PREFIX=r"""
#include "inktag_nrf_dfu_epoch.h"
#define INKTAG_NRF_SDK17 1
#define INKTAG_POWER_OPERATION_FLASH 1u
#define NRF_PWR_MGMT_SHUTDOWN_RESET 2u
#define NRF_SUCCESS 0u
#define NRF_ERROR_INTERNAL 3u
typedef unsigned ret_code_t;
static bool power_allowed=true,queued_reset,commit_failure,load_failure;
static unsigned requests,commits,power_checks;
static uint8_t retained;
static struct {uint32_t app_version,bootloader_version;} s_dfu_settings;
static bool inktag_nrf_health_operation_allowed(unsigned op,bool force){
 assert(op==INKTAG_POWER_OPERATION_FLASH && force);++power_checks;return power_allowed;
}
static void nrf_power_gpregret_set(uint8_t value){retained=value;}
static uint8_t nrf_power_gpregret_get(void){return retained;}
static void nrf_pwr_mgmt_shutdown(unsigned type){assert(type==NRF_PWR_MGMT_SHUTDOWN_RESET);queued_reset=true;++requests;}
static ret_code_t inktag_nrf_dfu_settings_init_original(bool irq){
 assert(!irq);s_dfu_settings.app_version=settings_words[2];
 s_dfu_settings.bootloader_version=settings_words[3];return load_failure?NRF_ERROR_INTERNAL:NRF_SUCCESS;
}
static ret_code_t nrf_dfu_settings_write_and_backup(void *callback){
 assert(!callback);++commits;if(commit_failure)return NRF_ERROR_INTERNAL;
 settings_words[2]=s_dfu_settings.app_version;settings_words[3]=s_dfu_settings.bootloader_version;return NRF_SUCCESS;
}
"""
MAIN=r"""
int main(int argc,char **argv){
 assert(argc==2);const char *test=argv[1];
 settings_words[1]=2;settings_words[2]=65738;settings_words[3]=65738;
 settings_words[6]=68856;settings_words[7]=0x6dc12b5e;settings_words[8]=1;
 saved=(boot_display_record_t){.magic=BOOT_DISPLAY_MAGIC,.version=1,.app_size=68856,.app_crc=0x6dc12b5e};present=true;
 bool expected=true,expect_journal_unchanged=false;
 if(!strcmp(test,"missing-record")){present=false;expect_journal_unchanged=true;}
 else if(!strcmp(test,"corrupt-record")){saved.magic^=1;expect_journal_unchanged=true;}
 else if(!strcmp(test,"record-write-failure")){saved.app_crc=123;accept=false;expect_journal_unchanged=true;}
 else if(!strcmp(test,"healthy-record")){}
 else if(!strcmp(test,"low-voltage")){power_allowed=false;expected=false;}
 else if(!strcmp(test,"invalid-bank")){settings_words[8]=0;expected=false;}
 else if(!strcmp(test,"empty-image")){settings_words[6]=0;expected=false;}
 else if(!strcmp(test,"invalid-settings")){settings_words[1]=0;expected=false;}
 else if(!strcmp(test,"commit-write-failure")){commit_failure=true;expected=false;}
 else if(!strcmp(test,"settings-load-failure")){load_failure=true;expected=false;}
 else if(!strcmp(test,"already-confirmed-missing-record")){settings_words[3]=0;present=false;expect_journal_unchanged=true;}
 else assert(false);
 boot_display_record_t before=saved;bool before_present=present;
 bool first=false,ota=false;bool display_context=inktag_nrf_boot_display_context(&first,&ota);
 uint8_t image[8]={0};epd_v2_application_info(image);
 if(settings_words[3])assert(!memcmp(image,(uint8_t[8]){0},8));
 if(queued_reset){
  assert(settings_words[3]==65738 && retained==INKTAG_NRF_DFU_CONFIRM_GPREGRET);
  ret_code_t status=nrf_dfu_settings_init(false);
  assert((status==NRF_SUCCESS)==(!commit_failure && !load_failure));
 }
 memset(image,0,sizeof(image));epd_v2_application_info(image);
 uint32_t size=0,crc=0;memcpy(&size,image,4);memcpy(&crc,image+4,4);
 printf("case=%s display_context=%u reset_requests=%u commits=%u pending=%u size=%u crc=%08x\n",test,display_context,requests,commits,settings_words[3],size,crc);fflush(stdout);
 assert((size==68856 && crc==0x6dc12b5e)==expected);
 if(expect_journal_unchanged){assert(before_present==present && !memcmp(&before,&saved,sizeof(saved)) && !first && !ota);}
 if(!strcmp(test,"already-confirmed-missing-record"))assert(!requests && !commits && !writes);
 else if(expected)assert(requests==1 && commits==1 && !settings_words[3] && !retained);
 if(!power_allowed || !settings_words[1] || !settings_words[6] || !settings_words[8])assert(!requests && !commits);
 if(commit_failure || load_failure)assert(settings_words[3]==65738 && retained==INKTAG_NRF_DFU_CONFIRM_GPREGRET);
 return 0;
}
"""
CASES=['missing-record','corrupt-record','record-write-failure','healthy-record','low-voltage','invalid-bank','empty-image','invalid-settings','commit-write-failure','settings-load-failure','already-confirmed-missing-record']
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--evidence-dir',type=Path);p.add_argument('--case',action='append',choices=CASES);a=p.parse_args()
 config=(PORT/'inktag_nrf_config_store.c').read_text();protocol=(PORT/'inktag_nrf_protocol_v2.inc').read_text();activation=(PORT/'inktag_nrf_dfu_activation.c').read_text()
 typedef=re.search(r'typedef struct \{\s*uint32_t magic;\s*uint8_t version;\s*uint8_t flags;[\s\S]*?} boot_display_record_t;',config).group()
 code=BOOT.replace('#include "inktag_page_cycle.h"','')+typedef+BOOT_TEST+PREFIX+function(config,'inktag_nrf_boot_display_context')+function(protocol,'epd_v2_application_info')+function(activation,'nrf_dfu_settings_init')+MAIN
 # Reaching this call already requires advertising readiness in actual main.
 main_source=function((PORT/'main.c').read_text(),'main');assert main_source.index('advertising_start();')<main_source.index('inktag_nrf_boot_display_context(')<main_source.index('app_sched_execute();')
 resource.setrlimit(resource.RLIMIT_CORE,(0,0))
 with tempfile.TemporaryDirectory() as tmp:
  out=a.evidence_dir or Path(tmp);out.mkdir(parents=True,exist_ok=True);source=out/'confirmation.c';source.write_text(code);steps=[]
  for variant,flags in [('strict',['-O2']),('asan-ubsan',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-sanitize-recover=all','-no-pie'])]:
   exe=out/('confirmation-'+variant);subprocess.run(['cc','-std=c99','-Wall','-Werror',*flags,'-I'+str(PORT),'-I'+str(ROOT/'compat/common/gui'),str(source),'-o',str(exe)],check=True)
   for case in a.case or CASES:
    r=subprocess.run([str(exe),case],capture_output=True,text=True);(out/(variant+'-'+case+'.log')).write_text(r.stdout+r.stderr);steps.append({'variant':variant,'case':case,'exit':r.returncode});(out/'RESULT.json').write_text(json.dumps({'steps':steps},indent=2)+'\n');print(r.stdout,flush=True,end='');assert r.returncode==0,(variant,case,r.returncode,r.stderr)
  print('PASS shipping App confirmation/reporting + Recovery commit: '+str(len(steps))+' cases')
if __name__=='__main__':main()
