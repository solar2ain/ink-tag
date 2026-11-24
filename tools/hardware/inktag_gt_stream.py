"""Private GT SRAM pilot: Type-2 transport for the common FMSC application.

The PN532 adapter is supplied explicitly by the caller. This module never
imports a BLE client, changes tag EEPROM, or retries an uncertain RF write.
"""
from __future__ import annotations
import struct
import time
import zlib

MAGIC = b'IKGS'
DATA, PULL, ECHO, CANCEL, ARM = 1, 2, 3, 4, 0x10
DATA_STREAM = 5
COMPACT = 6
GROUP_DATA, GROUP_CONFIRM = 7, 8
HELLO, REPLY, ACK = 0x80, 0x81, 0x82
MIRROR_YIELD_SECONDS = .08  # Existing handoff yield, not an execution ACK.


def frame(kind, sequence, nonce, offset=0, total=0, data=b'', *, compact=False):
    if not 0 < nonce <= 0xffffffff or len(data) > (52 if compact else 40):
        raise ValueError('Invalid nonce or payload')
    if compact:
        p=struct.pack('<BBHHH',kind,len(data),sequence,offset,total)+data.ljust(52,b'\0')
        return p+struct.pack('<I',zlib.crc32(struct.pack('<I',nonce)+p))
    p = struct.pack('<4sBBHIHHB3x', MAGIC, 1, kind, sequence, nonce,
                    offset, total, len(data)) + data.ljust(40, b'\0')
    return p + struct.pack('<I', zlib.crc32(p))


def parse(raw, nonce, *, compact=False):
    if compact:
        if len(raw)!=64 or zlib.crc32(struct.pack('<I',nonce)+raw[:60])!=int.from_bytes(raw[60:],'little'):
            raise ValueError('Invalid compact SRAM integrity/session')
        kind,n,seq,offset,total=struct.unpack('<BBHHH',raw[:8])
        if n>52 or any(raw[8+n:60]):raise ValueError('Invalid compact SRAM length/padding')
        return kind,seq,offset,total,raw[8:8+n]
    if len(raw) != 64 or raw[:5] != MAGIC+b'\1' or zlib.crc32(raw[:60]) != int.from_bytes(raw[60:], 'little'):
        raise ValueError('Invalid SRAM packet integrity')
    _, _, kind, seq, actual, offset, total, n = struct.unpack('<4sBBHIHHB3x', raw[:20])
    if actual != nonce or n > 40 or any(raw[17:20]) or any(raw[20+n:60]):
        raise ValueError('Stale or malformed SRAM packet')
    return kind, seq, offset, total, raw[20:20+n]


def query_reply(raw):
    """A previous descriptor is a transport candidate, not InkTag identity."""
    if len(raw)!=64:return False
    nonce=int.from_bytes(raw[8:12],'little')
    try:kind,_,offset,total,data=parse(raw,nonce)
    except ValueError:return False
    return bool(nonce and kind==REPLY and total in (75,79) and offset%40==0
                and offset<total and len(data)==min(40,total-offset))


