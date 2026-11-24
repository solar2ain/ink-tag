"""NDEF reads and volatile InkTag text commands; no persistent tag writer."""
from urllib.parse import parse_qs, urlsplit

MAX_NDEF = 900
SELECT_APP = bytes.fromhex('00a4040007d276000085010100')


def text_record(text):
    data = text.encode('utf8')
    # One common command fits both the 64-byte GT mirror and Type4 NDEF.
    if not data or len(data) > 54 or any(ord(c) < 32 for c in text):
        raise ValueError('NFC命令须为1..54字节UTF-8文本，不含控制字符')
    if text.startswith('IKNC'):
        from inktag_cli.nfc_command import decode_command_text
        decode_command_text(text)
    return bytes((0xd1, 1, len(data)+3, 0x54, 2))+b'en'+data


def decode_records(raw):
    """Bounded unchunked NDEF decoder; retain unknown record bytes."""
    records, offset = [], 0
    while offset < len(raw):
        header = raw[offset]; offset += 1
        if header & 0x20 or bool(header & 0x80) != (not records):
            raise ValueError('不支持分块或无效MB的NDEF记录')
        def take(n):
            nonlocal offset
            if offset+n > len(raw):
                raise ValueError('NDEF记录截断')
            value = raw[offset:offset+n]; offset += n
            return value
        type_length = take(1)[0]
        payload_length = int.from_bytes(take(1 if header & 0x10 else 4), 'big')
        id_length = take(1)[0] if header & 8 else 0
        kind, identifier, payload = take(type_length), take(id_length), take(payload_length)
        record = dict(tnf=header & 7, typeHex=kind.hex(), idHex=identifier.hex(), payloadHex=payload.hex())
        if header & 7 == 1 and kind == b'T':
            if not payload or payload[0] & 0x40 or 1+(payload[0] & 63) > len(payload):
                raise ValueError('无效NDEF Text状态/语言长度')
            n = payload[0] & 63
            record.update(language=payload[1:1+n].decode('ascii'),
                          text=payload[1+n:].decode('utf-16' if payload[0] & 128 else 'utf8'))
        elif header & 7 == 1 and kind == b'U':
            if not payload: raise ValueError('空NDEF URI记录')
            prefixes = ('', 'http://www.', 'https://www.', 'http://', 'https://')
            record['uriPrefixCode'] = payload[0]
            if payload[0] < len(prefixes):
                record['uri'] = prefixes[payload[0]]+payload[1:].decode('utf8')
        records.append(record)
        if header & 0x40:
            if offset != len(raw): raise ValueError('NDEF结束记录后仍有数据')
            return records
    if raw: raise ValueError('NDEF缺少结束记录')
    return records


