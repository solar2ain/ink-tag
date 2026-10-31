"""Full-flash PCB configuration validation; no hardware or GPIO guessing."""
from __future__ import annotations

import hashlib
from pathlib import Path
import re
import sys

from board_config import ConfigurationError, load_board_config

ROOT = Path(__file__).resolve().parents[2]
from inktag_programmer_formats.protocol import encode_screen_config, SCREEN_SCHEMA_VERSION
from inktag_programmer_formats.models import ScreenConfig
from inktag_programmer_formats.pin_config import screen_busy_active_high
from inktag_programmer_formats.generated_commands_v2 import ScreenControllerV2, ScreenColorV2

# Existing immutable NFC/Flash/ADC/PM backends, not per-panel App targets.
# Screen and RGB GPIO are bounded persistent data, not compiled aliases.
PCB_BACKENDS = {
    "HS_EL5101_5M_66_05": ("hs_nowa154", "INKTAG_NOWA", "HS_EL5101_5M_66_05", "Hanshow Nowa-154R-N", 1),
    "HS_EL5102_1M_67_33": ("hs_nowa213", "INKTAG_NOWA213", "HS_EL5102_1M_67_33", "Hanshow Nowa-213R-N", 3),
    "HSEL4Q_02_13M_30": ("hs_nebular213", "INKTAG_NEBULAR213", "HSEL4Q_02_13M_30", "Hanshow Nebular-213R-N", 4),
    "HSEL4Q_04_20M_33": ("hs_nebular420", "INKTAG_NEBULAR", "HSEL4Q_04_20M_33", "Hanshow Nebular-420R-N", 2),
    "HSEL4Q_01_54M_31": ("hs_nebularpro154q", "INKTAG_NEBULARPRO154Q", "HSEL4Q_01_54M_31", "Hanshow NebularPro-154Q-N", 5),
    "HSEL3_02_66M_36": ("hs_hsel3_266q", "INKTAG_HSEL3266Q", "HSEL3_02_66M_36", "Hanshow HSEL3_02_66M_36 (2.66 inch)", 6),
    "HS_EL5101_5M_60_04": ("hs_hsel5101_200bwr", "INKTAG_HSEL5101200", "HS_EL5101_5M_60_04", "Hanshow HS_EL5101_5M_60_04 (200x200 BWR; product unconfirmed)", 7),
    "HSEL5_01_54M_00": ("hs_stellar154", "INKTAG_STELLAR154", "HSEL5_01_54M_00", "Hanshow StellarPro-154R-N", 8),
    "HSEL5Q_01_54M_00": ("hs_stellarpro154q", "INKTAG_STELLARPRO154Q", "HSEL5Q_01_54M_00", "Hanshow StellarPro-154Q-N", 9),
    "HSEL5_01_54M_02": ("hs_hsel5154m02", "INKTAG_HSEL5154M02", "HSEL5_01_54M_02", "Hanshow HSEL5_01_54M_02 (native OTP BWR)", 10),
}

# Historical host/OTA compatibility labels only; not public config inputs or
# device-info values. The device reports MCU tlsr8359. Never rename signed IDs.
BOARD_HARDWARE = {
    'hs_nowa154':'tlsr8359_nowa154', 'hs_nowa213':'tlsr8359_nowa213',
    'hs_nebular213':'tlsr8359_nebular213', 'hs_nebular420':'tlsr8359_nebular420',
    'hs_nebularpro154q':'tlsr8359_nebular154', 'hs_hsel3_266q':'tlsr8359_polaris260',
    'hs_hsel5101_200bwr':'tlsr8359_hsel5101',
    'hs_stellar154':'tlsr8359_stellar154', 'hs_stellarpro154q':'tlsr8359_stellar154',
    'hs_hsel5154m02':'tlsr8359_stellar154',
}
HARDWARE = frozenset(BOARD_HARDWARE.values())

# Minimal public host safety map, locked to the family header by tests. Full
# tools cannot require or export private firmware C headers. None means no
# qualified ADC route, never permission to enable that board's Full writer.
ADC_PROTECTED_GPIO = {1:'PB1', 2:'PB1', 3:'PB1', 4:'PB1', 5:'PB0',
                      6:'PB0', 7:'PB0', 8:'PB0', 9:'PB0', 10:'PB0'}
