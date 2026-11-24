import { readFile, writeFile } from 'node:fs/promises'
import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { gzipSync } from 'node:zlib'

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..', '..')
const output = resolve(root, 'webui/src/device-gui/glyphCatalog.generated.ts')

function littleEndian(format) { return (format & 4) === 0 }

function parsePcf(buffer) {
  const bytes = new Uint8Array(buffer)
  const view = new DataView(buffer.buffer, buffer.byteOffset, buffer.byteLength)
  if (view.getUint32(0, true) !== 0x70636601) throw new Error('invalid PCF signature')
  const tables = new Map()
  const count = view.getUint32(4, true)
  for (let index = 0; index < count; index += 1) {
    const offset = 8 + index * 16
    tables.set(view.getUint32(offset, true), {
      format: view.getUint32(offset + 4, true),
      size: view.getUint32(offset + 8, true),
      offset: view.getUint32(offset + 12, true),
    })
  }
  const metricsTable = tables.get(4)
  const bitmapsTable = tables.get(8)
  const encodingTable = tables.get(32)
  if (!metricsTable || !bitmapsTable || !encodingTable) throw new Error('PCF is missing required tables')

  let cursor = metricsTable.offset
  const metricsFormat = view.getUint32(cursor, true); cursor += 4
  const metricsEndian = littleEndian(metricsFormat)
  const compressed = Boolean(metricsFormat & 0x100)
  const metricCount = compressed ? view.getUint16(cursor, metricsEndian) : view.getUint32(cursor, metricsEndian)
  cursor += compressed ? 2 : 4
  const metrics = []
  for (let index = 0; index < metricCount; index += 1) {
    if (compressed) {
      metrics.push({
        left: view.getUint8(cursor++) - 128,
        right: view.getUint8(cursor++) - 128,
        width: view.getUint8(cursor++) - 128,
        ascent: view.getUint8(cursor++) - 128,
        descent: view.getUint8(cursor++) - 128,
      })
    } else {
      const values = []
      for (let item = 0; item < 5; item += 1) { values.push(view.getInt16(cursor, metricsEndian)); cursor += 2 }
      cursor += 2
      metrics.push({ left: values[0], right: values[1], width: values[2], ascent: values[3], descent: values[4] })
    }
  }

  cursor = bitmapsTable.offset
  const bitmapFormat = view.getUint32(cursor, true); cursor += 4
  const bitmapEndian = littleEndian(bitmapFormat)
  const bitmapCount = view.getUint32(cursor, bitmapEndian); cursor += 4
  const offsets = []
  for (let index = 0; index < bitmapCount; index += 1) { offsets.push(view.getUint32(cursor, bitmapEndian)); cursor += 4 }
  const sizes = []
  for (let index = 0; index < 4; index += 1) { sizes.push(view.getUint32(cursor, bitmapEndian)); cursor += 4 }
  const bitmapData = bytes.subarray(cursor, cursor + sizes[bitmapFormat & 3])
  const glyphPad = 1 << (bitmapFormat & 3)
  const scanUnit = 1 << ((bitmapFormat >> 4) & 3)
  const byteOrder = bitmapFormat & 4 ? 'MSB' : 'LSB'
  const bitOrder = bitmapFormat & 8 ? 'MSB' : 'LSB'

  cursor = encodingTable.offset
  const encodingFormat = view.getUint32(cursor, true); cursor += 4
  const encodingEndian = littleEndian(encodingFormat)
  const minByte2 = view.getUint16(cursor, encodingEndian); cursor += 2
  const maxByte2 = view.getUint16(cursor, encodingEndian); cursor += 2
  const minByte1 = view.getUint16(cursor, encodingEndian); cursor += 2
  const maxByte1 = view.getUint16(cursor, encodingEndian); cursor += 2
  cursor += 2
  const glyphIndices = []
  const encodingCount = (maxByte2 - minByte2 + 1) * (maxByte1 - minByte1 + 1)
  for (let index = 0; index < encodingCount; index += 1) {
    const value = view.getUint16(cursor, encodingEndian); cursor += 2
    glyphIndices.push(value === 0xffff ? -1 : value)
  }

  function glyphIndex(byte1, byte2) {
    if (byte1 < minByte1 || byte1 > maxByte1 || byte2 < minByte2 || byte2 > maxByte2) return -1
    return glyphIndices[(byte1 - minByte1) * (maxByte2 - minByte2 + 1) + byte2 - minByte2]
  }

  function glyph(index) {
    const metric = metrics[index]
    const width = metric.right - metric.left
    const height = metric.ascent + metric.descent
    const rowBytes = Math.ceil(width / 8)
    const paddedRowBytes = Math.ceil(rowBytes / glyphPad) * glyphPad
    const packed = new Uint8Array(Math.ceil(width * height / 8))
    let targetBit = 0
    for (let y = 0; y < height; y += 1) {
      const rowOffset = offsets[index] + y * paddedRowBytes
      for (let x = 0; x < width; x += 1) {
        let byteIndex = Math.floor(x / 8)
        if (scanUnit > 1 && byteOrder === 'LSB') {
          byteIndex = Math.floor(byteIndex / scanUnit) * scanUnit + scanUnit - 1 - (byteIndex % scanUnit)
        }
        const bit = bitOrder === 'MSB' ? 7 - (x % 8) : x % 8
        if ((bitmapData[rowOffset + byteIndex] >> bit) & 1) packed[targetBit >> 3] |= 1 << (targetBit & 7)
        targetBit += 1
      }
    }
    return { width, height, x: metric.left, y: -metric.descent, dx: metric.width, packed }
  }
  return { glyphIndex, glyph }
}

