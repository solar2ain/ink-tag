"""PN532 serial transport for compatible InkTag devices."""
from pathlib import Path
import secrets
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'nfc/provider'))

PN532_AUTO_INF_LIMIT = 240


class Link:
    def __init__(self, args):
        self.args = args
        self.reader = self.pipe = self.session = self.stream = None
        self.uid = b''
        self.kind = ''
        self.wait = 30.0
        self.grouped = False

    def connect(self):
        from ink_nfc_fmsc.pn532 import Pn532, Activation, parse_ats, RfTransportError, crc_a
        self.reader = Pn532(self.args.port, baudrate=115200, wake_mode='standard')

        # Qualified HSU wake only, before SAM; never a tag write or APDU replay.
        import serial
        wake = serial.Serial(None, 115200, timeout=.1, write_timeout=1.5)
        try:
            wake.dtr = wake.rts = False
            wake.port = self.args.port
            wake.open()
            wake.write(bytes.fromhex('5555000000'))
            wake.flush()
            time.sleep(.5)
        finally:
            wake.close()
        self.reader.open()
        self.reader.command(bytes.fromhex('1214'), 0x13, name='SetParameters AutoRATS')
        raw = self._wait_for_target()
        if len(raw) < 6 or raw[:2] != b'\1\1':
            raise ValueError('未选中唯一NFC目标，请检查位置；未发送图像')
        length = raw[5]
        if length not in (4, 7, 10) or len(raw) < 6+length:
            raise ValueError('寻卡返回的UID长度无效')
        self.uid = raw[6:6+length]
        if self.args.uid is not None and self.uid != self.args.uid:
            raise ValueError(f'UID不匹配：期望{self.args.uid.hex()}，实际{self.uid.hex()}')
        self.reader.rf_on = True
        if raw[4] & 0x20:
            from ink_nfc_fmsc.isodep import IsoDepSession
            ats = parse_ats(raw[6+length:])
            activation = Activation(self.uid, raw[2:4], raw[4], ats, 0x26)
            self.session = IsoDepSession(self.reader, activation, max_inf_bytes=PN532_AUTO_INF_LIMIT)
            self.kind = 'iso-dep'
            return
        if raw[4] != 0 or length != 7 or self.uid[0] != 4:
            raise ValueError(f'不支持的NFC目标 SAK={raw[4]:02X}；未发送图像')
        from inktag_gt_stream import Stream, Type2Pipe
        self.kind = 'type2-sram'
        nonce = secrets.randbelow(0xffffffff)+1
        self.pipe = Type2Pipe(self.reader, crc_a, RfTransportError, self.uid, nonce)
        self.pipe.adopt_selection(raw)
        if self.args.command == 'read':
            return  # Standard NDEF discovery, never an image or stream request.
        # Standard NDEF discovery selects the ordinary mirror compatibility
        # path on an automatic receiver. Probe its live SRAM/version first;
        # the first D1 must still prove IKT1/UID before image DATA or D4.
        for attempt in range(6):
            status = self.pipe.session()
            if self.use_direct_rx(status):
                return
            if status[0] & 0x43 != 3 or not status[6] & 1 or attempt == 5:
                break
            # Let a field-woken MCU finish its verified SRAM seed before
            # ordinary NDEF discovery would select the compatibility path.
            time.sleep(.05)
        if self.args.command in ('send-cmd', 'clear', 'sync-time'):
            return  # Older firmware keeps its ordinary volatile NDEF endpoint.
        self.pipe.sector(0)
        if self.pipe.read(0)[:7] != self.uid:
            raise ValueError('价签身份不匹配，未发送图片。')
        prefix = b''.join(self.pipe.read(p) for p in (4, 8, 12, 16))
        if (len(prefix) != 64 or prefix[0] != 3 or not 4 <= prefix[1] <= 61 or
                prefix[2:4] != b'\xd1\1' or prefix[5] != 0x55 or
                prefix[1] != prefix[4]+4 or prefix[prefix[1]+2] != 0xfe):
            raise ValueError('未找到兼容的 InkTag NFC 传图标签。')
        if self.pipe.version() != bytes.fromhex('0004040502011503'):
            raise ValueError('不支持此标签的 InkTag NFC 传图。')
        self.pipe.mirror_d1 = True
        self.stream = Stream(nonce, self.pipe.exchange, direct=True)

    def use_direct_rx(self, status, *, refresh=False):
        """Qualify mirror/RX and version; the caller still validates real D1."""
        nc=status[0]&0x43
        if nc not in (3,0x41) or not status[6]&1:
            return False
        if self.stream is None or refresh:
            from inktag_gt_stream import Stream, Type2Pipe
            if nc==3:
                from inktag_gt_stream import query_reply
                if status[2]!=1 or status[6]&0x40:return False
                self.pipe.sector(0)
                header=self.pipe.read(0)
                if header[:7]!=self.uid or header[12]!=0xe1:
                    raise ValueError('Type2 identity/CC mismatch; zero SRAM writes')
                prefix=b''.join(self.pipe.read(page) for page in (4,8,12,16))
                uri=(prefix[0]==3 and 4<=prefix[1]<=61 and prefix[2:4]==b'\xd1\1'
                     and prefix[5]==0x55 and prefix[prefix[1]+2]==0xfe)
                if not uri and not query_reply(prefix):return False
            version = self.pipe.version()
            if version != bytes.fromhex('0004040502011503'):
                raise ValueError('Type2 GET_VERSION/map not qualified; zero SRAM writes')
            if nc==0x41:self.pipe.wait(0x41,0)
            else:
                if refresh:
                    self.pipe=Type2Pipe(self.reader,self.pipe.crc_a,self.pipe.rf_error,
                                        self.uid,secrets.randbelow(0xffffffff)+1)
                self.pipe.mirror_d1=True
            self.stream = Stream(self.pipe.nonce, self.pipe.exchange, direct=True)
        return True

    def _qualify_echoes(self):
        if self.stream is None or self.stream.echoes >= 3:
            return
        if self.stream.echoes:
            raise ValueError('Incomplete GT ECHO qualification; no replay')
        for n in (16,32,40):
            self.stream.echo(bytes((i*37+n)&255 for i in range(n)))

    def _wait_for_target(self):
        print('请将一个价签贴近读卡器，按 Ctrl+C 退出。', flush=True)
        while True:
            raw = self.reader.command(bytes.fromhex('4a0100'), 0x4b, name='InListPassiveTarget')
            if raw != b'\0':
                return raw
            time.sleep(1.0)

    def set_streamed(self, enabled):
        if self.stream is not None:
            self.stream.streamed = enabled

    def enable_compact(self):
        if self.stream is None:return  # Type4 has no GT mailbox framing.
        self._qualify_echoes()
        self.stream.negotiate_compact()
        self.stream.echo(bytes((i*37+52)&255 for i in range(52)))

    def enable_grouped(self):
        if self.stream is None or not self.stream.compact:
            raise ValueError('GT group capability requires negotiated compact SRAM')
        self.grouped=True

    def exchange_group(self, commands):
        if not self.grouped:raise ValueError('GT group mode is not enabled')
        return self.stream.send_group(commands)

    def set_wait(self, seconds):
        self.wait = seconds
        if self.pipe is not None:
            self.pipe.timeout = seconds

    def exchange(self, apdu):
        if self.stream is not None:
            if apdu==b'\0\xd1\0\0\0' and self.stream.sequence:
                status=self.pipe.session()
                if status[0]&0x43==3:
                    status=self.pipe.wait(3,0)
                    if not self.use_direct_rx(status,refresh=True):
                        raise ValueError('GT query mirror not ready; no command sent')
            if apdu!=b'\0\xd1\0\0\0' or self.stream.sequence and not self.pipe.mirror_d1:
                self._qualify_echoes()
            return self.stream.command(apdu)
        return self.session.transceive(apdu, timeout_s=self.wait,
                                       recover_responses=0, retry_de_timeouts=0)

    def close(self):
        if self.reader is not None:
            self.reader.close()

