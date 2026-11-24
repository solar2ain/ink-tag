import { U8g2Font } from '../vendor/u8g2-js/index.js'

export interface EditableGlyph {
  codePoint: number
  width: number
  height: number
  x: number
  y: number
  dx: number
  pixels: Uint8Array
}

interface FontInfo {
  bits_per_0: number
  bits_per_1: number
  bits_per_char_width: number
  bits_per_char_height: number
  bits_per_char_x: number
  bits_per_char_y: number
  bits_per_delta_x: number
  start_pos_unicode: number
}

const catalogCache = new Map<'wqy9' | 'wqy12', Map<number, EditableGlyph>>()

function decodeBase64(value: string) {
  const binary = atob(value)
  return Uint8Array.from(binary, (character) => character.charCodeAt(0))
}

async function gunzipBase64(value: string) {
  const compressed = decodeBase64(value)
  if (typeof DecompressionStream === 'undefined') throw new Error('当前浏览器不支持字形目录解压')
  const stream = new Blob([compressed]).stream().pipeThrough(new DecompressionStream('gzip'))
  return new Uint8Array(await new Response(stream).arrayBuffer())
}

async function decodeCatalog(face: 'wqy9' | 'wqy12') {
  const cached = catalogCache.get(face)
  if (cached) return cached
  const { glyphCatalogData } = await import('./glyphCatalog.generated')
  const bytes = await gunzipBase64(glyphCatalogData[face].gzipBase64)
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength)
  if (String.fromCharCode(...bytes.subarray(0, 4)) !== 'ITGC' || view.getUint16(4, true) !== 1) {
    throw new Error('内置字形目录版本不兼容')
  }
  const result = new Map<number, EditableGlyph>()
  let offset = 8
  for (let index = 0; index < view.getUint16(6, true); index += 1) {
    const codePoint = view.getUint16(offset, true)
    const width = bytes[offset + 2]
    const height = bytes[offset + 3]
    const x = (bytes[offset + 4] << 24) >> 24
    const y = (bytes[offset + 5] << 24) >> 24
    const dx = bytes[offset + 6]
    const length = bytes[offset + 7]
    const packed = bytes.subarray(offset + 8, offset + 8 + length)
    const pixels = new Uint8Array(width * height)
    for (let bit = 0; bit < pixels.length; bit += 1) pixels[bit] = (packed[bit >> 3] >> (bit & 7)) & 1
    result.set(codePoint, { codePoint, width, height, x, y, dx, pixels })
    offset += 8 + length
  }
  catalogCache.set(face, result)
  return result
}

export async function catalogGlyph(face: 'wqy9' | 'wqy12', character: string) {
  return (await decodeCatalog(face)).get(character.codePointAt(0) ?? 0)
}

export async function catalogMissingGlyphs(text: string) {
  const characters = [...new Set(Array.from(text.trim()))]
  const small = await decodeCatalog('wqy9'); const large = await decodeCatalog('wqy12')
  return characters.filter((character) => !small.has(character.codePointAt(0)!) || !large.has(character.codePointAt(0)!))
}

class BitWriter {
  bytes: number[] = []
  bit = 0

  add(count: number, value: number) {
    if (value < 0 || value >= 2 ** count) throw new Error('字形度量超出字体编码范围')
    for (let index = 0; index < count; index += 1) {
      const byte = this.bit >> 3
      if (this.bytes.length <= byte) this.bytes.push(0)
      if ((value >> index) & 1) this.bytes[byte] |= 1 << (this.bit & 7)
      this.bit += 1
    }
  }

  finish() { return Uint8Array.from(this.bytes) }
}

function encodeGlyph(glyph: EditableGlyph, info: FontInfo) {
  const body = new BitWriter()
  body.add(info.bits_per_char_width, glyph.width)
  body.add(info.bits_per_char_height, glyph.height)
  body.add(info.bits_per_char_x, glyph.x + 2 ** (info.bits_per_char_x - 1))
  body.add(info.bits_per_char_y, glyph.y + 2 ** (info.bits_per_char_y - 1))
  body.add(info.bits_per_delta_x, glyph.dx + 2 ** (info.bits_per_delta_x - 1))

  const pairs: Array<[number, number]> = []
  let cursor = 0
  while (cursor < glyph.pixels.length) {
    let zero = 0; let one = 0
    while (cursor < glyph.pixels.length && glyph.pixels[cursor] === 0) { zero += 1; cursor += 1 }
    while (cursor < glyph.pixels.length && glyph.pixels[cursor] !== 0) { one += 1; cursor += 1 }
    pairs.push([zero, one])
  }
  let first = true; let lastZero = -1; let lastOne = -1
  const emit = (zero: number, one: number) => {
    if (!first && zero === lastZero && one === lastOne) body.add(1, 1)
    else {
      if (!first) body.add(1, 0)
      body.add(info.bits_per_0, zero); body.add(info.bits_per_1, one)
      first = false; lastZero = zero; lastOne = one
    }
  }
  const zeroMax = 2 ** info.bits_per_0 - 1
  const oneMax = 2 ** info.bits_per_1 - 1
  for (let [zero, one] of pairs) {
    while (zero > zeroMax) { emit(zeroMax, 0); zero -= zeroMax }
    while (one > oneMax) { emit(zero, oneMax); zero = 0; one -= oneMax }
    if (zero || one) emit(zero, one)
  }
  body.add(1, 0)
  const encoded = body.finish()
  const record = new Uint8Array(3 + encoded.length)
  record[0] = glyph.codePoint >> 8; record[1] = glyph.codePoint & 0xff
  record[2] = record.length; record.set(encoded, 3)
  if (record.length > 255) throw new Error('单个字形超过 U8g2 记录上限')
  return record
}

