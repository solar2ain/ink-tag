"""Ordinary bounded ARM ELF32/loadable-layout validation for unsigned GR base.

Reader derived from the maintained stdlib native ELF parser. No protection
implementation, private producer or guard measurement is imported."""
import hashlib
import re
import struct

PROFILES = {
    'gr5331_zkc21v': (0x00204000, 0x00220000, 0x40000, 0x20001e00, 0x20018000),
    'gr551x_zkc42v': (0x01004000, 0x01020000, 0x52000, 0x30004000, 0x30020000),
    'gr5513_zkel_internal': (0x01004000, 0x01020000, 0x44000, 0x30004000, 0x30020000),
}
ALIASES = {'gr5331_zkel':'gr5331_zkc21v','gr551x_zkel':'gr551x_zkc42v',
           'gr551x_zkel_internal':'gr5513_zkel_internal'}


def profile(family):
    return PROFILES[ALIASES.get(family,family)]


class Elf:
    def __init__(self, raw):
        self.raw = raw
        if raw[:7] != b'\x7fELF\x01\x01\x01' or len(raw) < 52:
            raise ValueError('Expected little-endian ELF32')
        h = self.unpack('HHIIIIIHHHHHH', 16)
        if h[0] != 2 or h[1] != 40 or h[8] != 32 or h[10] != 40:
            raise ValueError('Expected unstripped linked ARM ELF32')
        self.segments = [self.unpack('8I', h[4]+i*32) for i in range(h[9])]
        self.loads = [s for s in self.segments if s[0] == 1 and s[4]]
        self.sections = [self.unpack('10I', h[5]+i*40) for i in range(h[11])]
        self.symbols = []
        for section in self.sections:
            if section[1] != 2:
                continue
            if section[9] != 16 or section[5] % 16 or section[6] >= len(self.sections):
                raise ValueError('Invalid symbol table')
            strings = self.sections[section[6]]
            names = self.slice(strings[4], strings[5])
            for offset in range(section[4], section[4]+section[5], 16):
                name, value, size, info, _, index = self.unpack('IIIBBH', offset)
                end = names.find(b'\0', name)
                if name >= len(names) or end < 0:
                    raise ValueError('Invalid symbol name')
                label = names[name:end].decode('ascii')
                if label:
                    self.symbols.append(dict(name=label, address=value, bytes=size, type=info & 15, section=index))

    def slice(self, offset, size):
        if offset < 0 or size < 0 or offset+size > len(self.raw):
            raise ValueError('Truncated ELF')
        return self.raw[offset:offset+size]

    def unpack(self, fmt, offset):
        return struct.unpack('<'+fmt, self.slice(offset, struct.calcsize('<'+fmt)))

    def symbol(self, name):
        found = [s for s in self.symbols if re.sub(r'\.lto_priv\.\d+$', '', s['name']) == name]
        if len(found) != 1:
            raise ValueError('Missing or ambiguous native symbol: '+name)
        return found[0]

    def offset(self, address, size):
        found = [s[1]+address-s[2] for s in self.loads if s[2] <= address and address+size <= s[2]+s[4]]
        if len(found) != 1:
            raise ValueError('Range not uniquely file backed')
        self.slice(found[0], size)
        return found[0]

    def image(self):
        start = min(s[3] for s in self.loads)
        end = max(s[3]+s[4] for s in self.loads)
        if not 0 < end-start <= 0x100000:
            raise ValueError('Unbounded load extent')
        out = bytearray(end-start)
        for s in self.loads:
            out[s[3]-start:s[3]-start+s[4]] = self.slice(s[1], s[4])
        return start, bytes(out)


def verify(raw, role, binary, family):
    if role not in ('app', 'recovery'):
        raise ValueError('Explicit App/Recovery role required')
    elf = Elf(raw)
    rec, app, slot, ram_low, ram_high = profile(family)
    low, high = (rec, app) if role == 'recovery' else (app, app+slot-944)
    start, image = elf.image()
    if start != low or image != binary or not binary or start+len(image) > high:
        raise ValueError('Native ELF/BIN extent or bytes differ')
    intervals = []
    for load in elf.loads:
        _, offset, virtual, physical, size, memory, flags, alignment = load
        if size > memory or not low <= physical < physical+size <= high:
            raise ValueError('Loadable bytes leave the native role partition')
        elf.slice(offset, size)
        if not (virtual == physical or ram_low <= virtual < virtual+memory <= ram_high):
            raise ValueError('Invalid native RAM execution/data placement')
        if virtual == physical and flags & 2:
            raise ValueError('Flash load cannot be writable')
        if any(physical < end and begin < physical+size for begin, end in intervals):
            raise ValueError('Overlapping native load segments')
        intervals.append((physical,physical+size))
    for symbol in elf.symbols:
        name = symbol['name'].split('.')[0]
        if (name.startswith(('inktag_code_', '__inktag_guard', 'inktag_gr_find',
                'inktag_gr_p224', 'inktag_findmy_', 'inktag_gr_nfc_', 'inktag_nfc_',
                'inktag_gt_', 'gr_clock_partial_', 'process_nfc_', 'nfc_command_',
                's_nfc_', 's_gt_', 'inktag_owner_auth_', 'sign_verify', 'rsa_', 'pkc_',
                'bl_check_fw_security', 'bypass_sign_')) or name in {
                'parse_and_verify','verify_signature','apply_verified_record',
                'verified_features','k_inktag_activation_product_public_key',
                'k_activation_domain','inktag_gr_boot_verified_size'}):
            raise ValueError('base native image retains private implementation: '+name)
    if role == 'recovery':
        policy = elf.symbol('inktag_recovery_policy_info')
        if policy['bytes'] != 6 or elf.slice(elf.offset(policy['address'],6),6) != b'IKSP\x01\x02':
            raise ValueError('base requires actual CRC-only Recovery policy')
    return dict(role=role, sourceProfile='common' if role == 'recovery' else 'base', address=start, bytes=len(image),
        sha256=hashlib.sha256(image).hexdigest(), integrity='crc-only', protection=False)