function buildCatalog(font) {
  const records = []
  const ranges = [[0x3000, 0x303f], [0x3400, 0x4dbf], [0x4e00, 0x9fff]]
  for (const [start, end] of ranges) {
    for (let codePoint = start; codePoint <= end; codePoint += 1) {
      const index = font.glyphIndex(codePoint >> 8, codePoint & 0xff)
      if (index < 0) continue
      const value = font.glyph(index)
      if (value.width < 1 || value.height < 1 || value.packed.length > 255) continue
      const record = new Uint8Array(8 + value.packed.length)
      const view = new DataView(record.buffer)
      view.setUint16(0, codePoint, true)
      record[2] = value.width; record[3] = value.height
      record[4] = value.x & 0xff; record[5] = value.y & 0xff
      record[6] = value.dx; record[7] = value.packed.length
      record.set(value.packed, 8)
      records.push(record)
    }
  }
  records.sort((a, b) => new DataView(a.buffer).getUint16(0, true) - new DataView(b.buffer).getUint16(0, true))
  const total = 8 + records.reduce((sum, record) => sum + record.length, 0)
  const output = new Uint8Array(total)
  output.set([0x49, 0x54, 0x47, 0x43])
  const view = new DataView(output.buffer)
  view.setUint16(4, 1, true); view.setUint16(6, records.length, true)
  let offset = 8
  for (const record of records) { output.set(record, offset); offset += record.length }
  return output
}

const sources = [
  ['wqy9', resolve(root, 'assets/fonts/wenquanyi/wenquanyi_9ptb.pcf')],
  ['wqy12', resolve(root, 'assets/fonts/wenquanyi/wenquanyi_12ptb.pcf')],
]
const catalogs = {}
for (const [name, path] of sources) catalogs[name] = buildCatalog(parsePcf(await readFile(path)))
const encode = (bytes) => gzipSync(Buffer.from(bytes.buffer, bytes.byteOffset, bytes.byteLength), { level: 9 }).toString('base64')
await writeFile(output, `// Generated by tools/generate-glyph-catalog.mjs. Do not edit.\nexport const glyphCatalogData = {\n  wqy9: { count: ${new DataView(catalogs.wqy9.buffer).getUint16(6, true)}, gzipBase64: '${encode(catalogs.wqy9)}' },\n  wqy12: { count: ${new DataView(catalogs.wqy12.buffer).getUint16(6, true)}, gzipBase64: '${encode(catalogs.wqy12)}' },\n} as const\n`)
console.log(`generated ${output}: ${catalogs.wqy9.length} + ${catalogs.wqy12.length} bytes`)
