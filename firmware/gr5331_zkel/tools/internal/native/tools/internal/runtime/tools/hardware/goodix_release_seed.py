"""Release-v1 basic records, derived on host from authenticated PCB/identity.

Pure codecs only. No connection, signing, EEPROM programming or boot helper.
Existing shared URL/QR producers are reused; no private protocol client.
"""
from pathlib import Path
import struct,sys,zlib
import goodix_pin_config as pins
import goodix_host_nvds as nvds

from goodix_offline.device_link import build_device_link
from goodix_offline.device_qr import encode_device_qr
from goodix_offline.nfc_command import encode_uri_ndef


def backend(chip,profile):
    nvds.require(type(profile) is bytes and len(profile)==76,'Incomplete configured Factory input')
    matches=[h for h,f in pins.FACTS.items() if
        (f['family'],f['board_id'],f['mcu'])==(profile[5],profile[6],profile[19]) and
        f['mcu']=={'GR5331':0x31,'GR5513':0x13,'GR5515':0x15}.get(chip)]
    nvds.require(len(matches)==1,'Unqualified configured Factory MCU/backend')
    h=matches[0]
    wire,screen=pins.decode_factory_records(h,profile[:12],profile[12:])
    return h,wire,screen


def basic(chip,address,profile):
    h,wire,screen=backend(chip,profile)
    name='IKT_GR_'+address[-2:].hex().upper()
    label='InkTag '+name[-4:]
    record=bytearray(60);record[:4]=b'GSV1'
    record[4:7]=bytes((1,len(name),len(label)))
    record[7:7+len(name)]=name.encode('ascii')
    record[26:26+len(label)]=label.encode('ascii')
    record[50:56]=bytes((1,0,10,0,9,0))
    record[56:60]=struct.pack('<I',pins.fnv(record[:56]))
    panel=b'SP'+bytes((1,17))+screen+bytes(3)
    panel+=struct.pack('<I',pins.fnv(panel))
    defaults={0x5210:profile,0x5207:pins.user_record(h,wire),
              0x5203:panel,0x5205:bytes(record)}
    url=build_device_link(name)
    qr=encode_device_qr(url)
    off=bytes((1,0,qr.modules))+struct.pack('<HI',len(qr.data),zlib.crc32(qr.data))+qr.data
    nvds.check_off(off)
    if pins.FACTS[h]['nfc_fitted']:
        ndef=encode_uri_ndef(url)
        defaults[0x520f]=bytes((1,len(ndef)))+ndef
    return off,defaults


def prepare(chip,address,kind,signed_recipe,profile,*,retained_vendor=(),family=None,
            retained_activation=None):
    return _prepare(nvds.prepare,chip,address,kind,signed_recipe,profile,
        retained_vendor=retained_vendor,family=family,retained_activation=retained_activation)


def prepare_full(chip,address,kind,signed_recipe,profile,*,retained_vendor=(),family=None,
                 retained_activation=None):
    return _prepare(nvds.prepare_full,chip,address,kind,signed_recipe,profile,
        retained_vendor=retained_vendor,family=family,retained_activation=retained_activation)


def _prepare(encode,chip,address,kind,signed_recipe,profile,*,retained_vendor,family,
             retained_activation):
    # Authentication of the original bounded recipe is not waived by replacing
    # its public default QR with a deterministic per-unit managed URL matrix.
    nvds.check_recipe(signed_recipe,chip)
    off,records=basic(chip,address,profile)
    recipe={**signed_recipe,'offHex':off.hex()}
    if retained_activation is not None:
        from goodix_activation_retention import TAG,native_token
        native_token(retained_activation)
        records[TAG]=retained_activation
    return encode(chip,address,kind,recipe,
        extra_records=(*records.items(),*retained_vendor),family=family)
