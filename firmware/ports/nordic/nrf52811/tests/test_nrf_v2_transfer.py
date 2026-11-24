"""Exercise the shipping Nordic WRITE handler with mocked hardware sinks."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'compat/nordic/src/inktag_nrf_protocol_v2.inc').read_text()
start = source.index('typedef struct {\n    ble_epd_t *epd;')
end = source.index('static void epd_v2_transfer_commit(', start)
shipping = source[start:end]
prefix = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "inktag_protocol_core.h"
#include "inktag_packbits.h"
#include "inktag_nrf_image_store.h"
#define INKTAG_NRF_IMAGE_STORE_ENABLED 1
#define INKTAG_POWER_OPERATION_FLASH 1
typedef struct { void *epd; } ble_epd_t;
typedef struct { bool active; uint8_t transaction_id; inktag_transfer_descriptor_t descriptor;
    uint32_t written, running_crc, last_activity_ticks; } transfer_t;
static transfer_t m_v2_transfer;
static uint8_t bytes[8000], qr_bytes[8000];
static unsigned used, calls, resets, responses;
static bool low_power, fail_sink;
static uint8_t last_status;
static uint8_t overlay_rotation;
static uint32_t last_ack;
static struct { uint8_t data[8000]; } qr;
static void *qr_unused;
#define epd_v2_qr_staging() (&qr)
static void app_feed_wdt(void) {}
static uint32_t app_timer_cnt_get(void) { return 17; }
static bool inktag_nrf_health_operation_allowed(int operation, bool force) {
    (void)operation; (void)force; return !low_power;
}
static inktag_status_v2_t epd_v2_store_status(inktag_image_store_result_t result) {
    return result == INKTAG_IMAGE_STORE_OK ? INKTAG_STATUS_OK : INKTAG_STATUS_IO_ERROR;
}
inktag_image_store_result_t inktag_image_store_append(const uint8_t *data, uint16_t length) {
    ++calls;
    if (fail_sink && calls == 2) return INKTAG_IMAGE_STORE_IO_ERROR;
    assert(used + length <= sizeof(bytes)); memcpy(bytes+used,data,length); used+=length;
    return INKTAG_IMAGE_STORE_OK;
}
static bool epd_brand_overlay_chunk(ble_epd_t *epd, uint8_t format, uint8_t segment,
    uint32_t offset, uint8_t *data, uint16_t length, uint8_t reading_rotation) {
    (void)epd; (void)format; (void)segment; (void)offset;
    overlay_rotation=reading_rotation;
    for (unsigned i=0;i<length;++i) data[i]^=0x40; return true;
}
static bool EPD_DriverWriteRam(void *epd, bool begin, bool black, uint8_t *data, uint16_t length) {
    (void)epd; (void)begin; (void)black;
    return inktag_image_store_append(data,length)==INKTAG_IMAGE_STORE_OK;
}
static void epd_v2_transfer_reset(ble_epd_t *epd, bool abort) {
    (void)epd; (void)abort; ++resets; m_v2_transfer.active=false;
}
static void epd_v2_send_response(ble_epd_t *epd, uint8_t op, uint8_t tx,
    inktag_status_v2_t status, const uint8_t *payload, uint16_t length) {
    (void)epd; (void)op; (void)tx; ++responses; last_status=status;
    if (length==4) last_ack=inktag_read_u32_le(payload);
}
#define epd_v2_reply_status(p,o,t,s) epd_v2_send_response(p,o,t,s,NULL,0u)

'''
tests = r'''
static void begin(uint8_t object, bool compressed) {
    memset(&m_v2_transfer,0,sizeof(m_v2_transfer));
    m_v2_transfer.active=true; m_v2_transfer.transaction_id=3;
    m_v2_transfer.running_crc=UINT32_MAX; m_v2_transfer.descriptor.object_type=object;
    m_v2_transfer.descriptor.black_mask=compressed?0x81:1;
    m_v2_transfer.descriptor.segment_count=2;
    m_v2_transfer.descriptor.segment_size[0]=4000;
    m_v2_transfer.descriptor.segment_size[1]=4000;
    m_v2_transfer.descriptor.total_size=8000;
    calls=used=resets=responses=0; low_power=fail_sink=false; last_ack=0; overlay_rotation=255;
}
int main(void) {
    ble_epd_t epd={(void*)1};
    uint8_t packet[16]={0}; uint8_t expected[650];
    memset(expected,0x5a,sizeof(expected));
    inktag_write_u16_le(packet+4,650);
    for(unsigned i=6;i<16;i+=2) {packet[i]=0xff;packet[i+1]=0x5a;}
    begin(INKTAG_TRANSFER_OBJECT_STORED_IMAGE,true);
    epd_v2_transfer_write(&epd,0,3,packet,16);
    assert(last_status==INKTAG_STATUS_OK && last_ack==650 && responses==1);
    assert(used==650 && memcmp(bytes,expected,650)==0);
    assert(m_v2_transfer.running_crc==inktag_crc32_update(UINT32_MAX,expected,650));
    /* Wrong transaction, offset, decoded length, truncated token and overflow
       produce no writes and preserve the lease for a corrected request. */
    begin(INKTAG_TRANSFER_OBJECT_STORED_IMAGE,true);
    epd_v2_transfer_write(&epd,0,4,packet,16);
    assert(last_status==INKTAG_STATUS_INVALID_STATE && calls==0);
    packet[0]=1; epd_v2_transfer_write(&epd,0,3,packet,16); packet[0]=0;
    assert(last_status==INKTAG_STATUS_INVALID_REQUEST && calls==0);
    packet[4]=1; epd_v2_transfer_write(&epd,0,3,packet,16);
    assert(last_status==INKTAG_STATUS_INVALID_REQUEST && calls==0);
    inktag_write_u16_le(packet+4,650); epd_v2_transfer_write(&epd,0,3,packet,15);
    assert(last_status==INKTAG_STATUS_INVALID_REQUEST && calls==0);
    m_v2_transfer.descriptor.total_size=600;
    epd_v2_transfer_write(&epd,0,3,packet,16);
    assert(last_status==INKTAG_STATUS_INVALID_REQUEST && calls==0);
    begin(INKTAG_TRANSFER_OBJECT_STORED_IMAGE,true); low_power=true;
    epd_v2_transfer_write(&epd,0,3,packet,16);
    assert(last_status==INKTAG_STATUS_LOW_POWER && calls==0 && resets==1);
    begin(INKTAG_TRANSFER_OBJECT_STORED_IMAGE,true); fail_sink=true;
    epd_v2_transfer_write(&epd,0,3,packet,16);
    assert(last_status==INKTAG_STATUS_IO_ERROR && resets==1 && last_ack==0);
    assert(!m_v2_transfer.active);
    begin(INKTAG_TRANSFER_OBJECT_DIRECT_FRAME,true);
    epd_v2_transfer_write(&epd,0,3,packet,16);
    assert(last_status==INKTAG_STATUS_OK && used==650);
    /* The branding sink mutates display output; CRC still covers host bytes. */
    assert(bytes[0]==(0x5a^0x40));
    assert(m_v2_transfer.running_crc==inktag_crc32_update(UINT32_MAX,expected,650));
    begin(INKTAG_TRANSFER_OBJECT_STORED_IMAGE,false);
    uint8_t raw[17]={0}; memset(raw+4,0x33,13);
    epd_v2_transfer_write(&epd,0,3,raw,sizeof(raw));
    assert(last_status==INKTAG_STATUS_OK && last_ack==13 && bytes[0]==0x33);
    /* Every reading direction reaches the direct display sink for raw and
     * compressed input. Storage retains host bytes and never burns in a badge. */
    for(unsigned rotation=0;rotation<4;rotation++) for(unsigned rle=0;rle<2;rle++) {
        for(unsigned direct=0;direct<2;direct++) {
            uint8_t raw_original[13];memset(raw_original,0x33,sizeof(raw_original));
            memcpy(raw+4,raw_original,sizeof(raw_original));
            begin(direct?INKTAG_TRANSFER_OBJECT_DIRECT_FRAME:INKTAG_TRANSFER_OBJECT_STORED_IMAGE,rle);
            m_v2_transfer.descriptor.black_mask|=rotation<<2;
            epd_v2_transfer_write(&epd,0,3,rle?packet:raw,rle?sizeof(packet):sizeof(raw));
            assert(last_status==INKTAG_STATUS_OK && last_ack==(rle?650:13));
            assert(overlay_rotation==(direct?rotation:255));
            uint8_t original=rle?0x5a:0x33;
            assert(bytes[0]==(direct?(original^0x40):original));
            assert(m_v2_transfer.running_crc==inktag_crc32_update(UINT32_MAX,rle?expected:raw_original,rle?650:13));
        }
    }
    (void)qr_unused; (void)qr_bytes;
    puts("PASS Nordic shipping WRITE: raw/RLE, decoded CRC/ACK, malformed zero-side-effects, power/I/O abort");
}
'''
with tempfile.TemporaryDirectory() as directory:
    fixture = Path(directory) / 'transfer.c'
    binary = Path(directory) / 'transfer'
    fixture.write_text(prefix + shipping + tests)
    subprocess.run([os.environ.get('CC', 'gcc'), '-std=c99', '-Wall', '-Werror',
        '-Wno-misleading-indentation', '-Ifirmware/common/protocol', '-Ifirmware/common/epd',
        '-Ifirmware/ports/nordic/common/src', str(fixture),
        'compat/common/protocol/inktag_protocol_core.c',
        'compat/common/protocol/inktag_packbits.c', '-o', str(binary)], cwd=ROOT, check=True)
    subprocess.run([str(binary)], check=True)
