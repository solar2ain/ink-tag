"""Use the real SPI asset adapter to check CRC/schema gates and RAM text load."""
from pathlib import Path
import runpy
import subprocess
import tempfile
import os
import struct

ROOT = Path(__file__).resolve().parents[1]
generate = runpy.run_path(str(ROOT/'build-support/gui/ram-gui-tables.py'))['generate']
package = runpy.run_path(str(ROOT/'build-support/gui/package-font-assets.py'))['package']
DRIVER = r'''
#define INKTAG_GUI_EXTERNAL_FONT_ASSETS 1
#define INKTAG_GUI_ASSET_READ_FAILURE_AWARE 1
#define INKTAG_NRF_GUI_RAM_TABLES 1
#define INKTAG_BOARD_LAZY_DISPLAY_BUS 1
#define INKTAG_BOARD_EXTERNAL_FLASH_JEDEC_ID 0xA14013u
#define INKTAG_BOARD_EXTERNAL_FLASH_ASSET_OFFSET 0x2000u
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "inktag_nrf_external_assets.c"
#include "u8g2_font.c"
static uint8_t data[12288], expected[INKTAG_NRF_GUI_TABLES_SIZE];
static unsigned sessions, reads, fail, maintenance;
void ble_epd_gui_maintenance(void) { maintenance++; }
bool inktag_nrf_external_flash_begin(void) { assert(!sessions); sessions++; return true; }
void inktag_nrf_external_flash_end(void) { assert(sessions); sessions--; }
bool inktag_nrf_external_flash_read(uint32_t address, void *out, uint32_t bytes) {
    assert(sessions && address >= 0x2000u && address + bytes <= 0x5000u);
    if (++reads == fail) return false;
    memcpy(out, data + address - 0x2000u, bytes); return true;
}
static void reset(void) { assert(!sessions); s_validated=-1; s_session_open=false; reads=0;
    memset(s_cache_virtual_base,0,sizeof(s_cache_virtual_base));s_cache_cursor=0; }
int main(int argc, char **argv) {
    assert(argc==2); FILE *file=fopen(argv[1],"rb"); assert(file);
    assert(fread(data,sizeof(data),1,file)==1); fclose(file);
    memcpy(expected,data+10516,sizeof(expected));
    reset(); assert(inktag_gui_assets_begin());
    assert(!memcmp(inktag_nrf_gui_tables,expected,sizeof(expected)));
    assert(inktag_gui_assets_begin()); assert(sessions==1);
    for (unsigned n=0;n<10500;n++) assert(inktag_gui_asset_read_byte((const uint8_t *)(uintptr_t)(0x60000010u+n))==data[16+n]);
    assert(maintenance >= (10500u+255u)/256u);
    assert(inktag_nrf_gui_assets_ok());
    inktag_gui_assets_end(); assert(!sessions);
    /* A cache-fill read failure is observable by the native renderer; retry
     * starts a new session and must refill the invalidated line. */
    reset();assert(inktag_gui_assets_begin());fail=reads+1;
    assert(inktag_gui_asset_read_byte((const uint8_t *)(uintptr_t)0x60000010u)==0xff);
    assert(!inktag_nrf_gui_assets_ok());
    assert(inktag_gui_asset_font_read_failed());
    /* The real font decoder must not continue reading or shift by an invalid
     * count once the physical asset read fails. */
    u8g2_font_decode_t decode={0};
    u8g2_font_t font={0};
    unsigned stopped=reads;
    for(unsigned count=0;count<256u;++count) {
        assert(u8g2_font_decode_get_unsigned_bits(&decode,(uint8_t)count)==0u);
        assert(u8g2_font_decode_get_signed_bits(&decode,(uint8_t)count)==0);
    }
    assert(u8g2_font_get_glyph_data(&font,0x4e00u)==NULL);
    assert(inktag_glyph_cursor==0u);
    assert(reads==stopped);
    inktag_gui_assets_end();fail=0;
    assert(inktag_gui_assets_begin());assert(inktag_gui_asset_read_byte((const uint8_t *)(uintptr_t)0x60000010u)==data[16]);
    assert(inktag_nrf_gui_assets_ok());inktag_gui_assets_end();
    reset(); data[16+ASSET_PAYLOAD_SIZE-1]^=1; assert(!inktag_gui_assets_begin()); assert(!sessions);
    data[16+ASSET_PAYLOAD_SIZE-1]^=1;
    reset(); data[4]=2; assert(!inktag_gui_assets_begin()); assert(!sessions); data[4]=1;
    reset(); data[8]^=1; assert(!inktag_gui_assets_begin()); assert(!sessions); data[8]^=1;
    /* A different, internally CRC-correct GUI projection is still incompatible
     * with the application's compiled table offsets. */
    reset(); data[16]^=1;
    uint32_t other_crc=inktag_crc32_update(UINT32_MAX,data+16,ASSET_PAYLOAD_SIZE)^UINT32_MAX;
    inktag_write_u32_le(data+12,other_crc);
    assert(!inktag_gui_assets_begin()); assert(!sessions && reads==1);
    data[16]^=1; inktag_write_u32_le(data+12,INKTAG_NRF_GUI_EXPECTED_CRC);
    /* Every manifest/CRC-page and final RAM-load read may fail. */
    for (fail=1;fail<=2+(ASSET_PAYLOAD_SIZE+255)/256;fail++) { reset(); assert(!inktag_gui_assets_begin()); assert(!sessions); }
    fail=0; reset(); assert(inktag_gui_assets_begin()); inktag_gui_assets_end();
    printf("PASS real GUI asset adapter: all 10500 font bytes and %u RAM table bytes; bad CRC/schema/length and %u read failures reject without open sessions\n", (unsigned)INKTAG_NRF_GUI_TABLES_SIZE, (unsigned)(2+(ASSET_PAYLOAD_SIZE+255)/256));
}
'''
with tempfile.TemporaryDirectory() as temp:
    out=Path(temp)
    generate(ROOT/'compat/common/gui',out)
    pool=(out/'tables.bin').read_bytes()
    image=package(bytes(n&255 for n in range(10500)),pool)
    (out/'assets.bin').write_bytes(image)
    (out/'inktag_nrf_gui_asset_binding.h').write_text('#define INKTAG_NRF_GUI_EXPECTED_CRC 0x%08xu\n' % struct.unpack_from('<I',image,12)[0])
    runpy.run_path(str(ROOT/'build-support/gui/cache-font-glyphs.py'))['generate'](ROOT/'compat/common/gui/u8g2_font.c',out/'u8g2_font.c',16)
    (out/'driver.c').write_text(DRIVER,encoding='utf-8')
    includes=['-I'+str(ROOT/p) for p in ('tests/mocks/nrf_external','compat/nordic/src','compat/common/gui','compat/common/protocol','compat/common/epd')]
    flags=['-O2'] if not os.environ.get('INKTAG_TEST_SANITIZERS') else ['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-sanitize-recover=all','-no-pie']
    subprocess.run(['cc','-std=gnu99',*flags,*includes,'-I'+str(out),str(out/'driver.c'),str(ROOT/'compat/common/protocol/inktag_protocol_core.c'),'-o',str(out/'test')],check=True)
    subprocess.run([str(out/'test'),str(out/'assets.bin')],check=True)
