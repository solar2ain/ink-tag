"""Small LZO1X M3 encoder, shared wire subset with the NFC LZO1X decoder.

InkTag implementation of the MIT LZokay decoder's instruction format;
no liblzo/minilzo code or dependency.
This greedy subset deliberately favors bounded work over optimal compression.
"""
from .packbits import PackBitsBlock

MAX_DECODED_BLOCK = 2000


def encode_lzo1x(data: bytes) -> bytes:
    output = bytearray()
    positions: dict[bytes, list[int]] = {}
    cursor = literal_start = 0
    previous_word: int | None = None

    def extended(count: int) -> None:
        while count > 255:
            output.append(0)
            count -= 255
        output.append(count)

    def literals(start: int, end: int) -> None:
        count = end - start
        if not count:
            return
        if previous_word is not None and count <= 3:
            output[previous_word] |= count
        elif not output and count <= 238:
            output.append(17 + count)
        elif count <= 18:
            output.append(count - 3)
        else:
            output.append(0)
            extended(count - 18)
        output.extend(data[start:end])

    def remember(index: int) -> None:
        if index + 3 > len(data):
            return
        values = positions.setdefault(data[index:index + 3], [])
        values.append(index)
        if len(values) > 16:
            del values[0]

    while cursor + 3 <= len(data):
        length = distance = 0
        for candidate in reversed(positions.get(data[cursor:cursor + 3], [])):
            if cursor - candidate > 16384:
                continue
            count = 3
            while cursor + count < len(data) and data[candidate + count] == data[cursor + count]:
                count += 1
            if count > length:
                length, distance = count, cursor - candidate
        if length < 4:
            remember(cursor)
            cursor += 1
            continue
        literals(literal_start, cursor)
        if length <= 33:
            output.append(32 + length - 2)
        else:
            output.append(32)
            extended(length - 33)
        word = (distance - 1) << 2
        previous_word = len(output)
        output.extend((word & 255, word >> 8))
        for index in range(cursor, cursor + length):
            remember(index)
        cursor += length
        literal_start = cursor
    literals(literal_start, len(data))
    output.extend((17, 0, 0))
    return bytes(output)


def build_lzo1x_blocks(data: bytes, *, decoded_offset: int = 0,
                       encoded_limit: int = 220) -> list[PackBitsBlock]:
    if encoded_limit < 5 or decoded_offset < 0:
        raise ValueError('Invalid LZO block budget/offset')
    blocks = []
    offset = 0
    while offset < len(data):
        count = min(MAX_DECODED_BLOCK, len(data) - offset)
        encoded = encode_lzo1x(data[offset:offset + count])
        if len(encoded) > encoded_limit:
            low, high, best = 1, count - 1, 0
            while low <= high:
                probe = (low + high) // 2
                candidate = encode_lzo1x(data[offset:offset + probe])
                if len(candidate) <= encoded_limit:
                    best, encoded = probe, candidate
                    low = probe + 1
                else:
                    high = probe - 1
            if not best:
                raise ValueError('LZO block budget cannot hold one byte')
            count = best
        blocks.append(PackBitsBlock(decoded_offset + offset, count, encoded))
        offset += count
    return blocks