class Stream:
    """Intermediate streaming writes are published; the next write waits for
    MCU consumption. Complete commands still require their real response."""
    def __init__(self, nonce, exchange, *, direct=False):
        self.nonce, self.exchange, self.sequence = nonce, exchange, 0
        self.echoes = 0
        self.streamed = False
        self.compact = False
        self.direct = direct

    @property
    def payload_bytes(self):return 52 if self.compact else 40

    def negotiate_compact(self):
        if self.compact or self.echoes<3:raise ValueError('Invalid compact negotiation state')
        response=self._packet(COMPACT,total=1,data=b'\x34')
        if (response[0],response[2],response[3],response[4])!=(REPLY,0,1,b'\x34'):
            raise ValueError('Compact negotiation refused')
        self.compact=True

    def send_group(self, commands, published=None):
        if not self.compact or self.echoes<3 or not 1<=len(commands)<=16:
            raise ValueError('Invalid image group state/size')
        # Validate the complete group before the first mutation. Controls such
        # as D4/slot selection never enter a fire-and-confirm DATA group.
        for command in commands:
            if (not 5<=len(command)<=260 or command[:2] not in (b'\xf0\xd2',b'\xf0\xd3')
                    or command[4]+5!=len(command)):
                raise ValueError('Only well-formed image DATA can be grouped')
        digest=2166136261
        for index,command in enumerate(commands,1):
            for offset in range(0,len(command),52):
                if self.sequence>=65535:raise ValueError('Session sequence exhausted')
                raw=frame(GROUP_DATA,self.sequence,self.nonce,offset,len(command),command[offset:offset+52],compact=True)
                if self.exchange(raw) is not None:raise ValueError('Unexpected group response')
                self.sequence+=1
            for byte in command:digest=((digest^byte)*16777619)&0xffffffff
            if published:published(index)  # Published only, not confirmed.
        response=self._packet(GROUP_CONFIRM)
        expected=len(commands).to_bytes(2,'little')+digest.to_bytes(4,'little')
        if (response[0],response[2],response[3],response[4])!=(REPLY,0,6,expected):
            raise ValueError('Image group confirmation mismatch')
        return {'count':len(commands),'digest':f'{digest:08x}'}

    def _packet(self, kind, offset=0, total=0, data=b''):
        if self.sequence >= 65535:
            raise ValueError('Session sequence exhausted')
        seq = self.sequence
        raw = self.exchange(frame(kind, seq, self.nonce, offset, total, data, compact=self.compact))
        if raw is None:
            if kind != DATA_STREAM or offset+len(data) >= total:
                raise ValueError('Missing response to a complete request')
            self.sequence += 1
            return None
        response = parse(raw, self.nonce,compact=self.compact)
        if response[1] != seq:
            raise ValueError('Unexpected response sequence')
        self.sequence += 1
        return response

    def echo(self, data):
        if not data or len(data) > self.payload_bytes:
            raise ValueError('Echo exceeds the negotiated payload')
        kind, _, offset, total, actual = self._packet(ECHO, total=len(data), data=data)
        if (kind, offset, total, actual) != (REPLY, 0, len(data), data):
            raise ValueError('SRAM echo mismatch')
        self.echoes += 1

    def command(self, data):
        if not 1 <= len(data) <= 260:
            raise ValueError('FMSC request must contain 1..260 bytes')
        first_d1 = (self.direct and not self.compact
                    and not self.streamed and data == b'\x00\xd1\x00\x00\x00')
        if self.echoes < 3 and not first_d1:
            raise ValueError('Three verified echo round trips are required')
        reply = None
        for offset in range(0, len(data), self.payload_bytes):
            chunk = data[offset:offset+self.payload_bytes]
            reply = self._packet(DATA_STREAM if self.streamed else DATA, offset, len(data), chunk)
            if offset+len(chunk) < len(data):
                if self.streamed and reply is None:
                    continue  # Next write checks consumption; final reply validates completion.
                if reply is None or (reply[0], reply[2], reply[3], reply[4]) != (ACK, offset+len(chunk), len(data), b''):
                    raise ValueError('Unexpected fragment ACK')
        kind, _, offset, total, chunk = reply
        if kind != REPLY or offset or not 1 <= total <= 80 or not chunk:
            raise ValueError('Unexpected FMSC response')
        result = chunk
        while len(result) < total:
            kind, _, offset, actual_total, chunk = self._packet(PULL, len(result), total)
            if kind != REPLY or offset != len(result) or actual_total != total or not chunk:
                raise ValueError('Unexpected response fragment')
            result += chunk
        if len(result) != total:
            raise ValueError('Response exceeds advertised length')
        return result



