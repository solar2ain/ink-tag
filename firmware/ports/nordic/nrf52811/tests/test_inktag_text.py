"""Compare the shared validator against independent strict Unicode decoding."""
from pathlib import Path
import ctypes
import random
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as temp:
    library = Path(temp) / 'text.so'
    subprocess.run(['gcc', '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-shared', '-fPIC', '-Ifirmware/common/epd',
                    '-DINKTAG_RESOURCE_NAME_MAX_BYTES=35u',
                    'compat/common/protocol/inktag_text.c',
                    'compat/common/protocol/inktag_resource_name.c',
                    '-o', str(library)], cwd=ROOT, check=True)
    valid = ctypes.CDLL(str(library)).inktag_text_utf8_valid
    valid.argtypes = [ctypes.c_char_p, ctypes.c_uint8, ctypes.c_uint8]
    valid.restype = ctypes.c_bool

    def compare(value):
        try:
            decoded = value.decode('utf-8', errors='strict')
            expected = not any(ord(c) < 32 or ord(c) == 127 for c in decoded)
        except UnicodeDecodeError:
            expected = False
            decoded = ''
        for limit in (0, 4, 255):
            assert valid(value, len(value), limit) == (expected and (not limit or len(decoded) <= limit)), (value, limit)

    assert valid(None, 0, 4) and not valid(None, 1, 4)
    for value in range(65536):
        compare(value.to_bytes(2, 'little'))
    for codepoint in (0x80, 0x7ff, 0x800, 0xd7ff, 0xe000, 0xffff, 0x10000, 0x10ffff):
        encoded = chr(codepoint).encode('utf-8')
        compare(encoded)
        for n in range(len(encoded)):
            compare(encoded[:n])
        compare(encoded * 5)
    rng = random.Random(20261007)
    for _ in range(20000):
        compare(rng.randbytes(rng.randrange(256)))
    for count in (0, 4, 5, 12, 24, 35, 255):
        compare(b'a' * count)
    api = ctypes.CDLL(str(library))
    alias = api.inktag_resource_name_valid
    alias.argtypes = [ctypes.c_char_p, ctypes.c_size_t]
    alias.restype = ctypes.c_bool

    matches = api.inktag_resource_name_matches
    matches.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_char_p, ctypes.c_size_t]
    matches.restype = ctypes.c_bool
    vocabulary = {'calendar','clock','device','slideshow','next','previous','clear',
                  '日历','时间','设备','轮播','下一页','上一页','清屏'}

    def reserved_reference(text):
        folded = ''.join(chr(ord(c) + 32) if 'A' <= c <= 'Z' else c for c in text)
        if folded in vocabulary:
            return True
        match = re.fullmatch(r'[Ss][Ll][Oo][Tt]-([1-9][0-9]*)|图槽-([1-9][0-9]*)', text)
        if match:
            number = int(match[1] or match[2])
            if number <= 256:
                return True
        return False

    def compare_alias(value):
        try:
            text = value.decode('utf-8')
            expected = (0 < len(value) <= 35 and value[:1] != b' ' and value[-1:] != b' '
                        and value[:4].lower() != b'iknc'
                        and not any(ord(c) < 32 or ord(c) == 127 for c in text)
                        and not reserved_reference(text))
        except UnicodeDecodeError:
            text = ''
            expected = False
        assert alias(value, len(value)) == expected, value
        # Duplicate matching preserves ASCII-only folding and requires a valid stored name.
        folded = bytes(c-32 if 97<=c<=122 else c for c in value)
        assert matches(folded,len(folded),value,len(value)) == expected, value
        assert not matches(value+b'x',len(value)+1,value,len(value))

    for word in vocabulary:
        if word.isascii():
            for bits in range(1 << len(word)):
                compare_alias(''.join(c.upper() if bits & (1 << n) else c
                                      for n, c in enumerate(word)).encode())
            # For every token position, every possible byte must retain the
            # original strict ASCII-case behavior (including controls/high bytes).
            encoded = word.upper().encode()
            for position in range(len(encoded)):
                for byte in range(256):
                    compare_alias(encoded[:position] + bytes([byte]) + encoded[position + 1:])
        else:
            compare_alias(word.encode())
    for number in range(258):
        for prefix in ('SLOT-', 'slot-', 'SlOt-', '图槽-'):
            compare_alias((prefix + str(number)).encode())
            compare_alias((prefix + '0' + str(number)).encode())
    for value in (b'IKNC', b'iKnc1:', b' leading', b'trailing ', b'slot--1', b'SLOT-65536'):
        compare_alias(value)
    for value in ('CLOCK', 'ſlot-1'):
        compare_alias(value.encode())
    for _ in range(20000):
        compare_alias(rng.randbytes(rng.randrange(40)))
    print('PASS strict UTF-8/reserved-name equivalence: exhaustive 2-byte/boundaries, 20000 texts, all case/slot variants and 20000 aliases')
