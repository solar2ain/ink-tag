"""PCB data only. Flash/NFC/ADC authority is internal, never editable fields."""
import hashlib
import re
import struct
from pathlib import Path
from board_config import ConfigurationError, load_board_config

ROOT=Path(__file__).resolve().parents[2]
from _om_full_formats.pin_config import PIN_ROLES, encode_pin_block, decode_pin_block
from _om_full_formats.protocol import encode_screen_config, SCREEN_SCHEMA_VERSION
from _om_full_formats.models import ScreenConfig
from _om_full_formats.generated_commands_v2 import ScreenControllerV2, ScreenColorV2
from _om_full_formats.generated_busy_defaults import SCREEN_BUSY_DEFAULTS

# Exact assemblies, not user-supplied backend IDs or silicon guesses.
BACKENDS={
 'om6626_hsela': ('HSELAQ_04_37MG_38',4,'layout-v1',0x3156484f),
 'om6626_zkc21b':('ZK_EL_OM21BA_V1.1',1,'layout-v1',0x31565a4f),
 'om6626_zkc42b':('ZK_EL_OM42BA_V1.0',2,'layout-v1',0x31565a4f),
 # Human-confirmed PCB marking (2026-10-14), production note PH2516.
 # Existing OM -N assembly/backend, never the GR5331 ZKC21V selector.
 'om6626_zkc21v':('ZK_EL_OM6626_2.13_V1.1',3,'layout-v1',0x31565a4f),
}
# A marking resolves an existing backend; it does not allocate an ID or grant
# Flash/NFC authority. HS product model remains unidentified, not guessed.
MARKINGS={entry[0]:hardware for hardware,entry in BACKENDS.items()}
if len(MARKINGS)!=len(BACKENDS):raise ValueError('ambiguous OM board marking map')
PRODUCTS={'om6626_zkc21b':'ZKC21B','om6626_zkc42b':'ZKC42B','om6626_zkc21v':'ZKC21V-N','om6626_hsela':None}
EDITABLE_MASK=0x03e8

def exact(value,keys,label):
    if type(value) is not dict or set(value)!=set(keys):
        raise ConfigurationError(label+' requires exactly '+', '.join(keys))

def defaults(hs):
    return [21,15,22,2,16,20,7,8,9,10,255,255] if hs else [21,15,22,20,16,3,7,4,18,17,255,255]

def validate_pins(wire,hs):
    cfg=decode_pin_block(wire);values=[cfg['pins'][k] for k in PIN_ROLES];d=defaults(hs)
    if cfg['activeLow']&~0x03c0:raise ConfigurationError('only EN/LED polarity is configurable')
    for i,v in enumerate(values):
        allowed=({d[3],d[5]} if i in (3,5) else {d[7],d[8],255} if i in (7,8)
                 else {d[9],255} if i==9 else {d[i]})
        if v not in allowed or (not EDITABLE_MASK&(1<<i) and v!=d[i]):
            raise ConfigurationError('OM backend does not permit pin role '+PIN_ROLES[i])
        expected=v!=255 and bool(0x380&(1<<i))
        if i not in (5,6,7,8,9) and bool(cfg['activeLow']&(1<<i))!=expected:
            raise ConfigurationError('OM backend polarity mismatch: '+PIN_ROLES[i])
    return wire

def pin_number(value):
    if value=='disabled':return 255
    if type(value) is str and re.fullmatch(r'GPIO(?:[0-9]|1[0-9]|2[0-5])',value):
        return int(value[4:])
    raise ConfigurationError('OM pins must be GPIO0..GPIO25 or disabled')

