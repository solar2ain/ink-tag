export type PackBitsBlock = {
  decodedOffset: number
  decodedLength: number
  encoded: Uint8Array<ArrayBufferLike>
}

type PackBitsToken = { encoded: number[]; decodedLength: number }

function packBitsTokens(
  data: Uint8Array<ArrayBufferLike>, encodedTokenLimit = 129,
): PackBitsToken[] {
  if (encodedTokenLimit < 2) throw new Error('PackBits token budget is too small')
  const literalLimit = Math.min(128, encodedTokenLimit - 1)
  const tokens: PackBitsToken[] = []
  let index = 0
  while (index < data.length) {
    let run = 1
    while (index + run < data.length && run < 130 && data[index + run] === data[index]) run += 1
    if (run >= 3) {
      tokens.push({ encoded: [0x80 | (run - 3), data[index]], decodedLength: run })
      index += run
      continue
    }

    const literalStart = index
    index += run
    while (index < data.length && index - literalStart < literalLimit) {
      let nextRun = 1
      while (index + nextRun < data.length && nextRun < 130 && data[index + nextRun] === data[index]) nextRun += 1
      if (nextRun >= 3) break
      index += Math.min(nextRun, literalLimit - (index - literalStart))
    }
    const encoded = [index - literalStart - 1]
    for (let cursor = literalStart; cursor < index; cursor += 1) encoded.push(data[cursor])
    tokens.push({ encoded, decodedLength: index - literalStart })
  }
  return tokens
}

export function encodePackBits(data: Uint8Array<ArrayBufferLike>): Uint8Array<ArrayBufferLike> {
  return Uint8Array.from(packBitsTokens(data).flatMap((token) => token.encoded))
}

function partitionTokens(
  tokens: PackBitsToken[],
  decodedOffset: number,
  decodedLimit: number,
  encodedLimit: number,
  fillLiteralTails: boolean,
): PackBitsBlock[] {
  const blocks: PackBitsBlock[] = []
  let localOffset = 0
  let encoded: number[] = []
  let decodedLength = 0
  const flush = () => {
    if (encoded.length === 0) return
    blocks.push({ decodedOffset: decodedOffset + localOffset, decodedLength, encoded: Uint8Array.from(encoded) })
    localOffset += decodedLength
    encoded = []
    decodedLength = 0
  }
  for (const token of tokens) {
    if (token.encoded.length > encodedLimit || token.decodedLength > decodedLimit) {
      throw new Error('PackBits token exceeds the block budget')
    }
    if (fillLiteralTails && (token.encoded[0] & 0x80) === 0) {
      let cursor = 1
      while (cursor < token.encoded.length) {
        const count = Math.min(token.encoded.length - cursor,
          encodedLimit - encoded.length - 1, decodedLimit - decodedLength)
        if (count < 1) { flush(); continue }
        encoded.push(count - 1, ...token.encoded.slice(cursor, cursor + count))
        decodedLength += count
        cursor += count
      }
      continue
    }
    if (encoded.length > 0 && (
      encoded.length + token.encoded.length > encodedLimit ||
      decodedLength + token.decodedLength > decodedLimit
    )) {
      flush()
    }
    encoded.push(...token.encoded)
    decodedLength += token.decodedLength
  }
  flush()
  return blocks
}

export function buildPackBitsBlocks(
  data: Uint8Array<ArrayBufferLike>, decodedOffset: number, decodedLimit: number, encodedLimit: number,
): PackBitsBlock[] {
  if (decodedLimit < 3 || decodedLimit > 0xffff || encodedLimit < 2) {
    throw new Error('PackBits block limits are invalid')
  }
  const tokens = packBitsTokens(data, encodedLimit)
  const original = partitionTokens(tokens, decodedOffset, decodedLimit, encodedLimit, false)
  if (original.length < 2) return original
  const filled = partitionTokens(tokens, decodedOffset, decodedLimit, encodedLimit, true)
  const oldWire = packBitsTransferWireBytes(original), newWire = packBitsTransferWireBytes(filled)
  // Extra literal headers must pay for themselves in complete WRITE frames.
  // Prefer the original token boundaries when both costs are equal.
  return filled.length <= original.length && newWire <= oldWire &&
    (filled.length < original.length || newWire < oldWire) ? filled : original
}

export function rawTransferWireBytes(segmentLengths: number[], chunkSize: number): number {
  if (chunkSize <= 0) throw new Error('Chunk size must be positive')
  return segmentLengths.reduce(
    (total, length) => total + length + Math.ceil(length / chunkSize) * 7,
    0,
  )
}

export function packBitsTransferWireBytes(blocks: PackBitsBlock[]): number {
  return blocks.reduce((total, block) => total + 9 + block.encoded.length, 0)
}

export function packBitsIsWorthwhile(
  blocks: PackBitsBlock[], segmentLengths: number[], chunkSize: number,
): boolean {
  return packBitsTransferWireBytes(blocks) * 100 < rawTransferWireBytes(segmentLengths, chunkSize) * 95
}

export function subsequentSegmentStarts(segmentLengths: number[]): Set<number> {
  const starts = new Set<number>()
  let offset = 0
  for (const length of segmentLengths.slice(0, -1)) {
    offset += length
    starts.add(offset)
  }
  return starts
}