class Type2Pipe:
    """Qualified candidate map: session sector3, SRAM sector1 F0..FF.

    Before each SRAM operation check the live direction and READY bits.
    Reading FF and writing FF transfer ownership; diagnostics never do so.
    """
    def __init__(self, reader, crc_a, rf_error, uid, nonce, *, timeout=3.0):
        if len(uid) != 7:
            raise ValueError('Exact seven-byte RF identity required')
        self.reader, self.crc_a, self.rf_error = reader, crc_a, rf_error
        self.uid, self.nonce, self.timeout = uid, nonce, timeout
        self.current_sector = None
        self._framing_state = None
        self._sram_image = None
        self._rx_turn_pending = False
        self._rx_turn_ready_at = None
        self.writes = 0
        self.compact = False
        self.mirror_d1 = False

    def _framing(self, bits, crc):
        state = (bits, crc)
        if self._framing_state != state:
            self.reader.set_framing(bits, crc)
            self._framing_state = state

    def select(self):
        self._framing_state = None
        self._sram_image = None
        self._rx_turn_pending = False
        self._rx_turn_ready_at = None
        self.compact = False
        p = self.reader.command(bytes.fromhex('4a0100'), 0x4b, name='InListPassiveTarget')
        self.adopt_selection(p)

    def adopt_selection(self, p):
        """Validate an already selected target, without repeating discovery.

        The universal adapter has just selected this UID to determine SAK/ATS.
        Calling InListPassiveTarget again can return zero for that active tag.
        Use only on a fresh pipe or after select() reset its transaction state.
        """
        if len(p) != 13 or p[:2] != b'\1\1' or p[4] or p[5] != 7 or p[6:] != self.uid:
            raise ValueError('Exact Type-2 target identity required')
        self.current_sector = 0

    def sector(self, number):
        if self.current_sector == number:
            return
        self._framing(8, False)
        p = b'\xc2\xff'
        if self.reader.communicate_thru(p+self.crc_a(p), 100) != b'\x0a':
            raise ValueError('Sector-select command refused')
        p = bytes((number, 0, 0, 0))
        try:
            # The measured passive ACK is absence of a response beyond1ms.
            # A1.5ms request selects the PN532's1.6ms finite hardware timer;
            # exact-AA06 sector0/3 known-map reads qualified this separately.
            self.reader.communicate_thru(p+self.crc_a(p), 1.5)
        except self.rf_error as error:
            if error.code != 1:
                raise
        else:
            raise ValueError('Sector-select passive ACK was not observed')
        self.current_sector = number
        # The following READ/WRITE selects its own CRC mode. Switching here
        # and immediately back for WRITE adds two PN532 register commands
        # without an intervening RF operation.

    def read(self, page):
        # Keep one reader framing across READ, WRITE and SECTOR_SELECT. Receive
        # the RF CRC explicitly, so avoiding CIU reconfiguration/readback does
        # not remove integrity verification. Qualified on exact AA06 first.
        self._framing(8, False)
        command = bytes((0x30, page))
        for attempt in range(4):
            p = self.reader.communicate_thru(command+self.crc_a(command), 100)
            if self.current_sector==0 and p==b'\x03' and attempt<3:
                time.sleep(.01)
                continue
            break
        if len(p) != 18:
            raise ValueError('Incomplete Type-2 READ with CRC')
        if self.crc_a(p[:16]) != p[16:]:
            raise ValueError('Invalid Type-2 READ CRC')
        return p[:16]

    def _write(self, page, data, *, allow_busy=False):
        if len(data) != 4:
            raise ValueError('Type-2 WRITE requires four bytes')
        self._framing(8, False)
        p = bytes((0xa2, page))+data
        # Fitted GT NAK03 explicitly rejects memory access during an I2C
        # mirror inspection. Retry only that refusal, never a timeout/unknown
        # outcome or an ACK. Pass-through writes retain their one-shot rule.
        for attempt in range(4 if allow_busy else 1):
            response=self.reader.communicate_thru(p+self.crc_a(p),100)
            if response==b'\x0a':
                self.writes+=1;return
            if response!=b'\x03' or not allow_busy or attempt==3:
                raise ValueError('Type-2 WRITE not acknowledged: '+response.hex())
            time.sleep(.01)
            status=self.session()
            if status[0]&0x43!=3 or status[2]!=1 or not status[6]&1:
                raise ValueError('Mirror map changed after NAK03; no write retry')
            self.sector(0)

    def session(self):
        self.sector(3)
        return self.read(0xf8)

    def version(self):
        self.sector(0)
        self._framing(8, False)
        command = b'\x60'
        reply = self.reader.communicate_thru(command+self.crc_a(command), 100)
        if len(reply) != 10 or self.crc_a(reply[:8]) != reply[8:]:
            raise ValueError('Invalid Type-2 GET_VERSION length/CRC; zero writes')
        return reply[:8]

    def wait(self, nc, ready):
        until = time.monotonic()+self.timeout
        while True:
            s = self.session()
            if not s[6] & 1:
                raise ValueError('RF field is not live')
            if (s[0] & 0x43 == nc and s[6] & 0x18 == ready and
                    (nc!=3 or s[2]==1 and not s[6]&0x40)):
                return s
            if time.monotonic() >= until:
                raise TimeoutError(f'GT ownership timeout: NC={s[0]:02x}, NS={s[6]:02x}')
            time.sleep(.01)



    def _receive_ready(self):
        if self._rx_turn_pending:
            # The fitted GT can reassert stale RX READY after a direction turn.
            # Keep the existing 120ms guard; actual port service cadence is
            # separate. Time already spent preparing the next command counts
            # from the RX-ready state observed at the end of the last reply.
            elapsed = (0 if self._rx_turn_ready_at is None else
                       max(0, time.monotonic()-self._rx_turn_ready_at))
            if elapsed < .12:
                time.sleep(.12-elapsed)
        # Always verify live ownership before the first SRAM write. No RF
        # mutation or uncertain-command replay occurs while settling.
        self.wait(0x41, 0)
        if self._rx_turn_pending:
            self._rx_turn_pending = False
            self._rx_turn_ready_at = None

    def exchange(self, raw):
        kind, seq, offset, total, data = parse(raw, self.nonce,compact=self.compact)
        if self.mirror_d1:
            if self.compact or not (kind in (PULL,ECHO) or
                    (kind,offset,total,data)==(DATA,0,5,b'\0\xd1\0\0\0')):
                raise ValueError('Mirror accepts only D1/PULL or ECHO handoff')
            s=self.session()
            if s[0]&0x43!=3 or s[2]!=1 or not s[6]&1:
                raise ValueError('Live volatile mirror unavailable; zero writes')
            self.sector(0)
            if self.read(0)[:7]!=self.uid:
                raise ValueError('Type2 identity changed; zero writes')
            # Repeated queries replace IKGS replies: invalidate the magic first.
            self._write(4,b'\0'*4,allow_busy=True)
            for start in range(4,64,4):self._write(4+start//4,raw[start:start+4],allow_busy=True)
            self._write(4,raw[:4],allow_busy=True)
        else:
            self._receive_ready()
            self.sector(1)
            for start in range(0, 64, 4):
                if start == 60 or self._sram_image is None or raw[start:start+4] != self._sram_image[start:start+4]:
                    self._write(0xf0+start//4, raw[start:start+4])
        self._sram_image = raw
        if kind == GROUP_DATA or (kind == DATA_STREAM and offset+len(data) < total):
            # Defer consumption check to the next _receive_ready(), including
            # CANCEL. No second writer runs until that live check succeeds.
            # The final fragment still waits for the real application reply.
            return None
        if self.mirror_d1:
            until=time.monotonic()+self.timeout
            time.sleep(min(MIRROR_YIELD_SECONDS,self.timeout))
            while True:
                if time.monotonic()>=until:raise TimeoutError(f'GT mirror response timeout seq={seq}')
                s=self.session();nc=s[0]&0x43
                if not s[6]&1:raise ValueError('RF field is not live')
                if nc==0x40:
                    self.mirror_d1=False  # Old firmware or deliberate ECHO handoff.
                    break
                if nc!=3 or s[2]!=1:raise ValueError('Mirror response map changed')
                if kind!=ECHO and not s[6]&0x40:
                    self.sector(0)
                    result=b''.join(self.read(page) for page in (4,8,12,16))
                    try:reply=parse(result,self.nonce)
                    except ValueError:reply=None  # Request/residue or incomplete publication.
                    if (reply is not None and reply[0]==REPLY and reply[1]==seq and
                            reply[2]==offset and 1<=reply[3]<=80 and reply[4]):
                        self._sram_image=result
                        return result  # Metadata only: no pass-through consumption ACK.
                if time.monotonic()>=until:raise TimeoutError(f'GT mirror response timeout seq={seq}')
                time.sleep(min(MIRROR_YIELD_SECONDS,max(0,until-time.monotonic())))
        self.wait(0x40, 8)
        self.sector(1)
        result = b''.join(self.read(page) for page in (0xf0, 0xf4, 0xf8, 0xfc))
        parse(result, self.nonce,compact=self.compact)
        self._sram_image = result
        # Confirm last-page consumption before the caller may issue D4/work.
        self.wait(0x41, 0)
        self._rx_turn_pending = True
        self._rx_turn_ready_at = time.monotonic()
        if kind==COMPACT:self.compact=True  # Legacy reply consumed before mode change.
        return result