# Immutable supply rails remain owned even after moving/disabling screen pins.
AUX_RAIL_GPIO = {1:15, 2:13, 3:15, 4:13, 5:13, 6:13, 7:15, 8:15, 9:None, 10:15}


def validate_aux_wire(wire, pins, board_id):
    """Same fixed v1/8258 mux and whole-net admission as the port consumer."""
    if len(wire)!=12 or wire[0]!=1 or len(pins)!=16 or board_id not in AUX_RAIL_GPIO:
        raise ConfigurationError('Invalid aux v1/backend')
    roles=('mosi','clock','cs','dc','reset','busy','enable','red','green','blue')
    mask=int.from_bytes(pins[2:4],'little');gpio={}
    for group,start,end in (('display',0,7),('led',7,10)):
        gpio[group]={roles[i]:('NONE' if pins[4+i]==255 else
            f'P{chr(65+pins[4+i]//8)}{pins[4+i]%8}') for i in range(start,end)}
        if group=='display':
            gpio[group]['enable_active_low']=bool(mask&(1<<6))
        else:
            gpio[group]['active_low']=[roles[i] for i in range(start,end) if mask&(1<<i)]
    # This check only validates existing routing/aux ownership. BUSY semantics
    # are separately resolved from the complete screen at Full admission.
    if _pin_wire(gpio,board_id,not bool(mask&(1<<5)))!=pins:
        raise ConfigurationError('Invalid complete pin backend for aux ownership')
    selected=[]
    for index in range(3):
        pin,flags=wire[1+index*2:3+index*2]
        if flags & ~7 or flags>>1>2 or (pin==255 and flags):
            raise ConfigurationError('Invalid button flags/disabled slot')
        selected.append(pin)
    mode,a,b=wire[7:10]
    if mode not in (0,3) or b!=255 or (mode==0)!=(a==255):
        raise ConfigurationError('TLSR supports off/passive-single buzzer only')
    if mode==3 and (not 500<=int.from_bytes(wire[10:12],'little')<=8000 or
                   a not in (0,2,3,4,8,9,10,11,12,13,26,27,29)):
        raise ConfigurationError('Invalid PWM frequency or actual 8258 mux')
    selected.extend((a,b));used=set(pins[4:16])-{255}
    adc=ADC_PROTECTED_GPIO[board_id]
    protected={5,6,7,28,*range(16,24),AUX_RAIL_GPIO[board_id]}
    if adc:protected.add((ord(adc[1])-65)*8+int(adc[2]))
    for pin in selected:
        if pin==255:continue
        if pin>=32 or pin in used or pin in protected:
            raise ConfigurationError('Protected/duplicate/occupied aux GPIO')
        used.add(pin)
    return bytes(wire)


