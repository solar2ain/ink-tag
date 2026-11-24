"""Drive actual port boot/tick hooks and common effects through scheduled wakes."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'compat/nordic/src/inktag_nrf_protocol_v2.inc').read_text()

def function(name):
    point = source.index(name + '(')
    start = source.rfind('\n', 0, point) + 1
    opening = source.index('{', point)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

prefix = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "inktag_status.h"
#define INKTAG_BOARD_RGB_STATUS_LED_QUALIFIED 1
static bool m_v2_led_test_active;
static uint32_t m_v2_led_test_deadline, now, starts;
static bool m_v2_indicator_timer_armed;
static inktag_status_runtime_t m_v2_indicator_runtime;
static uint8_t mask;
static const unsigned m_v2_indicator_timer = 1;
static uint32_t app_monotonic_now_ms(void) { return now; }
static void inktag_nrf_led_apply(uint8_t value) { mask = value; }
#define APP_TIMER_TICKS(x) (x)
#define APP_ERROR_CHECK(x) assert((x) == 0)
static int app_timer_start(unsigned timer, unsigned duration, void *ctx) {
    assert(timer == 1 && duration == 30 && !ctx); starts++; return 0;
}
static int app_timer_stop(unsigned timer) { assert(timer == 1); return 0; }
'''
fixture = r'''
int main(void) {
    inktag_status_config_t config;
    inktag_status_config_default(&config);
    now = 5000; /* Blocking initialization has already finished. */
    inktag_status_init(&m_v2_indicator_runtime, &config, 7,
        epd_v2_indicator_apply, NULL, now);
    inktag_nrf_startup_ready_begin();
    for (unsigned elapsed = 0; elapsed <= 1920; elapsed += 30) {
        now = 5000 + elapsed;
        epd_v2_indicator_wakeup(NULL);
        epd_v2_indicator_tick();
        uint8_t expected = elapsed < 1080 ? (1u << (elapsed / 360)) :
            elapsed < 1920 && ((elapsed - 1080) / 120) % 2 ? 2 : 0;
        assert(mask == expected);
        assert(inktag_nrf_startup_ready_complete() == (elapsed == 1920));
        assert(m_v2_indicator_timer_armed == (elapsed < 1920));
    }
    unsigned completed = starts;
    now += 1000; epd_v2_indicator_tick(); assert(starts == completed);
    m_v2_led_test_active = true; m_v2_led_test_deadline = now + 10000;
    epd_v2_indicator_tick(); assert(m_v2_indicator_timer_armed);
    now += 10000; epd_v2_indicator_wakeup(NULL); epd_v2_indicator_tick();
    assert(!m_v2_led_test_active && !m_v2_indicator_timer_armed && mask == 0);
    config.policy = INKTAG_STATUS_POLICY_OFF;
    inktag_status_set_config(&m_v2_indicator_runtime, &config, now);
    inktag_nrf_startup_ready_begin(); epd_v2_indicator_tick();
    assert(mask == 0 && inktag_nrf_startup_ready_complete() && !m_v2_indicator_timer_armed);
    puts("PASS production startup hooks: RGB 360ms; ready green 3x120ms; boot barrier; idle/off stop wakeups; 10s test timeout");
}
'''
with tempfile.TemporaryDirectory() as directory:
    temp = Path(directory)
    test = temp / 'boot.c'
    test.write_text(prefix + '\n'.join(function(name) for name in (
        'epd_v2_indicator_apply', 'epd_v2_indicator_wakeup', 'epd_v2_indicator_tick',
        'epd_v2_indicator_trigger', 'inktag_nrf_startup_ready_begin',
        'inktag_nrf_startup_ready_complete')) + fixture)
    binary = temp / 'boot'
    subprocess.run([os.environ.get('CC', 'gcc'), '-std=c99', '-O2', '-Wall', '-Werror',
        '-Icompat/common/status', str(test), 'compat/common/status/inktag_status.c',
        '-o', str(binary)], cwd=root, check=True)
    subprocess.run([str(binary)], check=True)
