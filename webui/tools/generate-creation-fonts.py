#!/usr/bin/env python3
"""Pack the original pixel strikes for WebUI. See src/assets/fonts/README.md."""

import argparse
import gzip
from pathlib import Path
import struct

def outline_strike(path, size):
    from fontTools.ttLib import TTFont
    from PIL import ImageFont

    with TTFont(path) as original:
        codepoints = sorted(original.getBestCmap())
    font = ImageFont.truetype(str(path), size, layout_engine=ImageFont.Layout.BASIC)
    for codepoint in codepoints:
        char = chr(codepoint)
        mask, (left, top) = font.getmask2(char, mode="1", anchor="ls")
        advance = round(font.getlength(char, mode="1"))
        box = mask.getbbox()
        if not box:
            yield codepoint, advance, 0, 0, 0, 0, b""
            continue
        x0, y0, x1, y1 = box
        width = x1 - x0
        stride = (width + 7) // 8
        packed = bytearray(stride * (y1 - y0))
        for y in range(y0, y1):
            for x in range(x0, x1):
                if mask[y * mask.size[0] + x]:
                    column = x - x0
                    packed[(y - y0) * stride + column // 8] |= 0x80 >> (column % 8)
        yield codepoint, advance, left + x0, top + y0, width, y1 - y0, packed


def bdf_strike(path):
    glyphs = []
    with path.open(encoding="latin-1") as lines:
        for line in lines:
            if line.startswith("STARTCHAR "):
                codepoint = -1
            elif line.startswith("ENCODING "):
                codepoint = int(line.split()[1])
            elif line.startswith("DWIDTH "):
                advance = int(line.split()[1])
            elif line.startswith("BBX "):
                width, height, left, bottom = map(int, line.split()[1:])
            elif line.strip() == "BITMAP":
                stride = (width + 7) // 8
                # Some upstream rows retain the full cell's zero padding.
                bitmap = b"".join(bytes.fromhex(next(lines))[:stride].ljust(stride, b"\0") for _ in range(height))
                if codepoint >= 0:
                    glyphs.append((codepoint, advance, left, -bottom - height, width, height, bitmap))
    return sorted(glyphs)


def pack_strikes(destination, strikes):
    # Size-count header followed by each strike's codepoint, metrics and row-packed bits.
    data = bytearray([len(strikes)])
    for size, source in strikes.items():
        glyphs = list(source)
        data.extend(struct.pack("<BI", size, len(glyphs)))
        for codepoint, advance, x, y, width, height, bitmap in glyphs:
            data.extend(struct.pack("<IBbbBB", codepoint, advance, x, y, width, height))
            data.extend(bitmap)
        print(f"{destination.name}, {size} px: {len(glyphs)} glyphs")
    destination.write_bytes(gzip.compress(data, mtime=0))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sources", type=Path)
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[1] / "src/assets/fonts")
    parser.add_argument("--song-only", action="store_true")
    options = parser.parse_args()
    base, out = options.sources, options.output
    out.mkdir(parents=True, exist_ok=True)
    if not options.song_only:
        for name, original, size in (
            ("compact", "MZPXorig.ttf", 12),
            ("body", "ZLabsPixel_12px_M_CN.ttf", 12),
            ("mini", "ChillBitmap7x.ttf", 8),
        ):
            pack_strikes(out / f"creation-{name}.bin.gz", {size: outline_strike(base / original, size)})
        pack_strikes(out / "creation-fusion.bin.gz", {
            size: bdf_strike(base / f"fusion-{size}px-zh-hans.bdf") for size in (8, 10, 12)
        })
    files = ("9pt", "10pt", "13px", "11pt", "12pt")
    pack_strikes(out / "creation-song.bin.gz", {
        size: bdf_strike(base / f"wenquanyi_{name}.bdf") for size, name in zip(range(12, 17), files)
    })


if __name__ == "__main__":
    main()