def validate_config(data):
    exact(data,('format_version','baseline','board_marking','screen','gpio'),'OM configuration')
    if type(data['format_version']) is not int or data['format_version']!=1 or data['baseline']!='inktag-release-v1':
        raise ConfigurationError('unsupported OM configuration version')
    marking=data['board_marking']
    if type(marking) is not str or marking not in MARKINGS:
        raise ConfigurationError('explicit known OM board_marking required')
    hardware=MARKINGS[marking]
    pcb,board_id,layout,signature=BACKENDS[hardware]
    screen=data['screen'];geometry=('width','height','visible_x','visible_y','visible_width','visible_height')
    exact(screen,('ic','recipe','color','rotation','mirror_x','mirror_y','native_ui_scale',*geometry),'screen')
    if type(screen['ic']) is not str or type(screen['color']) is not str:
        raise ConfigurationError('controller and colour must name shared enums')
    try:driver=int(ScreenControllerV2[screen['ic']]);color=int(ScreenColorV2[screen['color']])
    except KeyError as e:raise ConfigurationError('unregistered controller/colour') from e
    if type(screen['rotation']) is not int or screen['rotation'] not in (0,90,180,270):
        raise ConfigurationError('rotation must be 0/90/180/270')
    if any(type(screen[k]) is not bool for k in ('mirror_x','mirror_y')):
        raise ConfigurationError('mirror values must be boolean')
    if any(type(screen[k]) is not int or not 0<=screen[k]<=65535 for k in geometry):
        raise ConfigurationError('screen geometry must be unsigned 16-bit')
    if type(screen['recipe']) is not int or not 0<=screen['recipe']<=255:
        raise ConfigurationError('recipe must be an unsigned byte')
    if type(screen['native_ui_scale']) is not int or not 0<=screen['native_ui_scale']<=5:
        raise ConfigurationError('native_ui_scale must be 0..5')
    transform=screen['rotation']//90 | (4 if screen['mirror_x'] else 0) | (8 if screen['mirror_y'] else 0)
    wire=encode_screen_config(ScreenConfig(schema_version=SCREEN_SCHEMA_VERSION,driver=driver,
        recipe=screen['recipe'],color=color,transform=transform,native_ui_scale=screen['native_ui_scale'],
        **{k:screen[k] for k in geometry}))
    gpio=data['gpio'];exact(gpio,('display','led','aux'),'gpio')
    values=[];low=0
    for name,roles in (('display',PIN_ROLES[:7]),('led',PIN_ROLES[7:10]),('aux',PIN_ROLES[10:])):
        block=gpio[name]
        if name=='display':
            exact(block,(*roles,'enable_active_low'),'gpio.display')
            if type(block['enable_active_low']) is not bool:
                raise ConfigurationError('enable_active_low must be boolean')
            high=SCREEN_BUSY_DEFAULTS.get((driver<<8)|screen['recipe'])
            if high is None:raise ConfigurationError('unknown exact recipe BUSY semantics')
            polarity=['enable'] if block['enable_active_low'] else []
        else:
            exact(block,(*roles,'active_low'), 'gpio.'+name)
            polarity=block['active_low']
        if type(polarity) is not list or any(type(v) is not str or v not in roles for v in polarity) or len(set(polarity))!=len(polarity):
            raise ConfigurationError('active_low must contain unique role names from its group')
        for role in roles:
            if role in polarity:low|=1<<len(values)
            values.append(pin_number(block[role]))
    pins=validate_pins(encode_pin_block({'pins':dict(zip(PIN_ROLES,values)),'activeLow':low}),board_id==4)
    if wire[0]!=1 or pins[0]!=1:
        raise ConfigurationError('release-v1 shared screen/routing producers are required; no old-format adapter')
    return {'hardware':hardware,'board_marking':pcb,'board_id':board_id,'layout':layout,
        'firmware_family':'om6626_hsela' if board_id==4 else 'om6626_zkc',
        'product':PRODUCTS[hardware],
        'layout_signature':signature,'screen_wire':wire,'pin_wire':pins,
        'runtime_gpio_override':True,'editable_roles':EDITABLE_MASK,
        'portableExecutionQualified':False}

def read_config(path):
    result=validate_config(load_board_config(path))
    result['config_sha256']=hashlib.sha256(Path(path).read_bytes()).hexdigest()
    return result

def fnv(data):
    h=2166136261
    for b in data:h=((h^b)*16777619)&0xffffffff
    return h

def factory_record(config):
    if len(config['pin_wire'])!=16 or config['pin_wire'][0]!=1:
        raise ValueError('release-v1 common routing producer is required')
    prefix=b'OMF1'+bytes((1,config['board_id'],1,0))+config['pin_wire']
    return prefix+struct.pack('<I',fnv(prefix))


def require_family(config,metadata):
    if (metadata.get('baseline')!='inktag-release-v1' or
            metadata.get('family')!=config['firmware_family'] or metadata.get('layout')!=config['layout']):
        raise ValueError('PCB and MCU family base mismatch')

def default_preferences():
    # Native 43B field layout, fresh OMU1 namespace and complete common AUX.
    value=b'OMU1'+bytes((1,1,0,10,0,9,0,1,255,0,255,0,255,0,0,255,255))+struct.pack('<H',2000)+bytes((1,1,0))+bytes(16)
    checksum=0
    for b in value:checksum^=b
    assert len(value)==42
    return value+bytes((checksum,))