def aux_wire(gpio, pins, board_id):
    # All current TLSR profiles have no fitted qualified button. Never promote
    # suggested UART pads to defaults. Fixed list holes are explicit NONE.
    wire=bytearray((1,255,0,255,0,255,0,0,255,255,0xd0,7))
    buttons=gpio.get('buttons',[])
    if type(buttons) is not list or len(buttons)>3:
        raise ConfigurationError('gpio.buttons must contain at most three fixed slots')
    def pin_id(pin):
        if pin=='NONE':return 255
        if type(pin) is not str or not re.fullmatch(r'P[A-D][0-7]',pin):
            raise ConfigurationError('Aux pin must be PA0..PD7 or NONE')
        return (ord(pin[1])-65)*8+int(pin[2])
    for index,item in enumerate(buttons):
        if type(item) is not dict or not {'pin'}<=set(item)<= {'pin','active_low','pull'}:
            raise ConfigurationError('button requires pin; optional active_low/pull only')
        pin=pin_id(item['pin']);low=item.get('active_low',True);pull=item.get('pull','up')
        if type(low) is not bool or type(pull) is not str or pull not in ('none','up','down'):
            raise ConfigurationError('Invalid button polarity/pull')
        if pin==255 and (set(item)!={'pin'}):
            raise ConfigurationError('Disabled slot accepts pin=NONE only')
        wire[1+index*2]=pin
        wire[2+index*2]=(int(low)+2*('none','up','down').index(pull)) if pin!=255 else 0
    if 'buzzer' in gpio:
        buzzer=gpio['buzzer']
        if type(buzzer) is not dict or not set(buzzer)<= {'mode','gpio_a','gpio_b','frequency_hz'}:
            raise ConfigurationError('Unknown gpio.buzzer key')
        mode=buzzer.get('mode','passive-single')
        if mode not in ('off','passive-single'):
            raise ConfigurationError('TLSR buzzer mode is off or passive-single')
        a=pin_id(buzzer.get('gpio_a','NONE'));b=pin_id(buzzer.get('gpio_b','NONE'))
        frequency=buzzer.get('frequency_hz',2000)
        if type(frequency) is not int or not 500<=frequency<=8000:
            raise ConfigurationError('Buzzer frequency must be 500..8000 Hz')
        wire[7:10]=bytes((0 if mode=='off' else 3,a,b))
        wire[10:12]=frequency.to_bytes(2,'little')
    return validate_aux_wire(bytes(wire),pins,board_id)


def workflow_bindings() -> dict[str, str]:
    """Frozen host implementation, separate from compiled firmware inputs."""
    paths = [
        'firmware/ports/telink/tlsr8359/tools/'+('internal/' if name != 'flash.ps1' else '')+name for name in (
            'flash.ps1','full-flash.ps1','invoke-tlsr8359-uart-sws.ps1',
            'tlsr8359-uart-sws-common.ps1','tlsr825x-early-activate.py',
            'tlsr825x-flash-identify.py','terminal-log.ps1')]
    paths += ['tools/hardware/'+name+'.py' for name in (
        'tlsr_full_flash_config','tlsr_factory_config','tlsr_full_flash_base',
        'tlsr_full_flash_plan','tlsr_full_flash_contract','tlsr_full_flash_v1','tlsr_full_flash_source','tlsr_full_flash_archive',
        'tlsr_full_flash_format','tlsr_full_flash_state',
        'tlsr_full_flash_write_plan','tlsr_full_flash_transport',
        'ble_factory_address','tlsr_full_flash_address','tlsr_trusted_entry_profile')]
    paths += ['tools/hardware/flash-workflow.py',
              'tools/hardware/tlsr-terminal-entry.py', 'tools/sdk/make-tlsr-full-flash.py',
              'tools/hardware/board_config.py', 'tools/hardware/tlsr_flash_jedec.py',
              'cli/src/inktag_cli/protocol.py',
              'cli/src/inktag_cli/models.py', 'cli/src/inktag_cli/generated_commands_v2.py',
              'cli/src/inktag_cli/pin_config.py', 'cli/src/inktag_cli/generated_busy_defaults.py',
              'cli/src/inktag_cli/ota.py', 'cli/src/inktag_cli/tlsr_layout.py',
              'cli/src/inktag_cli/_drivers/ota_tlsr8359.py',
              'tools/sdk/make-telink-tlsr8359-images.py']
    # Actual programmer inputs are fixed here; CLI paths above remain original
    # build provenance only and are never consulted by probe/backup/flash.
    paths += [path.relative_to(ROOT).as_posix() for path in
              sorted((ROOT/'tools/hardware/inktag_programmer_formats').glob('*'))
              if path.suffix in ('.py','.json')]
    return {path:hashlib.sha256((ROOT/path).read_bytes()).hexdigest().upper() for path in paths}


