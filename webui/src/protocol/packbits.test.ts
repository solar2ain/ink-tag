import { describe, expect, it } from 'vitest'
import oracle from '../../../tests/fixtures/packbits-tail-fill-v1.json'
import {
  buildPackBitsBlocks, encodePackBits, packBitsIsWorthwhile, subsequentSegmentStarts,
  packBitsTransferWireBytes, rawTransferWireBytes,
} from './packbits'

function decodePackBits(encoded: Uint8Array): Uint8Array {
  const output: number[] = []
  for (let index = 0; index < encoded.length;) {
    const control = encoded[index++]
    if ((control & 0x80) !== 0) {
      if (index >= encoded.length) throw new Error('truncated repeat')
      const count = (control & 0x7f) + 3
      const value = encoded[index++]
      for (let cursor = 0; cursor < count; cursor += 1) output.push(value)
    } else {
      const count = control + 1
      if (index + count > encoded.length) throw new Error('truncated literal')
      output.push(...encoded.subarray(index, index + count))
      index += count
    }
  }
  return Uint8Array.from(output)
}

describe('InkTag PackBits transfer blocks', () => {
  it('round-trips literals and long runs', () => {
    const source = Uint8Array.from([
      ...Array.from({ length: 260 }, () => 0xaa),
      ...Array.from({ length: 150 }, (_, index) => index & 0xff),
      7, 7, 8, 8, 8, 9,
    ])
    expect(decodePackBits(encodePackBits(source))).toEqual(source)
  })

  it('partitions incompressible input to the negotiated encoded budget', () => {
    const source = Uint8Array.from({ length: 474 }, (_, index) => index & 0xff)
    const blocks = buildPackBitsBlocks(source, 17, 237, 235)
    expect(blocks.every((block) => block.decodedLength <= 237 && block.encoded.length <= 235)).toBe(true)
    expect(blocks[0].decodedOffset).toBe(17)
    expect(Uint8Array.from(blocks.flatMap((block) => [...decodePackBits(block.encoded)]))).toEqual(source)
  })

  it('compresses the segment before packetizing complete tokens', () => {
    const source = new Uint8Array(30000).fill(0xa5)
    const blocks = buildPackBitsBlocks(source, 91, 0xffff, 235)
    expect(blocks).toHaveLength(2)
    expect(blocks.every((block) => block.decodedLength > 237 && block.encoded.length <= 235)).toBe(true)
    expect(blocks.map((block) => block.decodedOffset)).toEqual([91, 15301])
    expect(Uint8Array.from(blocks.flatMap((block) => [...decodePackBits(block.encoded)]))).toEqual(source)
  })

  it('uses exact framed wire bytes and a strict 95 percent threshold', () => {
    const source = new Uint8Array(400).fill(0xff)
    const blocks = buildPackBitsBlocks(source, 0, 237, 235)
    expect(packBitsTransferWireBytes(blocks)).toBeLessThan(rawTransferWireBytes([400], 237))
    expect(packBitsIsWorthwhile(blocks, [400], 237)).toBe(true)

    const noise = Uint8Array.from({ length: 400 }, (_, index) => index & 0xff)
    const noisyBlocks = buildPackBitsBlocks(noise, 0, 237, 235)
    expect(packBitsIsWorthwhile(noisyBlocks, [400], 237)).toBe(false)
  })

  it('marks only subsequent decoded segment starts', () => {
    expect(subsequentSegmentStarts([2888, 2888, 100])).toEqual(new Set([2888, 5776]))
    expect(subsequentSegmentStarts([5776])).toEqual(new Set())
  })

  it('fills literal tails only when packet count and framed cost do not regress', () => {
    const source = Uint8Array.from({ length: 512 }, (_, index) => index & 0xff)
    const blocks = buildPackBitsBlocks(source, 17, 237, 235)
    expect(blocks).toHaveLength(3)
    expect(packBitsTransferWireBytes(blocks)).toBeLessThan(516 + 4 * 9)
    expect(Uint8Array.from(blocks.flatMap((block) => [...decodePackBits(block.encoded)]))).toEqual(source)

    const shorter = Uint8Array.from({ length: 240 }, (_, index) => index)
    expect(buildPackBitsBlocks(shorter, 0, 65535, 235).map(b => b.encoded.length)).toEqual([129, 113])
  })

  it('matches the same fixed byte oracle as the maintained CLI', () => {
    for (const vector of oracle.vectors) {
      const source = Uint8Array.from(vector.parts.flatMap(part => 'hex' in part
        ? Array.from(part.hex!.match(/../g) ?? [], byte => parseInt(byte, 16))
        : Array.from({ length: part.count! }, (_, i) =>
          'value' in part ? part.value! : (i + ('start' in part ? part.start! : 0)) & 255)))
      const blocks = buildPackBitsBlocks(source, vector.decodedOffset, vector.decodedLimit, vector.encodedLimit)
      expect(blocks.map(b => ({ decodedOffset: b.decodedOffset, decodedLength: b.decodedLength,
        encodedHex: Array.from(b.encoded, n => n.toString(16).padStart(2, '0')).join('') })), vector.name).toEqual(vector.expected)
      expect(blocks.length).toBeLessThanOrEqual(vector.oldBlockCount)
      expect(packBitsTransferWireBytes(blocks)).toBeLessThanOrEqual(vector.oldWireBytes)
      expect(Uint8Array.from(blocks.flatMap(b => [...decodePackBits(b.encoded)]))).toEqual(source)
    }
  })

  it('keeps decoded segment ends and invalid-budget refusals', () => {
    let offset = 73
    for (const source of [Uint8Array.from({ length: 240 }, (_, i) => i), new Uint8Array(131).fill(0xaa)]) {
      const blocks = buildPackBitsBlocks(source, offset, 65535, 235)
      expect(blocks[0].decodedOffset).toBe(offset)
      expect(blocks.at(-1)!.decodedOffset + blocks.at(-1)!.decodedLength).toBe(offset + source.length)
      offset += source.length
    }
    expect(() => buildPackBitsBlocks(new Uint8Array(1), 0, 2, 235)).toThrow()
    expect(() => buildPackBitsBlocks(new Uint8Array(1), 0, 65536, 235)).toThrow()
    expect(() => buildPackBitsBlocks(new Uint8Array(1), 0, 237, 1)).toThrow()
    expect(() => buildPackBitsBlocks(new Uint8Array(130).fill(1), 0, 3, 235)).toThrow()
  })
})
