/* InkTag LZO1X M3 encoder. Format/provenance: cli/src/inktag_cli/lzo1x.py.
 * Greedy bounded subset; the MCU shares its NFC LZO1X decoder. */
import type { PackBitsBlock } from './packbits'

export const LZO_MAX_DECODED_BLOCK = 2000

export function encodeLzo1x(data: Uint8Array<ArrayBufferLike>): Uint8Array<ArrayBufferLike> {
  const output: number[] = []
  const positions = new Map<number, number[]>()
  let cursor = 0, literalStart = 0, previousWord: number | undefined
  const key = (i: number) => (data[i] << 16) | (data[i + 1] << 8) | data[i + 2]
  const extended = (count: number) => {
    while (count > 255) { output.push(0); count -= 255 }
    output.push(count)
  }
  const literals = (start: number, end: number) => {
    const count = end - start
    if (!count) return
    if (previousWord !== undefined && count <= 3) output[previousWord] |= count
    else if (!output.length && count <= 238) output.push(17 + count)
    else if (count <= 18) output.push(count - 3)
    else { output.push(0); extended(count - 18) }
    for (let i = start; i < end; i++) output.push(data[i])
  }
  const remember = (index: number) => {
    if (index + 3 > data.length) return
    const k = key(index), values = positions.get(k) ?? []
    values.push(index)
    if (values.length > 16) values.shift()
    positions.set(k, values)
  }
  while (cursor + 3 <= data.length) {
    let length = 0, distance = 0
    const candidates = positions.get(key(cursor)) ?? []
    for (let i = candidates.length - 1; i >= 0; i--) {
      const candidate = candidates[i]
      if (cursor - candidate > 16384) continue
      let count = 3
      while (cursor + count < data.length && data[candidate + count] === data[cursor + count]) count++
      if (count > length) { length = count; distance = cursor - candidate }
    }
    if (length < 4) { remember(cursor++); continue }
    literals(literalStart, cursor)
    if (length <= 33) output.push(32 + length - 2)
    else { output.push(32); extended(length - 33) }
    const word = (distance - 1) << 2
    previousWord = output.length
    output.push(word & 255, word >> 8)
    for (let i = cursor; i < cursor + length; i++) remember(i)
    cursor += length
    literalStart = cursor
  }
  literals(literalStart, data.length)
  output.push(17, 0, 0)
  return Uint8Array.from(output)
}

export function buildLzo1xBlocks(
  data: Uint8Array<ArrayBufferLike>, decodedOffset = 0, encodedLimit = 220,
): PackBitsBlock[] {
  if (!Number.isInteger(encodedLimit) || encodedLimit < 5 || !Number.isInteger(decodedOffset) || decodedOffset < 0) {
    throw new Error('Invalid LZO block budget/offset')
  }
  const blocks: PackBitsBlock[] = []
  let offset = 0
  while (offset < data.length) {
    let count = Math.min(LZO_MAX_DECODED_BLOCK, data.length - offset)
    let encoded = encodeLzo1x(data.subarray(offset, offset + count))
    if (encoded.length > encodedLimit) {
      let low = 1, high = count - 1, best = 0
      while (low <= high) {
        const probe = Math.floor((low + high) / 2)
        const candidate = encodeLzo1x(data.subarray(offset, offset + probe))
        if (candidate.length <= encodedLimit) { best = probe; encoded = candidate; low = probe + 1 }
        else high = probe - 1
      }
      if (!best) throw new Error('LZO block budget cannot hold one byte')
      count = best
    }
    blocks.push({ decodedOffset: decodedOffset + offset, decodedLength: count, encoded })
    offset += count
  }
  return blocks
}