def compiled_bindings() -> dict[str, str]:
    paths = ["firmware/boards/tlsr8359_hsel.h"]
    paths += ["firmware/ports/telink/tlsr8359/src/inktag_tlsr_board.c",
              "firmware/ports/telink/tlsr8359/src/inktag_tlsr_identity.c",
              "firmware/ports/telink/tlsr8359/src/inktag_tlsr_identity.h",
              "firmware/ports/telink/tlsr8359/src/inktag_ble.c",
              "firmware/ports/telink/tlsr8359/src/recovery_ble.c",
              "firmware/common/ble/inktag_ble_address.h",
              "firmware/common/ble/inktag_ble_address.c",
              "firmware/common/ble/inktag_ble_address_record.c",
              "firmware/common/protocol/inktag_protocol_core.c",
              "firmware/ports/telink/tlsr8359/Makefile",
              "firmware/ports/telink/tlsr8359/src/main.c",
              "firmware/ports/telink/tlsr8359/src/app.c",
              "firmware/ports/telink/tlsr8359/src/app_config.h",
              "firmware/ports/telink/tlsr8359/src/inktag_app.c",
              "firmware/ports/telink/tlsr8359/src/inktag_protocol_v2.inc",
              "firmware/ports/telink/tlsr8359/src/inktag_epd.c",
              "firmware/ports/telink/tlsr8359/src/inktag_storage.h",
              "firmware/ports/telink/tlsr8359/src/inktag_storage.c",
              "firmware/ports/telink/common/src/inktag_tlsr_image_flash.h",
              "firmware/common/storage/inktag_image_fixed_store.inc",
              "firmware/common/storage/inktag_image_fixed_store.h",
              "firmware/common/gui/GUI.h",
              "firmware/common/gui/GUI.c",
              "firmware/common/gui/GUI_default.inc",
              "firmware/common/gui/inktag_gui_font_roles.h",
              "firmware/common/gui/inktag_gui_fixed_layout.h",
              "firmware/common/gui/inktag_gui_large_digits.h",
              "firmware/common/gui/u8g2_font.c",
              "firmware/common/gui/u8g2_font.h",
              "protocol/gui-font-native-v3.json",
              "firmware/common/ble/inktag_ble_identity.h",
              "firmware/common/ble/inktag_device_display_name.h",
              "firmware/common/platform/inktag_elapsed_clock.c",
              "firmware/common/platform/inktag_elapsed_clock.h",
              "firmware/common/time/inktag_time_checkpoint.h",
              "firmware/common/time/inktag_clock_rate.c",
              "firmware/ports/telink/tlsr8359/src/inktag_tlsr_ota_policy.h",
              "firmware/ports/telink/common/src/inktag_tlsr_flash_layout.h",
              "firmware/ports/telink/tlsr8359/src/inktag_tlsr_gui_assets.c",
              "firmware/ports/telink/tlsr8359/src/inktag_tlsr_gui_assets.h",
              "firmware/ports/telink/tlsr8359/src/inktag_tlsr_user_font_storage.c",
              "firmware/ports/telink/tlsr8359/src/inktag_tlsr_user_font_storage.h",
              "firmware/common/gui/fonts.c",
              "firmware/common/gui/fonts.h",
              "firmware/common/gui/inktag_gui_assets.h",
              "firmware/common/gui/inktag_gui_asset_package.h",
              "firmware/common/gui/inktag_gui_asset_package.c",
              "protocol/screen-controllers.json",
              "firmware/boards/boards.json",
              "firmware/ports/telink/common/src/inktag_tlsr_pin_layout.h",
              "firmware/ports/telink/common/src/inktag_tlsr_pin_backend.h",
              "firmware/ports/telink/common/src/inktag_tlsr_pin_record.h",
              "firmware/ports/telink/common/src/inktag_tlsr_factory_screen.h",
              "firmware/ports/telink/tlsr8359/src/inktag_tlsr_pin_routes.c",
              "firmware/ports/telink/tlsr8359/src/inktag_nfc.c",
              "firmware/ports/telink/tlsr8359/src/inktag_led.c",
              "firmware/ports/telink/tlsr8359/src/inktag_tlsr.h",
              "firmware/ports/telink/common/src/inktag_tlsr_features.h",
              "firmware/ports/telink/tlsr8359/src/inktag_tlsr_recovery_pins.c",
              "firmware/ports/telink/tlsr8359/src/inktag_settings.c",
              "firmware/common/protocol/inktag_pin_config.c",
              "firmware/common/protocol/inktag_aux_io.c",
              "firmware/common/protocol/inktag_aux_io.h",
              "firmware/ports/telink/common/src/inktag_tlsr_aux_backend.h",
              "firmware/ports/telink/common/src/inktag_tlsr_aux_io.c",
              "firmware/ports/telink/common/src/inktag_tlsr_aux_io.h",
              "firmware/common/epd/EPD_screen.c",
              "firmware/common/epd/EPD_screen.h",
              "firmware/common/epd/inktag_screen_busy.h",
              "firmware/common/epd/EPD_driver.h",
              "firmware/common/epd/EPD_runtime.c",
              "firmware/common/epd/UC8xxx.c",
              "firmware/common/epd/OspreyE6.inc"]
    return {path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest().upper() for path in paths}


