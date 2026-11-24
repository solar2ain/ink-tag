"""Bounded ARM ELF32 image reader; no activation/protection/link patching.

Parser provenance: the unchanged generic Elf class from goodix_code_elf.py,
399593187. Target admission is separate from this ordinary file codec.
"""
import struct
import re

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
            raise ValueError('Missing or ambiguous symbol: '+name)
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