function existingRecords(bytes: Uint8Array) {
  const font = U8g2Font.fromArray(bytes)
  const unicodeStart = 23 + font.info.start_pos_unicode
  const firstDelta = (bytes[unicodeStart] << 8) | bytes[unicodeStart + 1]
  let cursor = unicodeStart + firstDelta
  const records = new Map<number, Uint8Array>()
  while (cursor + 2 < bytes.length) {
    const codePoint = (bytes[cursor] << 8) | bytes[cursor + 1]
    if (codePoint === 0) break
    const length = bytes[cursor + 2]
    if (length < 4 || cursor + length > bytes.length) throw new Error('U8g2 Unicode 字形表损坏')
    records.set(codePoint, bytes.slice(cursor, cursor + length)); cursor += length
  }
  let asciiCount = 0; cursor = 23
  while (cursor + 1 < unicodeStart && bytes[cursor + 1] !== 0) { asciiCount += 1; cursor += bytes[cursor + 1] }
  return { font, unicodeStart, asciiCount, records }
}

export function extendU8g2Font(base: Uint8Array, glyphs: readonly EditableGlyph[]) {
  const { font, unicodeStart, asciiCount, records } = existingRecords(base)
  for (const glyph of glyphs) {
    if (glyph.codePoint <= 255) continue
    records.set(glyph.codePoint, encodeGlyph(glyph, font.info))
  }
  const sorted = [...records.entries()].sort((left, right) => left[0] - right[0])
  const chunks: typeof sorted[] = []
  for (let index = 0; index < sorted.length; index += 101) chunks.push(sorted.slice(index, index + 101))
  if (!chunks.length) chunks.push([])
  const lookupLength = chunks.length * 4
  const dataLength = sorted.reduce((sum, [, record]) => sum + record.length, 0)
  const result = new Uint8Array(unicodeStart + lookupLength + dataLength + 2)
  result.set(base.subarray(0, unicodeStart))
  result[0] = (asciiCount + sorted.length) & 0xff
  let lookup = unicodeStart; let data = unicodeStart + lookupLength
  chunks.forEach((chunk, index) => {
    const delta = index === 0 ? lookupLength : chunks[index - 1].reduce((sum, [, record]) => sum + record.length, 0)
    result[lookup] = delta >> 8; result[lookup + 1] = delta & 0xff
    const end = index === chunks.length - 1 ? 0xffff : chunk[chunk.length - 1][0]
    result[lookup + 2] = end >> 8; result[lookup + 3] = end & 0xff; lookup += 4
    for (const [, record] of chunk) { result.set(record, data); data += record.length }
  })
  U8g2Font.fromArray(result)
  return result
}

export async function addCatalogGlyphs(fonts: readonly Uint8Array[], text: string) {
  const characters = [...new Set(Array.from(text.trim()))]
  const missing = await catalogMissingGlyphs(text)
  if (missing.length) throw new Error(`内置字形源缺少：${missing.join('、')}`)
  const next = fonts.map((font) => font.slice())
  const faces = ['wqy9', 'wqy12'] as const
  for (let index = 0; index < faces.length; index += 1) {
    const reader = U8g2Font.fromArray(next[index])
    const catalog = await decodeCatalog(faces[index])
    const glyphs = characters.filter((character) => !reader.hasGlyph(character.codePointAt(0)!))
      .map((character) => catalog.get(character.codePointAt(0)!)!)
    if (glyphs.length) next[index] = extendU8g2Font(next[index], glyphs)
  }
  return next
}

export function missingChineseFontGlyphs(fonts: readonly Uint8Array[], text: string) {
  const characters = [...new Set(Array.from(text.trim()))]
  return characters.filter((character) => {
    const codePoint = character.codePointAt(0)!
    return !U8g2Font.fromArray(fonts[0]).hasGlyph(codePoint) || !U8g2Font.fromArray(fonts[1]).hasGlyph(codePoint)
  })
}
