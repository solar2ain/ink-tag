"""Bounded ordinary Intel HEX validation; no private sealing algorithm.
Generic record decoder provenance: seal-goodix-code.py at399593187.
"""
def verify_hex(elf, text, expected_memory=None):
    memory = {}; upper = 0; finished = False
    for line in text.splitlines():
        if finished or not line.startswith(':'):
            raise ValueError('Invalid HEX record')
        record = bytes.fromhex(line[1:])
        if len(record) < 5 or record[0]+5 != len(record) or sum(record) & 255:
            raise ValueError('Invalid HEX checksum/length')
        size = record[0]; address = int.from_bytes(record[1:3],'big'); kind = record[3]; data = record[4:-1]
        if kind == 0:
            for i,value in enumerate(data):
                at = upper+address+i
                if at in memory:
                    raise ValueError('Overlapping HEX records')
                memory[at] = value
        elif kind == 4 and size == 2: upper = int.from_bytes(data,'big') << 16
        elif kind == 2 and size == 2: upper = int.from_bytes(data,'big') << 4
        elif kind == 1 and size == 0: finished = True
        elif kind not in (3,5): raise ValueError('Unsupported HEX record')
    expected = expected_memory
    if expected is None:
        expected = {}
        for s in elf.loads:
            expected.update({s[3]+i:value for i,value in enumerate(elf.slice(s[1],s[4]))})
    if not finished or memory != expected:
        raise ValueError('ELF/HEX loadable bytes mismatch')