def _mapping(value, keys, label):
    if type(value) is not dict or set(value) != set(keys):
        raise ConfigurationError(f"{label} requires exactly: {', '.join(keys)}")


def _pin_wire(gpio, board_id, busy_high):
    roles=('mosi','clock','cs','dc','reset','busy','enable','red','green','blue')
    if type(gpio) is not dict or not {'display','led'}<=set(gpio)<= {'display','led','buttons','buzzer'}:
        raise ConfigurationError('gpio requires display/led; optional buttons/buzzer only')
    _mapping(gpio['display'], (*roles[:7],'enable_active_low'), 'gpio.display')
    if type(gpio['display']['enable_active_low']) is not bool or type(busy_high) is not bool:
        raise ConfigurationError('enable_active_low must be boolean and screen BUSY must be known')
    led=gpio['led']
    _mapping(led, (*roles[7:],'active_low'), 'gpio.led')
    protected={'PA5','PA6',*(f'PC{i}' for i in range(8))}
    if board_id not in ADC_PROTECTED_GPIO:
        raise ConfigurationError('unknown immutable GPIO protection backend')
    adc=ADC_PROTECTED_GPIO[board_id]
    if adc: protected.add(adc)
    pins=[];mask=0;used=set()
    for group,names in (('display',roles[:7]),('led',roles[7:])):
        low=(gpio[group]['active_low'] if group=='led' else
             [name for name in ('enable',)
              if gpio['display'][name]!='NONE' and
              gpio['display']['enable_active_low']])
        if type(low) is not list or any(type(x) is not str or x not in names for x in low) or len(set(low))!=len(low):
            raise ConfigurationError(f'gpio.{group}.active_low must list distinct supported role names')
        for name in names:
            pin=gpio[group][name];role=roles.index(name)
            if pin=='NONE':
                if name in low: raise ConfigurationError('inactive pin has a polarity')
                pins.append(255);continue
            if name in low and name in ('mosi','clock','dc'):
                raise ConfigurationError('MOSI/clock/DC polarity remapping has no backend')
            if type(pin) is not str or not re.fullmatch(r'P[A-D][0-7]',pin) or pin in protected or pin in used:
                raise ConfigurationError(f'Invalid/protected/duplicate GPIO {group}.{name}')
            if pin=='PA7' and (name!='blue' or name not in low):
                raise ConfigurationError('PA7 is available only for active-low blue/SWS sharing')
            used.add(pin);pins.append((ord(pin[1])-ord('A'))*8+int(pin[2]))
            if name in low: mask|=1<<role
    fitted=sum(x!=255 for x in pins[:6])
    if fitted!=6 and not (fitted==0 and pins[6]==255):
        raise ConfigurationError('display requires all six signals or a fully disabled bus/rail')
    if pins[6]==255 and gpio['display']['enable_active_low']:
        raise ConfigurationError('disconnected enable has no active-low setting')
    return bytes((1,0,mask&255,mask>>8,*pins,255,255))


