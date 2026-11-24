"""Pre-native-tool pure Python oracle; no publisher, transport or CLI.

Copied functions retain their bodies from nrf11_full.py SHA256 d34eed26dbefd455a75d5dcb1c8ceaa32507bec33462e84a0379836e60b86063.
TOML parsing uses the same standard library as the old board loader.
"""
from pathlib import Path
from dataclasses import dataclass,asdict
from typing import Any
import hashlib,struct,sys,tomllib
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'../../../../tools/platforms/nordic/nrf52811/programmer/internal/programmer_codec'))
from generated_commands_v2 import ScreenControllerV2,ScreenColorV2,SCREEN_RULES_V2
SCREEN_SCHEMA_VERSION=1
PIN_ROLES=('mosi','clock','cs','dc','reset','busy','enable','red','green','blue','txd','rxd')
ACTIVE_LOW_MASK=1023
def sha(data):return hashlib.sha256(data).hexdigest()
def load_board_config(path):return tomllib.loads(Path(path).read_text(encoding='utf-8'))
@dataclass
class ScreenConfig:
    schema_version: int
    driver: int
    color: int
    transform: int
    width: int
    height: int
    visible_x: int
    visible_y: int
    visible_width: int
    visible_height: int
    recipe: int = 0
    native_ui_scale: int = 0

    def to_dict(self) -> dict[str, Any]:
        result = asdict(self)
        result["driverHex"] = f"0x{self.driver:02X}"
        result["recipeHex"] = f"0x{self.recipe:02X}"
        return result

def fnv(data):
    value=2166136261
    for byte in data:value=((value^byte)*16777619)&0xffffffff
    return value

def read_config(path):
    path=Path(path)
    if path.is_symlink() or not path.is_file() or path.stat().st_size>16384:raise ValueError('Invalid Config')
    value=load_board_config(path)
    def exact(data,keys,label):
        if type(data) is not dict or set(data)!=set(keys):raise ValueError('Unknown or missing '+label)
    exact(value,('format_version','board_marking','note','screen','gpio'),'Config fields')
    if (type(value['format_version']) is not int or value['format_version']!=1 or
        value['board_marking']!='EL022HWRL' or value['note']!='CAL_VER06'):
        raise ValueError('Unknown PCB/revision or schema')
    screen=value['screen'];geometry=('width','height','visible_x','visible_y','visible_width','visible_height')
    exact(screen,('ic','recipe','color','rotation','mirror_x','mirror_y','native_ui_scale',*geometry),'screen')
    if (type(screen['rotation']) is not int or screen['rotation'] not in (0,90,180,270) or
        any(type(screen[k]) is not bool for k in ('mirror_x','mirror_y')) or
        any(type(screen[k]) is not int for k in ('recipe','native_ui_scale',*geometry))):
        raise ValueError('Invalid screen types/absolute orientation')
    try:
        controller=int(ScreenControllerV2[screen['ic']]);color=int(ScreenColorV2[screen['color']])
        model=ScreenConfig(schema_version=1,driver=controller,recipe=screen['recipe'],color=color,
            transform=screen['rotation']//90|(4 if screen['mirror_x'] else 0)|(8 if screen['mirror_y'] else 0),
            native_ui_scale=screen['native_ui_scale'],**{k:screen[k] for k in geometry})
        wire=encode_screen_config(model)
    except (KeyError,TypeError,ValueError) as error:raise ValueError('Invalid registered screen configuration') from error
    # Only the independent target's already-retired drivers are absent.
    if (controller in tuple(int(ScreenControllerV2[n]) for n in ('UC8151','UC8159','JD79665CA','SSD1675','SSD1XXX')) or
        (controller==int(ScreenControllerV2.SSD1677) and color==int(ScreenColorV2.GRAY4))):
        raise ValueError('Screen path is unsupported by this target')
    gpio=value['gpio'];exact(gpio,('display','led'),'gpio')
    display_roles=('mosi','clock','cs','dc','reset','busy','enable');led_roles=('red','green','blue')
    def pin(text):
        if text=='NONE':return 255
        if (type(text) is not str or len(text)!=5 or not text.startswith('P0.') or
            not text[3:].isascii() or not text[3:].isdigit() or int(text[3:])>=32):
            raise ValueError('GPIO must use P0.xx or NONE')
        value=int(text[3:])
        if not ((1<<value)&0xffdf0ff8):raise ValueError('GPIO conflicts with protected board resources')
        return value
    pins={};low=0
    for group,roles,allowed in [('display',display_roles,{'enable'}),('led',led_roles,set(led_roles))]:
        block=gpio[group]
        if group=='display':
            exact(block,(*roles,'enable_active_low'),'gpio.display')
            if type(block['enable_active_low']) is not bool:
                raise ValueError('enable_active_low must be boolean')
            active=['enable'] if block['enable_active_low'] else []
        else:
            exact(block,(*roles,'active_low'),'gpio.led')
            active=block['active_low']
            if type(active) is not list or any(type(k) is not str for k in active) or len(set(active))!=len(active) or not set(active)<=allowed:
                raise ValueError('Invalid LED polarity')
        for role in roles:
            pins[role]=pin(block[role])
            if role in active:low|=1<<((*display_roles,*led_roles).index(role))
    pin_wire=encode_pin_block(dict(pins={**pins,'txd':255,'rxd':255},activeLow=low))
    native=bytes([*[pins[k] for k in display_roles[:6]],2,pins['red'],pins['green'],pins['blue'],pins['enable'],1,1])
    return dict(**value,mcu='nrf52811',firmware_family='aesl22_nrf',board='nrf52811_asel22',
        screen_wire_hex=wire.hex(),factory_pins_hex=native.hex(),pin_wire_hex=pin_wire.hex(),
        active_low=low,sha256=sha(path.read_bytes()))

