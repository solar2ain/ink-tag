"""Exercise the production RGB adapter and LED wire handler with GPIO mocks."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'compat/nordic/src/inktag_nrf_protocol_v2.inc').read_text()

def function(name):
    start = source.index('static ', source.index(name) - 40)
    opening = source.index('{', source.index(name, start))
    depth = 1
    position = opening + 1
    while depth:
        depth += (source[position] == '{') - (source[position] == '}')
        position += 1
    return source[start:position]

prefix = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "nrf.h"
#include "inktag_protocol_core.h"
#include "inktag_nrf_led.h"
#include "inktag_nrf_pins.h"
#include "inktag_board.h"
#define INKTAG_BOARD_RGB_STATUS_LED_QUALIFIED 1
gpio_mock_t gpio_mock;
typedef int ble_epd_t;
static bool m_v2_led_test_active;
static uint32_t m_v2_led_test_deadline;
static uint32_t app_monotonic_now_ms(void) { return 500u; }
static struct { uint8_t last_output; } m_v2_indicator_runtime;
static unsigned responses, completions, ticks;
static bool reference_allowed(void *unused,inktag_pin_role_t role,uint8_t pin){
 (void)unused;return role<INKTAG_PIN_TXD && pin<32 && (INKTAG_BOARD_CONFIG_GPIO_MASK&(UINT32_C(1)<<pin));
}
static uint8_t response_status, completed_mask;
static void epd_v2_send_response(ble_epd_t *p, uint8_t op, uint8_t tx,
    uint8_t status, const uint8_t *payload, uint16_t length) {
    (void)p; assert(op == INKTAG_CMD_RUN_TEST && tx == 19);
    assert(payload == NULL && !length); ++responses; response_status = status;
}
#define epd_v2_reply_status(p,o,t,s) epd_v2_send_response(p,o,t,s,NULL,0u)

static void epd_v2_send_event(ble_epd_t *p, uint8_t event, uint8_t status,
    uint8_t op, uint8_t tx, const uint8_t *payload, uint16_t length) {
    (void)p; assert(event == INKTAG_EVENT_COMPLETE && status == INKTAG_STATUS_OK);
    assert(op == INKTAG_CMD_RUN_TEST && tx == 19 && length == 1);
    ++completions; completed_mask = *payload;
}
static void epd_v2_indicator_tick(void) { ++ticks; }
'''
fixture = r'''
int main(void) {
    const uint8_t defaults[]=INKTAG_BOARD_DEFAULT_PIN_WIRE;
    for(unsigned at=0;at<16;++at)for(unsigned value=0;value<256;++value){
        uint8_t wire[16];memcpy(wire,defaults,16);wire[at]=value;
        inktag_pin_config_t decoded;
        bool expected=inktag_pin_config_decode(wire,16,&decoded) && inktag_pin_config_validate(&decoded,reference_allowed,NULL);
        assert(inktag_nrf_pins_valid(wire)==expected);
    }
    assert(inktag_nrf_pins_publish(defaults));
    const uint8_t *stable_wire=inktag_nrf_pins_wire;
    assert(stable_wire==inktag_nrf_pins_current() && !memcmp(stable_wire,defaults,16));
    for (uint8_t mask = 0; mask < 8; ++mask) {
        uint8_t body[] = {INKTAG_TEST_LED_CHANNEL, mask};
        assert(epd_v2_run_led_test(NULL, INKTAG_CMD_RUN_TEST, 19, body, 2));
        assert(response_status == INKTAG_STATUS_OK && completed_mask == mask);
        assert(gpio_mock.OUTCLR == ((uint32_t)mask << 16));
        assert(gpio_mock.OUTSET == ((uint32_t)(7 ^ mask) << 16));
        assert(gpio_mock.DIRSET == (7u << 16));
        for (unsigned pin = 16; pin <= 18; ++pin) assert(gpio_mock.PIN_CNF[pin] == 3);
        assert(m_v2_led_test_active == (mask != 0));
        assert(m_v2_led_test_deadline == 10500u);
    }
    uint8_t invalid[][3] = {{INKTAG_TEST_LED_CHANNEL, 8, 0},
                          {INKTAG_TEST_LED_CHANNEL, 1, 0}};
    for (unsigned length = 1; length <= 3; ++length) {
        unsigned before = completions;
        gpio_mock_t previous = gpio_mock;
        assert(epd_v2_run_led_test(NULL, INKTAG_CMD_RUN_TEST, 19, invalid[length == 2 ? 0 : 1], length));
        assert(response_status == INKTAG_STATUS_INVALID_REQUEST && completions == before);
        assert(memcmp(&previous, &gpio_mock, sizeof previous) == 0);
    }
    assert(!epd_v2_run_led_test(NULL, INKTAG_CMD_RUN_TEST, 19, invalid[0], 0));
    uint8_t other[] = {255};
    assert(!epd_v2_run_led_test(NULL, INKTAG_CMD_RUN_TEST, 19, other, 1));
    uint8_t red[] = {INKTAG_TEST_LED_CHANNEL, 1};
    assert(epd_v2_run_led_test(NULL, INKTAG_CMD_RUN_TEST, 19, red, 2));
    epd_v2_indicator_apply(NULL, 2);
    assert(gpio_mock.OUTCLR == (1u << 16));
    epd_v2_led_test_cancel();
    assert(!m_v2_led_test_active && ticks == 1 && gpio_mock.OUTCLR == 0);
    epd_v2_led_test_cancel(); assert(ticks == 1);
    epd_v2_indicator_apply(NULL, 4); assert(gpio_mock.OUTCLR == (4u << 16));
    uint8_t remap[16];memcpy(remap,defaults,16);
    remap[11]=8;remap[12]=9;remap[13]=255;remap[2]=0;remap[3]=1;
    assert(inktag_nrf_pins_publish(remap));
    assert(inktag_nrf_pins_wire==stable_wire && !memcmp(stable_wire,remap,16));
    assert(gpio_mock.DIRCLR==(7u<<16));
    inktag_nrf_led_apply(1);
    assert(gpio_mock.OUTSET==((1u<<8)|(1u<<9)) && !gpio_mock.OUTCLR);
    inktag_nrf_led_apply(2);
    assert(!gpio_mock.OUTSET && gpio_mock.OUTCLR==((1u<<8)|(1u<<9)));
    gpio_mock_t previous=gpio_mock;remap[11]=12;
    assert(!inktag_nrf_pins_publish(remap) && !memcmp(&previous,&gpio_mock,sizeof previous));
    inktag_nrf_pins_invalidate();assert(!inktag_nrf_pins_current());
    assert(inktag_nrf_pins_wire==stable_wire && stable_wire[0]==0u);
    previous=gpio_mock;inktag_nrf_led_apply(7);assert(!memcmp(&previous,&gpio_mock,sizeof previous));
    puts("PASS qualified RGB GPIO masks; production LED request/completion; malformed requests; override/cancel");
}
'''
with tempfile.TemporaryDirectory() as directory:
    temp = Path(directory)
    (temp / 'nrf.h').write_text('''#ifndef MOCK_NRF_H
#define MOCK_NRF_H
#include <stdint.h>
typedef struct { uint32_t OUTSET, OUTCLR, DIRSET, DIRCLR, PIN_CNF[32]; } gpio_mock_t;
extern gpio_mock_t gpio_mock;
#define NRF_P0 (&gpio_mock)
#endif
''')
    (temp / 'app_util_platform.h').write_text('#define CRITICAL_REGION_ENTER() do {} while(0)\n#define CRITICAL_REGION_EXIT() do {} while(0)\n')
    test = temp / 'test.c'
    test.write_text(prefix + function('epd_v2_indicator_apply') + '\n' +
                    function('epd_v2_led_test_cancel') + '\n' +
                    function('epd_v2_run_led_test') + '\n' + fixture)
    binary = temp / 'test'
    subprocess.run([os.environ.get('CC', 'gcc'), '-std=c99', '-O2', '-Wall', '-Werror',
                    '-DNRF52811_XXAA', '-I' + str(temp),
                    '-Ifirmware/boards', '-Ifirmware/ports/nordic/common/config', '-Ifirmware/common/protocol',
                    '-Ifirmware/common/epd', '-Ifirmware/common/ble',
                    '-Ifirmware/common/input', '-Ifirmware/common/storage',
                    '-Ifirmware/ports/nordic/common/src', str(test),
                    'compat/nordic/src/inktag_nrf_led.c',
                    'compat/nordic/src/inktag_nrf_pins.c',
                    'compat/common/protocol/inktag_pin_config.c', '-o', str(binary)],
                   cwd=ROOT, check=True)
    print(subprocess.check_output([str(binary)], text=True))
