"""Exact PriceETag configured-Full data, using the shared file and screen ABI.

No programmer, BLE client or alternative renderer. GPIO qualification is not
inferred from a file: only this PCB's fitted routes are admitted.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import runpy
import struct
import sys
from board_config import ConfigurationError, load_board_config
from nordic_board_family import resolve_marking, require_base_family, FIRMWARE_FAMILY

ROOT = Path(__file__).resolve().parents[2]
from nordic_tool_format.screen import (ScreenConfig, encode_screen_config, decode_screen_config,
    SCREEN_SCHEMA_VERSION, SCREEN_VERSION, ScreenControllerV2, ScreenColorV2)

PINS = dict(mosi='P0.08', clock='P0.09', cs='P0.10', dc='P0.11',
            reset='P0.12', busy='P0.13', enable='NONE')
EPD = bytes((8,9,10,11,12,13,255,255,255,255,255,0,0))
DEFAULT_PINS = bytes((1,0,0,0,8,9,10,11,12,13,255,255,255,255,255,255))
DEFAULT_AUX = bytes((1,2,3,255,0,255,0,0,255,255,0xd0,7))

def validate_io(pins, aux):
    if (len(pins)!=16 or pins[:2]!=b'\x01\x00' or pins[3] or
        pins[2] or pins[10:]!=b'\xff'*6 or
        not (sorted(pins[4:10])==list(range(8,14)) or pins[4:10]==b'\xff'*6)):
        raise ConfigurationError('Unknown or unqualified Nordic routing')
    if (len(aux)!=12 or aux[0]!=1 or aux[1] not in (2,255) or
        aux[2] not in (0,1,2,3,4,5) or (aux[1]==255 and aux[2]) or
        aux[3:10]!=bytes((255,0,255,0,0,255,255))):
        raise ConfigurationError('Unqualified Nordic AUX route')

def validate_screen_wire(wire):
    if wire==bytes(17): return None
    parsed=decode_screen_config(wire)
    if len(wire)!=17 or wire[0]!=SCREEN_VERSION or parsed is None or encode_screen_config(parsed)!=wire:
        raise ConfigurationError('Invalid release-v1 factory screen wire')
    return wire

def checksum(data):
    value=2166136261
    for byte in data: value=((value ^ byte)*16777619)&0xffffffff
    return value

def exact(value, keys, name):
    if type(value) is not dict or set(value)!=set(keys):
        raise ConfigurationError(f'{name} requires exactly {", ".join(keys)}')

def screen_record(wire):
    if validate_screen_wire(wire) is None:
        raise ConfigurationError('An unconfigured screen has no ID3 record')
    body=b'SP\x01\x11'+wire+bytes(3)
    return body+struct.pack('<I',checksum(body))

def profile(wire, pins=DEFAULT_PINS, aux=DEFAULT_AUX):
    validate_io(pins,aux)
    validate_screen_wire(wire)
    body=b'NFP1'+pins+aux+wire
    return body+struct.pack('<I',checksum(body))

def validate_profile(data):
    if (len(data)!=53 or data[:4]!=b'NFP1' or
        checksum(data[:49])!=struct.unpack_from('<I',data,49)[0]):
        raise ConfigurationError('Unknown Nordic board/default authority')
    validate_io(data[4:20],data[20:32])
    return validate_screen_wire(data[32:49])

def validate(data):
    exact(data,('format_version','board_marking','screen','gpio'),'Nordic configuration')
    if type(data['format_version']) is not int or data['format_version']!=1:
        raise ConfigurationError('Only version1 Nordic configuration is admitted')
    identity=resolve_marking(data['board_marking'])
    exact(data['gpio'],('display','led'),'gpio')
    display=data['gpio']['display'];led=data['gpio']['led']
    exact(display,(*PINS,'active_low'),'gpio.display')
    low=display['active_low']
    if (any(display[k]!=v for k,v in PINS.items()) or type(low) is not list or
        low!=[]):
        raise ConfigurationError('Unqualified/protected GPIO or polarity; this PCB admits fitted screen routes only')
    exact(led,('red','green','blue','active_low'),'gpio.led')
    if led!=dict(red='NONE',green='NONE',blue='NONE',active_low=[]):
        raise ConfigurationError('PriceETag has no qualified LED/RGB routes')
    screen=data['screen']
    geometry=('width','height','visible_x','visible_y','visible_width','visible_height')
    exact(screen,('ic','recipe','color','rotation','mirror_x','mirror_y','native_ui_scale',*geometry),'screen')
    if (type(screen['rotation']) is not int or screen['rotation'] not in (0,90,180,270) or
        any(type(screen[k]) is not bool for k in ('mirror_x','mirror_y')) or
        any(type(screen[k]) is not int for k in ('recipe','native_ui_scale',*geometry))):
        raise ConfigurationError('Invalid screen types/absolute orientation')
    try:
        wire=encode_screen_config(ScreenConfig(schema_version=SCREEN_SCHEMA_VERSION,
            driver=int(ScreenControllerV2[screen['ic']]),recipe=screen['recipe'],
            color=int(ScreenColorV2[screen['color']]),
            transform=screen['rotation']//90|(4 if screen['mirror_x'] else 0)|(8 if screen['mirror_y'] else 0),
            native_ui_scale=screen['native_ui_scale'],**{k:screen[k] for k in geometry}))
    except (ValueError,KeyError,TypeError) as error:
        raise ConfigurationError(f'Invalid registered IC/recipe/geometry: {error}') from error
    return dict(**identity,format_version=1,baseline='inktag-release-v1',layout_marker='N3V1',screen_wire_hex=wire.hex(),
        board_profile_hex=profile(wire).hex(),screen_record_hex=screen_record(wire).hex(),
        led_available=False,aux_available=False,button='P0.02 fixed; no configurable remap',
        physical_qualification=False)

def read_config(path):
    result=validate(load_board_config(path))
    result['configuration_sha256']=hashlib.sha256(Path(path).read_bytes()).hexdigest()
    return result

def configure_base(base, configuration):
    result=read_config(configuration)
    full=runpy.run_path(str(ROOT/'tools/sdk/nordic-full-settings.py'))
    if Path(base).suffix.lower()!='.zip':
        raise ValueError('Full requires complete app-full.zip with its signed App')
    from nordic_full_bundle import verify_bundle
    candidate,package_review=verify_bundle(base)
    if not candidate or any(x<0 or x>=full['IMAGE'] for x in candidate):
        raise ValueError('Full candidate overlaps image/UICR/unknown space')
    image=bytes(candidate.get(x,255) for x in range(full['FLASH_BYTES']))
    full['TAIL']['validate_selectors'](image)
    # Native package geometry and metadata, not candidate/source approval.
    require_base_family(result['firmware_family'], FIRMWARE_FAMILY)
    size,crc,_=full['dfu_validate'](image)
    full['assets_validate'](image)
    generation,records=full['journal_decode'](image[full['SETTINGS']:full['RECOVERY']])
    validate_profile(records[1])
    records[1]=bytes.fromhex(result['board_profile_hex'])
    records[3]=bytes.fromhex(result['screen_record_hex'])
    # Neutral factory defaults. Per-unit Full reseeds names from immutable FICR.
    records[7]=full['FACTORY']['device_settings_record'](b'',b'',aux=records[1][20:32])
    records[12]=struct.pack('<IBBHII',full['FACTORY']['BOOT_DISPLAY_MAGIC'],1,1,0,size,crc)
    seeded=full['journal_encode']((generation+1)&0xffffffff,records)
    candidate.update({full['SETTINGS']+i:b for i,b in enumerate(seeded)})
    result.update(package_review)
    result.update(base_firmware_family=FIRMWARE_FAMILY,app_bytes=size,app_crc32=f'{crc:08X}',base_sha256=hashlib.sha256(Path(base).read_bytes()).hexdigest(),
        executable_bytes_changed=False,user_data='Full clears user data except verified same-device native Pro; confirmation defaults to N',
        warning='Full clears settings/password/Find/user fonts/internal images and diagnostic history; retains only verified same-device native Pro (proven absent remains Free); FICR/UICR/calibration retained; native DFU state reinitialized; external Flash not accessed',
        confirmation_default='N',ota_anti_rollback_unchanged=True)
    return candidate,result

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--config',type=Path,required=True)
    p.add_argument('--base',type=Path,required=True)
    p.add_argument('--output',type=Path)
    a=p.parse_args()
    candidate,result=configure_base(a.base,a.config)
    if a.output:
        if a.output.exists(): raise ValueError('Output must be new')
        runpy.run_path(str(ROOT/'tools/sdk/merge-ihex.py'))['write_hex'](a.output,candidate)
    print(json.dumps(result,indent=2))

if __name__=='__main__':
    try:
        main()
    except (ValueError, OSError, KeyError) as error:
        print(f'Configured Full refused before device access: {error}',file=sys.stderr)
        raise SystemExit(2)
