"""Actual native reader accepts the generic journal without UID/name/QR data."""
from pathlib import Path
import argparse,os,re,struct,tempfile
from fixtures import nrf11_initialization_oracle as oracle
from test_nrf_native_v1 import compile_run,ROOT

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--settings',type=Path)
    parser.add_argument('--internal',type=Path)
    args=parser.parse_args()
    assert bool(args.settings)==bool(args.internal)
    prefix=(ROOT/'tests/fixtures/nrf_native_v1.inc').read_text().split('int main(void)',1)[0]
    prefix=re.sub(r'^#define INKTAG_BOARD_[^\n]*\n','',prefix,flags=re.M)
    body=r'''
int main(int argc,char **argv){
 assert(argc==3);
 FILE *f=fopen(argv[1],"rb");assert(f && fread(bytes,1,sizeof(bytes),f)==sizeof(bytes));fclose(f);
 f=fopen(argv[2],"rb");assert(f && fseek(f,0x2f000,SEEK_SET)==0);
 assert(fread(test_native_dfu,1,sizeof(test_native_dfu),f)==sizeof(test_native_dfu));fclose(f);
 cold();assert(inktag_nrf_pin_epoch_ready());
 epd_config_t routing;epd_config_read(&routing);assert(!epd_config_empty(&routing) && routing.mosi_pin!=255);
 epd_screen_config_t screen;assert(epd_screen_config_read(&screen));
 epd_device_settings_t settings;assert(epd_device_settings_read(&settings));
 assert(!settings.name_length && !settings.label_length);
 nrf11_user_record_t user;inktag_display_settings_t display;
 assert(nrf11_user_read(&user,&display)==INKTAG_SETTINGS_JOURNAL_READ_MISSING);
 epd_device_info_qr_t qr;assert(!epd_device_info_qr_read(0,&qr));
 assert(inktag_nrf_config_store_last_error()==NRF_ERROR_NOT_FOUND);
 assert(!writes && !erases && !sessions);
 puts("PASS actual native generic defaults: configured display admitted, missing user/QR accepted, no provisioning writes");
 return 0;
}
'''
    with tempfile.TemporaryDirectory() as temporary:
        directory=Path(temporary)
        if args.settings:
            settings=args.settings.resolve();internal=args.internal.resolve()
        else:
            config=oracle.read_config(ROOT/'../../../../tools/platforms/nordic/nrf52811/programmer/configs/EL022HWRL.toml')
            factory=b'N1P1'+bytes.fromhex(config['factory_pins_hex'])+bytes.fromhex(config['screen_wire_hex'])+struct.pack('<H',config['active_low'])
            factory+=struct.pack('<I',oracle.fnv(factory))
            screen=b'SP\x01\x11'+bytes.fromhex(config['screen_wire_hex'])+bytes(3);screen+=struct.pack('<I',oracle.fnv(screen))
            boot=struct.pack('<IBBHII',0x3153424e,1,1,0,8,0x12345678)
            journal=struct.pack('<4I',0x31505349,1,0xfffffffe,0xffffffff)+b''.join(oracle.compact_record(k,v) for k,v in [(1,factory),(3,screen),(12,boot)])
            settings=directory/'settings.bin';settings.write_bytes(journal.ljust(8192,b'\xff'))
            data=bytearray(b'\xff'*196608)
            values=bytearray(92);struct.pack_into('<I',values,4,2);struct.pack_into('<I',values,8,0x100ca);struct.pack_into('<I',values,24,8);struct.pack_into('<I',values,32,1)
            struct.pack_into('<I',values,0,__import__('zlib').crc32(values[4:92]));data[0x2f000:0x2f05c]=values
            internal=directory/'internal.bin';internal.write_bytes(data)
        for sanitize in (False,True):
            previous=os.environ.get('INKTAG_TEST_SANITIZERS')
            if sanitize:os.environ['INKTAG_TEST_SANITIZERS']='1'
            else:os.environ.pop('INKTAG_TEST_SANITIZERS',None)
            try:
                target=directory/('san' if sanitize else 'normal');target.mkdir()
                compile_run(target,prefix+body,(settings,internal))
            finally:
                if previous is None:os.environ.pop('INKTAG_TEST_SANITIZERS',None)
                else:os.environ['INKTAG_TEST_SANITIZERS']=previous

if __name__=='__main__':main()