def compact_record(kind,payload):
    head=struct.pack('<IHHI',0x31525349,kind,len(payload),fnv(struct.pack('<HH',kind,len(payload))+payload))
    return head+payload+b'\xff'*((-len(payload))%4)+b'ISC1'

def encode_screen_config(config: ScreenConfig) -> bytes:
    validate_screen_config(config)
    return struct.pack(
        "<BBBBBHHHHHH",
        config.schema_version,
        config.driver,
        config.recipe,
        config.color,
        config.transform | (config.native_ui_scale << 4),
        config.width,
        config.height,
        config.visible_x,
        config.visible_y,
        config.visible_width,
        config.visible_height,
    )

def validate_screen_config(config: ScreenConfig) -> None:
    if config.schema_version != SCREEN_SCHEMA_VERSION:
        raise ValueError("unsupported screen schema")
    rule = SCREEN_RULES_V2.get((config.driver, config.recipe))
    if not rule or config.color not in rule["colors"]:
        raise ValueError("invalid screen driver or color")
    if not 0 <= config.transform <= 0x0F:
        raise ValueError("invalid screen transform")
    if not 0 <= config.native_ui_scale <= 5:
        raise ValueError("invalid native UI scale")
    max_width, max_height = rule["max"]
    if not 1 <= config.width <= max_width or not 1 <= config.height <= max_height:
        raise ValueError("invalid screen dimensions")
    if config.width % int(rule["align"]) != 0:
        raise ValueError("screen width does not meet the controller alignment")
    if config.visible_width < 1 or config.visible_height < 1:
        raise ValueError("visible viewport is empty")
    if config.visible_x + config.visible_width > config.width:
        raise ValueError("visible viewport exceeds screen width")
    if config.visible_y + config.visible_height > config.height:
        raise ValueError("visible viewport exceeds screen height")
    size = (config.width, config.height)
    if rule.get("full_viewport") and (
        config.visible_x != 0 or config.visible_y != 0
        or config.visible_width != config.width or config.visible_height != config.height
    ):
        raise ValueError("this panel recipe requires the full viewport")
    if rule["fixed_sizes"] and size not in rule["fixed_sizes"]:
        raise ValueError("screen dimensions do not match the panel recipe")
    color_sizes = rule["color_fixed_sizes"].get(config.color, ())
    if color_sizes and size not in color_sizes:
        raise ValueError("screen dimensions do not match the selected color mode")

def encode_pin_block(config: dict) -> bytes:
    if not isinstance(config, dict) or set(config) - {"pins", "activeLow"}:
        raise ValueError("Pin configuration requires pins and optional activeLow")
    pins = config.get("pins", {})
    if not isinstance(pins, dict) or set(pins) != set(PIN_ROLES):
        raise ValueError("A complete routing with all twelve named pins is required")
    values = [pins[name] for name in PIN_ROLES]
    if any(type(pin) is not int or not 0 <= pin <= 255 for pin in values):
        raise ValueError("Pin numbers must be integers 0..255; 255 is disconnected")
    if values[10:] != [255, 255]:
        raise ValueError('TXD and RXD positions are reserved and must be 255')
    low = config.get("activeLow", 0)
    if type(low) is not int or not 0 <= low <= ACTIVE_LOW_MASK or low & ~ACTIVE_LOW_MASK:
        raise ValueError("Invalid active-low role mask")
    active = [pin for pin in values if pin != 255]
    if len(set(active)) != len(active):
        raise ValueError("Active pin roles cannot overlap")
    if any(pin == 255 and low & (1 << i) for i, pin in enumerate(values)):
        raise ValueError("Disconnected pins cannot have polarity flags")
    display = sum(pin != 255 for pin in values[:6])
    if display != 6 and (display != 0 or values[6] != 255):
        raise ValueError("Provide all six display signals, or disable the complete display and rail")
    return bytes((1, 0)) + struct.pack("<H", low) + bytes(values)