def validate_board_config(data: dict) -> dict:
    _mapping(data, ("format_version", "board_marking", "screen", "gpio"), "board configuration")
    if type(data["format_version"]) is not int or data["format_version"] != 1:
        raise ConfigurationError("unsupported board configuration version")
    if type(data['board_marking']) is not str or data['board_marking'] not in PCB_BACKENDS:
        raise ConfigurationError('PCB has no confirmed immutable backend')
    board, prefix, pcb, product, board_id = PCB_BACKENDS[data['board_marking']]
    hardware = BOARD_HARDWARE[board]
    screen = data["screen"]
    geometry = ("width", "height", "visible_x", "visible_y", "visible_width", "visible_height")
    _mapping(screen, ("ic", "recipe", "color", "rotation", "mirror_x", "mirror_y", "native_ui_scale", *geometry), "screen")
    # Controller IDs, recipes and colour values are the shared stable wire
    # contract; neither a CLI/WebUI preset label nor a PCB's original screen
    # selects a driver. Unknown die markings must use the honest family ID.
    try:
        if type(screen['ic']) is not str or type(screen['color']) is not str:
            raise KeyError('non-string screen IC/colour')
        driver = int(ScreenControllerV2[screen['ic']])
        color = int(ScreenColorV2[screen['color']])
    except KeyError as exc:
        raise ConfigurationError('screen.ic/color must name a registered controller/colour') from exc
    if type(screen['recipe']) is not int or not 0 <= screen['recipe'] <= 255:
        raise ConfigurationError('screen.recipe must be an integer from 0 to 255')
    if type(screen["rotation"]) is not int or screen["rotation"] not in (0, 90, 180, 270):
        raise ConfigurationError("screen.rotation must be 0, 90, 180 or 270")
    if type(screen["mirror_x"]) is not bool or type(screen["mirror_y"]) is not bool:
        raise ConfigurationError("screen mirrors must be boolean")
    if type(screen['native_ui_scale']) is not int or not 0 <= screen['native_ui_scale'] <= 5:
        raise ConfigurationError('screen.native_ui_scale must be 0..5; 0 selects AUTO')
    if any(type(screen[key]) is not int or not 0 <= screen[key] <= 65535 for key in geometry):
        raise ConfigurationError('screen geometry must use unsigned 16-bit integers')
    gpio = data["gpio"]
    # Absolute orientation, never XOR/add an old profile's transform again.
    transform = screen["rotation"] // 90 | (4 if screen["mirror_x"] else 0) | (8 if screen["mirror_y"] else 0)
    try:
        config = ScreenConfig(schema_version=SCREEN_SCHEMA_VERSION,
            driver=driver, recipe=screen['recipe'], color=color, transform=transform,
            native_ui_scale=screen['native_ui_scale'], **{key:screen[key] for key in geometry})
        wire = encode_screen_config(config)
    except ValueError as exc:
        raise ConfigurationError(f'Invalid screen configuration: {exc}') from exc
    busy_high=screen_busy_active_high(config)
    if busy_high is None:
        raise ConfigurationError('screen controller/recipe has no qualified BUSY semantics')
    pin_wire=_pin_wire(gpio,board_id,busy_high)
    aux=aux_wire(gpio,pin_wire,board_id)
    no_panel = all(pin == 255 for pin in pin_wire[4:11])
    return {"format_version": 1, "firmware_family": "tlsr8359_hsel",
            "hardware": hardware, "board": board, "board_id": board_id,
            "board_marking": pcb, "screen_wire_hex": wire.hex().upper(), "gpio": gpio,
            "pin_wire_hex":pin_wire.hex().upper(),"pin_expansion_bytes":4,
            "aux_wire_hex":aux.hex().upper(), "aux_explicit":bool({'buttons','buzzer'} & set(gpio)),
            "runtime_gpio_override": True, "generic_delivered": False,
            "no_panel_bringup": no_panel,
            "base_image_store_only": board == "hs_nebular420"}


def read_board_config(path: str | Path) -> dict:
    result = validate_board_config(load_board_config(path))
    result["configuration_sha256"] = hashlib.sha256(Path(path).read_bytes()).hexdigest().upper()
    # The optional manufacturing note is host archive selection only. It never
    # changes selector/GPIO/App bytes and is not an asserted MCU identity.
    suffix=Path(path).stem.removeprefix(result['board_marking']+'-')
    result['archive_board_note']=suffix if re.fullmatch(r'YW\d{4}',suffix) else ''
    return result
