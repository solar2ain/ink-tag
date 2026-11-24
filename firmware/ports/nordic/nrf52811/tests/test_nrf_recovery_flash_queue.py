"""Exercise production Recovery queue/chunks, lifetimes and per-submit gates."""
from pathlib import Path
import re
import subprocess
import tempfile
import os

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'compat/nordic/src/inktag_nrf_dfu_flash.c').read_text()


def function(name):
    match = re.search(r'^[^\n;{]+?\b' + name + r'\([^;{}]*\)\n\{', source, re.M)
    assert match, name
    start, opening = match.start(), match.end() - 1
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "inktag_power.h"
#define MBR_SIZE 0x1000u
#define BOOTLOADER_SETTINGS_ADDRESS 0x2f000u
#define BOOTLOADER_SETTINGS_PAGE_SIZE 4096u
#define FLASH_END_ADDRESS 0x30000u
#define NRF_SUCCESS 0
#define NRF_ERROR_BUSY 1
#define NRF_ERROR_NO_MEM 2
#define NRF_ERROR_NULL 3
#define NRF_ERROR_INVALID_LENGTH 4
#define NRF_ERROR_INVALID_ADDR 5
#define NRF_ERROR_FORBIDDEN 6
#define NRF_EVT_FLASH_OPERATION_SUCCESS 7
#define NRF_EVT_FLASH_OPERATION_ERROR 8
#define CRITICAL_REGION_ENTER() ((void)0)
#define CRITICAL_REGION_EXIT() ((void)0)
typedef unsigned ret_code_t;
typedef void (*nrf_dfu_flash_callback_t)(void *);
typedef struct {unsigned unused;} nrf_fstorage_t;
static bool allowed=true,soft_busy;
static uint16_t voltage=3300;
static unsigned writes,erases,callbacks,checks,last_words,last_address;
static void *last_callback_source;
static uint16_t sample_vdd_mv(void) {checks++;return allowed?voltage:2300u;}
static unsigned sd_flash_write(uint32_t *dest,const uint32_t *src,unsigned words) {
    assert(src&&words&&words<=128);if(soft_busy)return NRF_ERROR_BUSY;
    writes++;last_address=(uint32_t)(uintptr_t)dest;last_words=words;return 0;
}
static unsigned sd_flash_page_erase(unsigned page) {
    assert(page>=1&&page<48);erases++;return 0;
}
static void nrf_nvmc_write_words(unsigned dest,const void *src,unsigned words) {
    assert(src&&words);writes++;last_address=dest;last_words=words;
}
static void nrf_nvmc_page_erase(unsigned address) {assert(!(address&4095));erases++;}
static void completed(void *src) {callbacks++;last_callback_source=src;}
'''
definitions = '\n'.join(re.findall(
    r'^#define (?:FLASH_PAGE_SIZE|FLASH_WRITE_CHUNK_BYTES|FLASH_QUEUE_SIZE|FLASH_RETRY_LIMIT) .*$',
    source, re.M)) + '\n'
state = source[source.index('typedef enum'):source.index('static bool wait_for_event')]
functions = '\n'.join(function(name) for name in (
    'power_operation_allowed', 'inktag_nrf_dfu_ota_admission_allowed',
    'inktag_nrf_dfu_power_allows_write',
    'current_op', 'range_valid', 'application_mutation_allowed',
    'complete_current', 'submit_current',
    'flash_soc_event', 'nrf_dfu_flash_init', 'queue_operation',
    'nrf_dfu_flash_store', 'nrf_dfu_flash_erase', 'nrf_fstorage_is_busy'))
fixture = r'''
int main(void) {
    uint32_t settings[256]={0},packet[5]={0};
    nrf_dfu_flash_init(true);
    assert(nrf_dfu_flash_store(0x2f000,settings,sizeof(settings),completed)==0);
    assert(writes==1&&last_words==128&&last_address==0x2f000&&callbacks==0);
    assert(nrf_dfu_flash_store(0x19000,packet,sizeof(packet),completed)==0);
    assert(s_queue_count==2&&writes==1);
    flash_soc_event(NRF_EVT_FLASH_OPERATION_SUCCESS,NULL);
    assert(writes==2&&last_words==128&&last_address==0x2f200&&callbacks==0);
    flash_soc_event(NRF_EVT_FLASH_OPERATION_SUCCESS,NULL);
    assert(callbacks==1&&last_callback_source==settings&&writes==3&&last_words==5);
    flash_soc_event(NRF_EVT_FLASH_OPERATION_SUCCESS,NULL);
    assert(callbacks==2&&last_callback_source==packet&&!nrf_fstorage_is_busy(NULL));
    unsigned before=writes;
    allowed=false;
    assert(nrf_dfu_flash_store(0x19000,packet,20,completed)==NRF_ERROR_FORBIDDEN);
    assert(nrf_dfu_flash_erase(0x19000,1,completed)==NRF_ERROR_FORBIDDEN);
    assert(writes==before&&erases==0&&callbacks==2);
    allowed=true;soft_busy=true;
    assert(nrf_dfu_flash_store(0x2f000,settings,1024,completed)==0&&!s_operation_accepted);
    soft_busy=false;flash_soc_event(NRF_EVT_FLASH_OPERATION_SUCCESS,NULL);
    assert(writes==before+1&&s_operation_accepted);
    /* Loss of write authority between chunks never submits the second half. */
    allowed=false;flash_soc_event(NRF_EVT_FLASH_OPERATION_SUCCESS,NULL);
    assert(writes==before+1&&callbacks==3&&!nrf_fstorage_is_busy(NULL));
    allowed=true;
    assert(nrf_dfu_flash_store(0x19001,packet,20,NULL)==NRF_ERROR_INVALID_ADDR);
    assert(nrf_dfu_flash_store(0x2fffc,packet,20,NULL)==NRF_ERROR_INVALID_ADDR);
    assert(nrf_dfu_flash_store(0x19000,packet,19,NULL)==NRF_ERROR_INVALID_LENGTH);
    assert(nrf_dfu_flash_erase(0x19000,2,NULL)==NRF_ERROR_INVALID_LENGTH);
    assert(nrf_dfu_flash_erase(0x19000,1,completed)==0&&erases==1);
    flash_soc_event(NRF_EVT_FLASH_OPERATION_SUCCESS,NULL);
    assert(callbacks==4&&!nrf_fstorage_is_busy(NULL));
    /* Bounded retries retain the buffer until the operation is released. */
    assert(nrf_dfu_flash_store(0x19000,packet,20,completed)==0);
    for(unsigned i=0;i<FLASH_RETRY_LIMIT;i++) {
        flash_soc_event(NRF_EVT_FLASH_OPERATION_ERROR,NULL);assert(callbacks==4);
    }
    flash_soc_event(NRF_EVT_FLASH_OPERATION_ERROR,NULL);
    assert(callbacks==5&&!nrf_fstorage_is_busy(NULL));
    nrf_dfu_flash_init(false);before=writes;
    assert(nrf_dfu_flash_store(0x2f000,settings,1024,completed)==0);
    assert(writes==before+1&&last_words==256&&callbacks==6);
    assert(checks>writes);
    /* Valid low voltage still blocks both native backends. */
    voltage=2399;before=writes;unsigned before_erases=erases;
    assert(!inktag_nrf_dfu_ota_admission_allowed());
    assert(nrf_dfu_flash_store(0x19000,packet,20,completed)==NRF_ERROR_FORBIDDEN);
    assert(nrf_dfu_flash_erase(0x19000,1,completed)==NRF_ERROR_FORBIDDEN);
    assert(writes==before && erases==before_erases);
    voltage=2400;assert(inktag_nrf_dfu_power_allows_write());
    assert(!inktag_nrf_dfu_ota_admission_allowed());
    voltage=2599;assert(!inktag_nrf_dfu_ota_admission_allowed());
    voltage=2600;assert(inktag_nrf_dfu_ota_admission_allowed());
    /* Missing ADC result / rejected range is unavailable telemetry, not an
     * OTA or physical-write veto. Address/alignment checks remain mandatory. */
    const uint16_t unavailable[]={0,1499,6001};
    for(unsigned backend=0;backend<2;backend++) {
        nrf_dfu_flash_init(backend!=0);
        for(unsigned n=0;n<sizeof(unavailable)/sizeof(unavailable[0]);n++) {
            voltage=unavailable[n];before=writes;before_erases=erases;
            assert(inktag_nrf_dfu_ota_admission_allowed());
            assert(nrf_dfu_flash_store(0x19000,packet,20,completed)==NRF_SUCCESS);
            if(backend) flash_soc_event(NRF_EVT_FLASH_OPERATION_SUCCESS,NULL);
            assert(nrf_dfu_flash_erase(0x19000,1,completed)==NRF_SUCCESS);
            if(backend) flash_soc_event(NRF_EVT_FLASH_OPERATION_SUCCESS,NULL);
            assert(writes==before+1 && erases==before_erases+1);
            assert(!nrf_fstorage_is_busy(NULL));
            assert(nrf_dfu_flash_store(0x30000,packet,20,NULL)==NRF_ERROR_INVALID_ADDR);
            assert(nrf_dfu_flash_erase(0x19001,1,NULL)==NRF_ERROR_INVALID_ADDR);
            assert(writes==before+1 && erases==before_erases+1);
        }
    }
    before=writes;
    voltage=3300;
    assert(nrf_dfu_flash_store(0x2f000,settings,sizeof(settings),completed)==NRF_SUCCESS);
    assert(writes==before+1 && nrf_fstorage_is_busy(NULL));
    flash_soc_event(NRF_EVT_FLASH_OPERATION_SUCCESS,NULL);
    assert(writes==before+2 && nrf_fstorage_is_busy(NULL));
    flash_soc_event(NRF_EVT_FLASH_OPERATION_SUCCESS,NULL);
    assert(!nrf_fstorage_is_busy(NULL));
    assert(nrf_dfu_flash_erase(0x19000,1,completed)==NRF_SUCCESS);
    flash_soc_event(NRF_EVT_FLASH_OPERATION_SUCCESS,NULL);
    assert(!nrf_fstorage_is_busy(NULL));
    assert(nrf_dfu_flash_store(0x19001,packet,20,NULL)==NRF_ERROR_INVALID_ADDR);
    assert(nrf_dfu_flash_erase(0x19000,2,NULL)==NRF_ERROR_INVALID_LENGTH);
    puts("PASS Recovery unavailable-ADC OTA admission/NVMC/queue write+erase; valid-low thresholds, chunks, IRQ lifetime and bad-range guards");
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / 'queue.c'
    path.write_text(prefix + definitions + state +
        'static void submit_current(void);\n' + functions + fixture)
    binary = Path(directory) / 'queue'
    flags=['-O2'] if not os.environ.get('INKTAG_TEST_SANITIZERS') else ['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-sanitize-recover=all','-no-pie']
    subprocess.run(['gcc', '-std=c99', *flags, '-Wall', '-Werror',
        '-Wno-pointer-to-int-cast', '-Wno-int-to-pointer-cast',
        '-Ifirmware/common/power', str(path), 'compat/common/power/inktag_power.c',
        '-o', str(binary)], cwd=ROOT, check=True)
    subprocess.run([str(binary)], check=True)
