"""Synthetic images and an ephemeral test signer; never a production artifact."""
from pathlib import Path
import hashlib,io,json,struct,sys,zipfile,zlib,importlib.util
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec,utils
ROOT=Path(__file__).resolve().parents[2]
INTERNAL=ROOT/'../../../../tools/platforms/nordic/nrf52811/programmer/internal'
def load(name='nrf11_full_test'):
    source=INTERNAL/'nrf11_full.py'
    if not source.exists():source=INTERNAL/'nrf11_full_format.py'  # Public format-only fixture closure.
    spec=importlib.util.spec_from_file_location(name,source)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    return module
def varint(value):
    out=bytearray()
    while value>=128:out.append((value&127)|128);value>>=7
    out.append(value);return bytes(out)
def fields(values):
    out=b''
    for k,v in values.items():
        out+=varint((k<<3)|(2 if isinstance(v,bytes) else 0))
        out+=varint(len(v))+v if isinstance(v,bytes) else varint(v)
    return out
def zip_bytes(files):
    out=io.BytesIO()
    with zipfile.ZipFile(out,'w',zipfile.ZIP_DEFLATED) as archive:
        for n,v in files.items():archive.writestr(n,v)
    return out.getvalue()
def fixture(directory,app_tail=None,epoch=1,app_version=200):
    directory=Path(directory);directory.mkdir(exist_ok=True)
    full=load();key=ec.generate_private_key(ec.SECP256R1());pub=key.public_key().public_numbers()
    full.PUBLIC_KEY_LE=pub.x.to_bytes(32,'little')+pub.y.to_bytes(32,'little')
    app_tail=full.FACTORY_PIN_MARKER if app_tail is None else app_tail
    version=(epoch<<16)|app_version
    app=bytearray(b'TEST-APP'*96+app_tail);struct.pack_into('<II',app,0,0x20006000,0x19009);app=bytes(app)
    init=fields({1:version,2:52,3:b'\xa6\x02',4:0,5:0,6:0,7:len(app),8:fields({1:3,2:hashlib.sha256(app).digest()[::-1]}),9:0,10:fields({1:1,2:b''})})
    r,s=utils.decode_dss_signature(key.sign(init,ec.ECDSA(hashes.SHA256())))
    dat=fields({2:fields({1:fields({1:1,2:init}),2:0,3:r.to_bytes(32,'little')+s.to_bytes(32,'little')})})
    ota=zip_bytes({'manifest.json':json.dumps({'manifest':{'application':{'bin_file':'test.bin','dat_file':'test.dat'}}}).encode(),'test.bin':app,'test.dat':dat})
    sd=b'TEST-SOFTDEVICE'*9;rec=bytearray(b'TEST-RECOVERY'*5);struct.pack_into('<II',rec,0,0x20006000,0x2a009)
    memory={}
    def put(a,b):memory.update({a+i:v for i,v in enumerate(b)})
    put(0x1000,sd);put(0x19000,app);put(0x2a000,rec)
    put(0xff8,struct.pack('<II',0x2a000,0x2e000))
    settings=bytearray(b'\xff'*0x323)
    for offset,value in [(4,2),(8,version),(12,0),(0x10,0),(0x14,0),(0x2c,0),(0x18,len(app)),(0x1c,zlib.crc32(app)),(0x20,1),(0x34,len(sd))]:struct.pack_into('<I',settings,offset,value)
    settings[0x260]=settings[0x2a1]=1
    for offset,value in [(0x261,zlib.crc32(sd)),(0x2a2,zlib.crc32(app))]:struct.pack_into('<I',settings,offset,value)
    struct.pack_into('<I',settings,0x25c,zlib.crc32(settings[0x260:0x323]))
    struct.pack_into('<I',settings,0,zlib.crc32(settings[4:92]))
    put(0x2e000,settings);put(0x2f000,settings)
    path=directory/'test.hex';full.write_hex(path,memory)
    payload=bytes((i*19+7)&255 for i in range(12228));assets=struct.pack('<IIII',0x44464b49,1,len(payload),zlib.crc32(payload))+payload+b'\xff'*44
    files={'full.hex':path.read_bytes(),'app-ota.zip':ota,'gui-assets.bin':assets}
    base=dict(format_version=1,mcu='nrf52811',firmware_family='aesl22_nrf',backend_ids=['nrf52811_asel22'],hw_version=52,config_schema=1,baseline='inktag-release-v1',layout_version=1,layout_marker='N1V1',compatibility_epoch=epoch,factory_format='N1P1',user_format='N1R1',images={n:dict(bytes=len(v),sha256=full.sha(v)) for n,v in files.items()})
    files['base.json']=json.dumps(base).encode();bundle=directory/'test-full.zip';bundle.write_bytes(zip_bytes(files))
    ficr=bytearray(b'\xff'*4096);struct.pack_into('<I',ficr,0x100,0x52811);struct.pack_into('<II',ficr,0x10c,24,192);struct.pack_into('<II',ficr,0x10,4096,48);ficr[0x60:0x68]=bytes(range(1,9));ficr[0xa4:0xaa]=bytes.fromhex('cdab1793eee3');struct.pack_into('<I',ficr,0xa0,1)
    config=ROOT/'../../../../tools/platforms/nordic/nrf52811/programmer/configs/EL022HWRL.toml'
    # Pure shared codecs are frozen module bytes, with no complete CLI or BLE.
    if str(INTERNAL) not in sys.path:sys.path.insert(0,str(INTERNAL))
    return full,bundle,config,bytes(ficr),files,memory
