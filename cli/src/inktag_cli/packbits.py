from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class PackBitsBlock:
    decoded_offset: int
    decoded_length: int
    encoded: bytes


def _packbits_tokens(
    data: bytes, *, encoded_token_limit: int = 129
) -> list[tuple[bytes, int]]:
    """Encode one stream and retain its independently decodable token boundaries."""
    if encoded_token_limit < 2:
        raise ValueError("PackBits token budget must hold control and data")
    literal_limit = min(128, encoded_token_limit - 1)
    source = memoryview(data)
    tokens: list[tuple[bytes, int]] = []
    index = 0
    while index < len(source):
        run = 1
        while (index + run < len(source) and run < 130 and
               source[index + run] == source[index]):
            run += 1
        if run >= 3:
            tokens.append((bytes((0x80 | (run - 3), source[index])), run))
            index += run
            continue

        literal_start = index
        index += run
        while index < len(source) and index - literal_start < literal_limit:
            next_run = 1
            while (index + next_run < len(source) and next_run < 130 and
                   source[index + next_run] == source[index]):
                next_run += 1
            if next_run >= 3:
                break
            index += min(next_run, literal_limit - (index - literal_start))
        literal = source[literal_start:index]
        tokens.append((bytes((len(literal) - 1,)) + bytes(literal), len(literal)))
    return tokens


def encode_packbits(data: bytes) -> bytes:
    """Encode one PackBits stream."""
    return b"".join(encoded for encoded, _decoded_length in _packbits_tokens(data))


def build_packbits_blocks(
    data: bytes,
    *,
    decoded_offset: int,
    decoded_limit: int,
    encoded_limit: int,
) -> list[PackBitsBlock]:
    """Partition data into independently decodable, payload-bounded blocks."""
    if decoded_limit < 3 or decoded_limit > 0xFFFF or encoded_limit < 2:
        raise ValueError("invalid PackBits block limits")
    blocks: list[PackBitsBlock] = []
    local_offset = 0
    encoded = bytearray()
    decoded_length = 0
    for token, token_decoded_length in _packbits_tokens(
        data, encoded_token_limit=encoded_limit
    ):
        if encoded and (len(encoded) + len(token) > encoded_limit or
                        decoded_length + token_decoded_length > decoded_limit):
            blocks.append(PackBitsBlock(
                decoded_offset=decoded_offset + local_offset,
                decoded_length=decoded_length,
                encoded=bytes(encoded),
            ))
            local_offset += decoded_length
            encoded.clear()
            decoded_length = 0
        if len(token) > encoded_limit or token_decoded_length > decoded_limit:
            raise ValueError("PackBits token exceeds the block budget")
        encoded.extend(token)
        decoded_length += token_decoded_length
    if encoded:
        blocks.append(PackBitsBlock(
            decoded_offset=decoded_offset + local_offset,
            decoded_length=decoded_length,
            encoded=bytes(encoded),
        ))
    return blocks


def raw_transfer_wire_bytes(segment_lengths: list[int], chunk_size: int) -> int:
    """Count V2 header, offset and raw bytes for all TRANSFER_WRITE frames."""
    if chunk_size <= 0:
        raise ValueError("chunk size must be positive")
    return sum(
        length + ((length + chunk_size - 1) // chunk_size) * 7
        for length in segment_lengths
    )


def packbits_transfer_wire_bytes(blocks: list[PackBitsBlock]) -> int:
    """Count V2 header, offset, decoded length and encoded block bytes."""
    return sum(9 + len(block.encoded) for block in blocks)


def packbits_is_worthwhile(
    blocks: list[PackBitsBlock], segment_lengths: list[int], chunk_size: int
) -> bool:
    """Require compressed framed wire size to be strictly below 95% of raw."""
    return (
        packbits_transfer_wire_bytes(blocks) * 100
        < raw_transfer_wire_bytes(segment_lengths, chunk_size) * 95
    )


def subsequent_segment_starts(segment_lengths: list[int]) -> set[int]:
    """Return decoded starts that may trigger a direct-frame plane switch."""
    starts: set[int] = set()
    offset = 0
    for length in segment_lengths[:-1]:
        offset += length
        starts.add(offset)
    return starts
