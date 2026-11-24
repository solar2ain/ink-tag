"""Exercise the production DFU queue with an IRQ at submission return."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def test_completion_irq_before_submit_returns():
    source = (ROOT / 'compat/nordic/src/inktag_nrf_dfu_flash.c').read_text()

    def function(name):
        match = re.search(r'^[^\n;{]+?\b' + name + r'\([^;{}]*\)\n\{', source, re.M)
        assert match, name
        opening = match.end() - 1
        depth, end = 1, opening + 1
        while depth:
            depth += (source[end] == '{') - (source[end] == '}')
            end += 1
        return source[match.start():end]

    prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#define MBR_SIZE 0x1000u
#define FLASH_END_ADDRESS 0x30000u
#define INKTAG_NRF_SAFE_RESUME 0
#define NRF_SUCCESS 0u
#define NRF_ERROR_BUSY 1u
#define NRF_ERROR_NO_MEM 2u
#define NRF_ERROR_NULL 3u
#define NRF_ERROR_INVALID_LENGTH 4u
#define NRF_ERROR_INVALID_ADDR 5u
#define NRF_ERROR_FORBIDDEN 6u
#define NRF_EVT_FLASH_OPERATION_SUCCESS 7u
#define NRF_EVT_FLASH_OPERATION_ERROR 8u
typedef unsigned ret_code_t;
typedef void (*nrf_dfu_flash_callback_t)(void *);
typedef struct { unsigned unused; } nrf_fstorage_t;
static unsigned writes, callbacks, critical_depth;
static bool inject_irq, pending_irq;
static void flash_soc_event(uint32_t event, void *context);
static bool inktag_nrf_dfu_power_allows_write(void) { return true; }
static unsigned sd_flash_write(uint32_t *, const uint32_t *, unsigned);
static unsigned sd_flash_page_erase(unsigned page) { (void)page; return 0; }
static void nrf_nvmc_write_words(unsigned a, const void *b, unsigned c) { (void)a; (void)b; (void)c; }
static void nrf_nvmc_page_erase(unsigned a) { (void)a; }
static void critical_exit(void) {
    assert(critical_depth);
    if (--critical_depth == 0 && pending_irq) {
        pending_irq = false;
        flash_soc_event(NRF_EVT_FLASH_OPERATION_SUCCESS, NULL);
    }
}
#define CRITICAL_REGION_ENTER() (++critical_depth)
#define CRITICAL_REGION_EXIT() critical_exit()
'''
    definitions = '\n'.join(re.findall(
        r'^#define (?:FLASH_PAGE_SIZE|FLASH_WRITE_CHUNK_BYTES|FLASH_QUEUE_SIZE|FLASH_RETRY_LIMIT) .*$',
        source, re.M)) + '\n'
    state = source[source.index('typedef enum'):source.index('static bool wait_for_event')]
    functions = '\n'.join(function(name) for name in (
        'current_op', 'range_valid', 'application_mutation_allowed',
        'complete_current', 'submit_current', 'flash_soc_event',
        'nrf_dfu_flash_init', 'queue_operation', 'nrf_dfu_flash_store',
        'nrf_dfu_flash_erase', 'nrf_fstorage_is_busy'))
    fixture = r'''
static unsigned sd_flash_write(uint32_t *destination, const uint32_t *source, unsigned words) {
    (void)destination; assert(source && words); ++writes;
    /* The SD accepted and completed this operation, but its application IRQ
     * becomes runnable before the caller publishes s_operation_accepted. */
    if (inject_irq) {
        inject_irq = false;
        if (critical_depth) pending_irq = true;
        else flash_soc_event(NRF_EVT_FLASH_OPERATION_SUCCESS, NULL);
    }
    return NRF_SUCCESS;
}
static void released(void *source) { assert(source); ++callbacks; }
int main(void) {
    uint32_t packet[5] = {1,2,3,4,5};
    nrf_dfu_flash_init(true);
    inject_irq = true;
    assert(nrf_dfu_flash_store(0x19000, packet, sizeof(packet), released) == NRF_SUCCESS);
    assert(writes == 1); /* Never submit the same source twice. */
    assert(callbacks == 1 && !nrf_fstorage_is_busy(NULL));
    assert(critical_depth == 0 && !pending_irq);
    /* Ordinary asynchronous completion still retains the source buffer. */
    assert(nrf_dfu_flash_store(0x19014, packet, sizeof(packet), released) == NRF_SUCCESS);
    assert(writes == 2 && callbacks == 1 && nrf_fstorage_is_busy(NULL));
    flash_soc_event(NRF_EVT_FLASH_OPERATION_SUCCESS, NULL);
    assert(callbacks == 2 && !nrf_fstorage_is_busy(NULL));
    puts("PASS production queue: submission-return IRQ, one submission/release, ordinary async lifetime");
}
'''
    with tempfile.TemporaryDirectory() as temp:
        unit, executable = Path(temp) / 'irq.c', Path(temp) / 'irq'
        unit.write_text(prefix + definitions + state + functions + fixture)
        subprocess.run(['gcc', '-std=c99', '-Wall', '-Wextra', '-Werror',
                        '-Wno-unused-function', '-Wno-int-to-pointer-cast',
                        '-O2', str(unit), '-o', str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == '__main__':
    test_completion_irq_before_submit_returns()
