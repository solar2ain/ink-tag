"""Generate size-oriented Nordic drivers without editing the common snapshot.

Only consecutive constant EPD_Write calls become tuples. Every original
command and data transaction stays separate; dynamic calls, waits and other
statements remain in place. The normal common build remains the reference.
"""
from pathlib import Path
import re
import sys

CALL = re.compile(r'^(\s*)EPD_Write\(([^;]+)\);(?:\s*//[^\n]*)?$')
ATOM = re.compile(r'(?:0[xX][0-9a-fA-F]+|[0-9]+|[A-Z][A-Z0-9_a-z]*)(?:u|U)?$')


def project(source):
    lines = source.splitlines()
    output = ['/* Generated Nordic projection; common source is unchanged. */',
              '#include <stdint.h>',
              'void EPD_WriteInitCommands(const uint8_t*, uint16_t);']

    def parse(line):
        match = CALL.fullmatch(line)
        if not match:
            return None
        args = [arg.strip() for arg in match[2].split(',')]
        if not 2 <= len(args) <= 17 or not all(ATOM.fullmatch(a) for a in args):
            return None
        return match[1], args

    i = 0
    while i < len(lines):
        first = parse(lines[i])
        group = []
        j = i
        if first:
            while j < len(lines):
                item = parse(lines[j])
                if not item or item[0] != first[0]:
                    break
                group.append(item[1])
                j += 1
        previous = lines[i - 1].strip() if i else ''
        unbraced = bool(re.match(r'(?:if|for|while)\s*\(.*\)\s*$', previous))
        if len(group) >= 1 and not unbraced:
            indent = first[0]
            output += [indent + '{', indent + '    static const uint8_t commands[] = {']
            for args in group:
                output.append(indent + '        ' + ', '.join(
                    [args[0], str(len(args) - 1) + 'u'] + args[1:]) + ',')
            output += [indent + '    };',
                       indent + '    EPD_WriteInitCommands(commands, sizeof(commands));',
                       indent + '}']
            i = j
        else:
            output.append(lines[i])
            i += 1
    return '\n'.join(output) + '\n'


def project_fixed_pins(source):
    """Load validated display/RGB routes; keep only board-private BS fixed.

    GPIO/SPI/BUSY/reset function bodies stay intact. The published pin16 has
    static lifetime and is the sole route authority, shared with RGB/readback.
    Never retain a caller-owned configuration pointer or a second route copy.
    """
    start = source.index('static uint8_t epd_pads[PAD_COUNT]')
    end = source.index('static bool EPD_LED_ENABLED',start)
    old = source[start:end]
    roles = [('MOSI',4),('SCLK',5),('CS',6),('DC',7),
             ('RST',8),('BUSY',9),('EN',10)]
    assert 'epd_pads[PAD_COUNT]' in old
    pins = ['#if !defined(INKTAG_NRF_BOARD_NRF52811_AESL) || !defined(NRF52811_XXAA)',
            '#error "factory GPIO projection requires the EL022HWRL native port"',
            '#endif',
            'static const epd_config_t inktag_default_epd_pins = INKTAG_BOARD_DEFAULT_EPD_CONFIG;',
            'extern const uint8_t *const inktag_nrf_pins_wire;',
            '#define EPD_BS_PIN inktag_default_epd_pins.bs_pin',
            '/* Qualified RGB status owns every LED; the legacy single-blue',
            ' * screen hook must not change its routes, polarity or phases. */',
            '#define EPD_LED_PIN 255u']
    pins += [f'#define EPD_{name}_PIN (inktag_nrf_pins_wire[{offset}u])' for name,offset in roles]
    pins += ['#undef digitalWrite',
             'extern const uint8_t *inktag_nrf_pins_current(void);',
             'static __attribute__((noinline,noclone)) void inktag_epd_pin_write(uint32_t pin,uint32_t value) {',
             '    const uint8_t *wire=inktag_nrf_pins_current();',
             '    if(wire && pin==EPD_EN_PIN && (wire[2]&64u)) value^=1u;',
             '    nrf_gpio_pin_write(pin,value);',
             '}',
             '#define digitalWrite(pin,value) inktag_epd_pin_write(pin,value)']
    source = source[:start]+'\n'.join(pins)+'\n'+source[end:]
    source=re.sub(r'^#define EPD_\w+_PIN epd_pads\[PAD_\w+\]\n', '', source, flags=re.M)
    source=source.replace('epd_pads[pad]', 'inktag_epd_pad_value(pad)')
    accessor='static uint8_t inktag_epd_pad_value(unsigned pad) { return pad<6u ? inktag_nrf_pins_wire[4u+pad] : pad==PAD_BS ? EPD_BS_PIN : pad==PAD_ENABLE ? EPD_EN_PIN : 255u; }\n'
    source=source.replace('static bool EPD_LED_ENABLED',accessor+'static bool EPD_LED_ENABLED')
    match = re.search(r'void EPD_GPIO_Load\(epd_config_t\* cfg\) \{[\s\S]*?\n}',source)
    assert match
    replacement='void EPD_GPIO_Load(epd_config_t* cfg) { (void)cfg; /* published pin16 owns routing */ }'
    source=source[:match.start()]+replacement+source[match.end():]
    match = re.search(r'^bool[^\n]*\bEPD_GPIO_IsConfigured\(void\) \{[^}]*}',source,re.M)
    assert match
    replacement=('bool __attribute__((pure)) EPD_GPIO_IsConfigured(void) { '
                 'return inktag_nrf_pins_wire[0] && inktag_nrf_pins_wire[4]!=255u; }')
    source=source[:match.start()]+replacement+source[match.end():]
    return '/* Generated factory GPIO projection; common control source is unchanged. */\n'+source

if __name__ == '__main__':
    destination = Path(sys.argv[2])
    destination.parent.mkdir(parents=True, exist_ok=True)
    source = Path(sys.argv[1])
    transform = project_fixed_pins if source.name == 'inktag_epd_driver.c' else project
    destination.write_text(transform(source.read_text(encoding='utf-8')), encoding='utf-8')