def type2_read(pipe):
    pipe.sector(0)
    header = pipe.read(0)
    if header[12] != 0xe1 or header[13] >> 4 != 1 or header[14] == 0:
        raise ValueError('无效Type2 Capability Container')
    # Sector0 user pages only. Never read configuration/session registers.
    limit = min(header[14]*8, 880)
    data = bytearray()
    def fetch(end):
        if end > limit: raise ValueError('Type2 TLV超出CC用户区')
        while len(data) < end:
            data.extend(pipe.read(4+len(data)//4))
        return data[:end]
    offset = 0
    while offset < limit:
        kind = fetch(offset+1)[offset]; offset += 1
        if kind == 0: continue
        if kind == 0xfe: return b''
        size = fetch(offset+1)[offset]; offset += 1
        if size == 255:
            size = int.from_bytes(fetch(offset+2)[offset:offset+2], 'big'); offset += 2
        if size > MAX_NDEF: raise ValueError('Type2 TLV长度超出读取上限')
        value = bytes(fetch(offset+size)[offset:offset+size]); offset += size
        if kind == 3: return value
    raise ValueError('Type2用户区没有NDEF TLV')


def type2_command(link, record):
    pipe, uid = link.pipe, link.uid
    """Use a verified live GT receiver or the existing volatile NDEF mirror."""
    if len(record) > 61: raise ValueError('命令超出64字节SRAM镜像')
    direct = getattr(link,'stream',None) is not None
    session = None
    if not direct:
        current = type2_read(pipe)
        records = decode_records(current)
        urls = [r['uri'] for r in records if 'uri' in r]
        if not any(parse_qs(urlsplit(url).query).get('ble', [''])[0].startswith('IKT_') for url in urls):
            raise ValueError('当前NDEF不是InkTag设备链接；未写命令')
        session = pipe.session()
        direct = link.use_direct_rx(session)
    if direct:
        reply=link.exchange(bytes.fromhex('00d1000000'))
        # A live RX register is transport readiness, not proof of MCU identity.
        if len(reply)<2 or reply[-2:]!=b'\x90\0':
            raise ValueError('GT D1 refused: '+reply.hex())
        fields={};at=0;body=reply[:-2]
        while at<len(body):
            if at+2>len(body):raise ValueError('GT D1 TLV truncated')
            tag,n=body[at:at+2];at+=2
            if tag in fields or at+n>len(body):raise ValueError('GT D1 TLV invalid')
            fields[tag]=body[at:at+n];at+=n
        if fields.get(0xc0)!=b'IKT1' or fields.get(0xc1)!=uid:
            raise ValueError('GT D1 identity mismatch; no text command sent')
        reply=link.exchange(record)
        if reply!=b'\x90\0':raise ValueError('GT command refused: '+reply.hex())
        return
    if session[0] & 0x43 != 3 or session[2] != 1 or not session[6] & 1:
        raise ValueError('Live volatile mirror unavailable; zero writes')
    pipe.sector(0)
    if pipe.read(0)[:7] != uid:
        raise ValueError('Type2 UID changed; zero writes')
    frame = (bytes((3, len(record)))+record+b'\xfe').ljust(64, b'\0')
    # Keep NLEN/TLV empty until the full record is present. RF stays on until
    # close; MCU mirror_take requires field-off before consuming this frame.
    pipe._write(4, b'\x03\x00\xfe\x00', allow_busy=True)
    for offset in range(4, 64, 4):
        pipe._write(4+offset//4, frame[offset:offset+4], allow_busy=True)
    pipe._write(4, frame[:4], allow_busy=True)
    # Transport ACK is not a command execution acknowledgement.



class Type4Ndef:
    def __init__(self, link):
        self.link = link

    def apdu(self, command):
        reply = self.link.exchange(command)
        if len(reply) < 2 or reply[-2:] != b'\x90\0':
            raise ValueError('NDEF APDU refused: '+reply.hex())
        return reply[:-2]

    def read_bytes(self, offset, length):
        out = bytearray()
        while len(out) < length:
            # Stay within InkTag's CC MLe18 and also honor a smaller tag MLe.
            count = min(18, self.mle, length-len(out))
            command = b'\x00\xb0'+(offset+len(out)).to_bytes(2, 'big')+bytes((count,))
            value = self.apdu(command)
            if not value or len(value) > count: raise ValueError('无效NDEF READ长度')
            out.extend(value)
        return bytes(out)

    def select(self):
        self.apdu(SELECT_APP)
        self.apdu(bytes.fromhex('00a4000c02e103'))
        self.mle = 15
        prefix = self.read_bytes(0, 7)
        length = int.from_bytes(prefix[:2], 'big')
        self.mle, self.mlc = int.from_bytes(prefix[3:5], 'big'), int.from_bytes(prefix[5:7], 'big')
        if not 15 <= length <= 256 or prefix[2] >> 4 != 2 or self.mle == 0 or self.mlc == 0:
            raise ValueError('不支持的Type4 Capability Container')
        cc = prefix+self.read_bytes(7, length-7)
        offset = 7
        while offset < len(cc):
            kind = cc[offset]; offset += 1
            if kind == 0: continue
            if kind == 0xfe: break
            if offset >= len(cc): raise ValueError('CC TLV截断')
            size = cc[offset]; offset += 1
            if offset+size > len(cc): raise ValueError('CC TLV截断')
            value = cc[offset:offset+size]; offset += size
            if kind == 4 and size == 6:
                self.file_id = value[:2]
                self.capacity = int.from_bytes(value[2:4], 'big')
                self.read_access, self.write_access = value[4:]
                if self.read_access != 0 or self.capacity < 2:
                    raise ValueError('NDEF文件不可读')
                self.apdu(bytes.fromhex('00a4000c02')+self.file_id)
                return
        raise ValueError('CC未声明普通NDEF文件')

    def read(self):
        self.select()
        length = int.from_bytes(self.read_bytes(0, 2), 'big')
        if length > min(MAX_NDEF, self.capacity-2): raise ValueError('NDEF文件长度越界')
        return self.read_bytes(2, length)

    def command(self, record):
        # D1 proves InkTag ownership before writing the transient Type4 endpoint.
        # Do not use an arbitrary tag's persistent NDEF file as a command inbox.
        reply = self.apdu(bytes.fromhex('00d1000000'))
        fields, pos = {}, 0
        while pos < len(reply):
            if pos+2 > len(reply): raise ValueError('D1 TLV截断')
            tag, size = reply[pos:pos+2]; pos += 2
            if tag in fields or pos+size > len(reply): raise ValueError('D1 TLV无效')
            fields[tag] = reply[pos:pos+size]; pos += size
        if fields.get(0xc0) != b'IKT1' or fields.get(0xc1) != self.link.uid:
            raise ValueError('D1不是当前UID的InkTag身份；未写命令')
        self.select()
        if self.write_access != 0 or len(record)+2 > self.capacity:
            raise ValueError('NDEF命令不可写或超出文件容量')
        def update(offset, value):
            return self.apdu(b'\x00\xd6'+offset.to_bytes(2, 'big')+bytes((len(value),))+value)
        update(0, b'\0\0')
        chunk = min(24, self.mlc)
        for offset in range(0, len(record), chunk):
            update(offset+2, record[offset:offset+chunk])
        update(0, len(record).to_bytes(2, 'big'))


