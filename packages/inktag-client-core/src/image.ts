import {
  TransferFormat,
  transferFormatFor,
  transformRgba,
  type RgbaFrame,
  type ScreenProfileLikeV2,
} from './screen'
import { neutralPaletteMismatch } from './image-processing'

export type ImageStep = 'bw' | 'red' | 'color' | 'gray-msb' | 'gray-lsb' | 'e6-area0' | 'e6-area6'
export interface ImageTransfer { data: Uint8Array; step: ImageStep }
export interface EncodedImage { format: number; transfers: ImageTransfer[] }
export type ImageGeometry = Pick<ScreenProfileLikeV2,
  'width' | 'height' | 'visibleX' | 'visibleY' | 'visibleWidth' | 'visibleHeight' | 'transform' | 'color'>


function luminance(r: number, g: number, b: number): number { return .299 * r + .587 * g + .114 * b }
function colorDistance(r: number, g: number, b: number, target: readonly [number, number, number]): number {
  if (neutralPaletteMismatch(r, g, b, target)) return Number.POSITIVE_INFINITY
  const dr = r - target[0]
  const dg = g - target[1]
  const db = b - target[2]
  return dr * dr + dg * dg + db * db
}

function framedImage(image: RgbaFrame, screen: ImageGeometry): RgbaFrame {
  const transformed = transformRgba(image, screen.transform)
  if (transformed.width !== screen.visibleWidth || transformed.height !== screen.visibleHeight) {
    throw new RangeError('页面尺寸与屏幕可视区域不匹配')
  }
  const data = new Uint8ClampedArray(screen.width * screen.height * 4)
  data.fill(0xff)
  for (let y = 0; y < transformed.height; y++) {
    const source = y * transformed.width * 4
    const target = ((screen.visibleY + y) * screen.width + screen.visibleX) * 4
    data.set(transformed.data.subarray(source, source + transformed.width * 4), target)
  }
  return { width: screen.width, height: screen.height, data }
}

function mono(frame: RgbaFrame): Uint8Array {
  const byteWidth = Math.ceil(frame.width / 8)
  const output = new Uint8Array(byteWidth * frame.height)
  for (let y = 0; y < frame.height; y++) for (let x = 0; x < frame.width; x++) {
    const pixel = (y * frame.width + x) * 4
    if (luminance(frame.data[pixel], frame.data[pixel + 1], frame.data[pixel + 2]) >= 140) {
      output[y * byteWidth + (x >> 3)] |= 1 << (7 - (x & 7))
    }
  }
  return output
}

const accents: Record<number, readonly [number, number, number]> = {
  2: [220, 48, 38], 5: [235, 194, 25], 6: [44, 97, 165], 7: [19, 116, 99], 8: [225, 139, 35],
}

function dual(frame: RgbaFrame, color: number): { black: Uint8Array; accent: Uint8Array } {
  const byteWidth = Math.ceil(frame.width / 8)
  const black = new Uint8Array(byteWidth * frame.height)
  const accent = new Uint8Array(byteWidth * frame.height)
  accent.fill(0xff)
  const target = accents[color] || accents[2]
  for (let y = 0; y < frame.height; y++) for (let x = 0; x < frame.width; x++) {
    const pixel = (y * frame.width + x) * 4
    const r = frame.data[pixel]
    const g = frame.data[pixel + 1]
    const b = frame.data[pixel + 2]
    const index = y * byteWidth + (x >> 3)
    const bit = 7 - (x & 7)
    const isAccent = colorDistance(r, g, b, target) < colorDistance(r, g, b, [0, 0, 0]) &&
      colorDistance(r, g, b, target) < colorDistance(r, g, b, [255, 255, 255])
    if (isAccent) {
      accent[index] &= ~(1 << bit)
      // Match native GFX: leave the black plane white under accent.
      black[index] |= 1 << bit
    }
    else if (luminance(r, g, b) >= 140) black[index] |= 1 << bit
  }
  return { black, accent }
}

function indexed2(frame: RgbaFrame, color: number): Uint8Array {
  const output = new Uint8Array(Math.ceil(frame.width * frame.height / 4))
  const palette: Array<{ value: number; rgb: readonly [number, number, number] }> = [
    { value: 0, rgb: [0, 0, 0] }, { value: 1, rgb: [255, 255, 255] }, { value: 3, rgb: [220, 48, 38] },
  ]
  if (color === 3) palette.push({ value: 2, rgb: [235, 194, 25] })
  for (let pixel = 0; pixel < frame.width * frame.height; pixel++) {
    const offset = pixel * 4
    let closest = palette[0]
    for (const candidate of palette.slice(1)) {
      if (colorDistance(frame.data[offset], frame.data[offset + 1], frame.data[offset + 2], candidate.rgb) <
          colorDistance(frame.data[offset], frame.data[offset + 1], frame.data[offset + 2], closest.rgb)) closest = candidate
    }
    output[pixel >> 2] |= closest.value << (6 - (pixel & 3) * 2)
  }
  return output
}

function gray4(frame: RgbaFrame): { ram1: Uint8Array; ram2: Uint8Array } {
  const byteWidth = Math.ceil(frame.width / 8)
  const high = new Uint8Array(byteWidth * frame.height)
  const low = new Uint8Array(byteWidth * frame.height)
  for (let y = 0; y < frame.height; y++) for (let x = 0; x < frame.width; x++) {
    const pixel = (y * frame.width + x) * 4
    const value = luminance(frame.data[pixel], frame.data[pixel + 1], frame.data[pixel + 2])
    const level = value < 43 ? 0 : value < 128 ? 1 : value < 213 ? 2 : 3
    const index = y * byteWidth + (x >> 3)
    const bit = 7 - (x & 7)
    high[index] |= ((level >> 1) & 1) << bit
    low[index] |= (level & 1) << bit
  }
  return { ram1: low.map((value) => (~value) & 0xff), ram2: high.map((value) => (~value) & 0xff) }
}

function indexed4FromPlanes(black: Uint8Array, accent: Uint8Array): Uint8Array {
  const output = new Uint8Array(black.length * 4)
  let target = 0
  for (let index = 0; index < black.length; index++) {
    let blackByte = black[index]
    let accentByte = accent[index]
    for (let bit = 0; bit < 8; bit += 2) {
      let value = (accentByte & 0x80) === 0 ? 4 : (blackByte & 0x80) === 0 ? 0 : 3
      blackByte = (blackByte << 1) & 0xff
      accentByte = (accentByte << 1) & 0xff
      value = (value << 4) | ((accentByte & 0x80) === 0 ? 4 : (blackByte & 0x80) === 0 ? 0 : 3)
      blackByte = (blackByte << 1) & 0xff
      accentByte = (accentByte << 1) & 0xff
      output[target++] = value
    }
  }
  return output
}

function ospreyE6(frame: RgbaFrame): Uint8Array {
  const palette = [
    { rgb: [0, 0, 0] as const, code: 0 }, { rgb: [255, 255, 255] as const, code: 1 },
    { rgb: [235, 194, 25] as const, code: 2 }, { rgb: [220, 48, 38] as const, code: 3 },
    { rgb: [0, 0, 255] as const, code: 5 }, { rgb: [41, 204, 20] as const, code: 6 },
  ]
  const output = new Uint8Array(Math.ceil(frame.width * frame.height / 2))
  for (let pixel = 0; pixel < frame.width * frame.height; pixel++) {
    const [r, g, b] = frame.data.subarray(pixel * 4, pixel * 4 + 3)
    let closest = palette[0]
    for (const candidate of palette.slice(1)) {
      if (colorDistance(r, g, b, candidate.rgb) < colorDistance(r, g, b, closest.rgb)) closest = candidate
    }
    output[pixel >> 1] |= closest.code << ((pixel & 1) ? 0 : 4)
  }
  return output
}

function lenmoryE6(frame: RgbaFrame): { area0: Uint8Array; area6: Uint8Array } {
  const area0 = new Uint8Array(Math.ceil(frame.width * frame.height / 4))
  const area6 = new Uint8Array(area0.length)
  const palette = [
    { rgb: [0, 0, 0] as const, area0: 1, area6: 0 }, { rgb: [255, 255, 255] as const, area0: 1, area6: 1 },
    { rgb: [220, 48, 38] as const, area0: 3, area6: 1 }, { rgb: [235, 194, 25] as const, area0: 1, area6: 2 },
    { rgb: [41, 204, 20] as const, area0: 2, area6: 1 }, { rgb: [0, 0, 255] as const, area0: 0, area6: 1 },
  ]
  for (let pixel = 0; pixel < frame.width * frame.height; pixel++) {
    const offset = pixel * 4
    let closest = palette[0]
    for (const candidate of palette.slice(1)) {
      if (colorDistance(frame.data[offset], frame.data[offset + 1], frame.data[offset + 2], candidate.rgb) <
          colorDistance(frame.data[offset], frame.data[offset + 1], frame.data[offset + 2], closest.rgb)) closest = candidate
    }
    const shift = 6 - (pixel & 3) * 2
    area0[pixel >> 2] |= closest.area0 << shift
    area6[pixel >> 2] |= closest.area6 << shift
  }
  return { area0, area6 }
}

export function encodeImage(image: RgbaFrame, screen: ScreenProfileLikeV2): EncodedImage {
  const driver = 'driver' in screen ? screen.driver : screen.controller
  return encodeFrame(framedImage(image, screen), screen.color, transferFormatFor(screen), driver === 0x40 || driver === 0x41)
}

/** D1 advertises packing directly. Native NFC pads every physical row. */
export function encodeNfcImage(image: RgbaFrame, geometry: ImageGeometry, format: number): EncodedImage {
  const alignment = format === TransferFormat.INDEXED_4BPP ? (geometry.color === 10 ? 2 : 8)
    : format === TransferFormat.INDEXED_2BPP || format === TransferFormat.INDEXED_2X2BPP ? 4 : 1
  const width = Math.ceil(geometry.width / alignment) * alignment
  const encoded = encodeFrame(framedImage(image, { ...geometry, width }), geometry.color, format, geometry.color === 10)
  if (format === TransferFormat.INDEXED_4BPP && geometry.color !== 10 && width !== geometry.width) {
    const rowBytes = Math.ceil(geometry.width / 2)
    const packed = encoded.transfers[0].data
    const data = new Uint8Array(rowBytes * geometry.height)
    for (let y = 0; y < geometry.height; y++) {
      const start = y * width / 2
      data.set(packed.subarray(start, start + rowBytes), y * rowBytes)
    }
    encoded.transfers[0].data = data
  }
  return encoded
}

function encodeFrame(frame: RgbaFrame, color: number, format: number, osprey: boolean): EncodedImage {
  if (format === TransferFormat.MONO_1BPP) return { format, transfers: [{ data: mono(frame), step: 'bw' }] }
  if (format === TransferFormat.DUAL_1BPP) {
    const planes = dual(frame, color)
    return { format, transfers: [{ data: planes.black, step: 'bw' }, { data: planes.accent, step: 'red' }] }
  }
  if (format === TransferFormat.INDEXED_2BPP) return { format, transfers: [{ data: indexed2(frame, color), step: 'color' }] }
  if (format === TransferFormat.PLANAR_2X1BPP) {
    const planes = gray4(frame)
    return { format, transfers: [{ data: planes.ram1, step: 'gray-msb' }, { data: planes.ram2, step: 'gray-lsb' }] }
  }
  if (format === TransferFormat.INDEXED_4BPP) {
    if (osprey) return { format, transfers: [{ data: ospreyE6(frame), step: 'bw' }] }
    const planes = color === 1
      ? { black: mono(frame), accent: new Uint8Array(mono(frame).length).fill(0xff) }
      : dual(frame, color)
    return { format, transfers: [{ data: indexed4FromPlanes(planes.black, planes.accent), step: 'bw' }] }
  }
  if (format === TransferFormat.INDEXED_2X2BPP) {
    const planes = lenmoryE6(frame)
    return { format, transfers: [{ data: planes.area0, step: 'e6-area0' }, { data: planes.area6, step: 'e6-area6' }] }
  }
  throw new RangeError('当前屏幕传输格式尚未实现')
}

function putPixel(data: Uint8ClampedArray, pixel: number, rgb: readonly [number, number, number]): void {
  const offset = pixel * 4
  data[offset] = rgb[0]
  data[offset + 1] = rgb[1]
  data[offset + 2] = rgb[2]
  data[offset + 3] = 0xff
}

/** Present raw panel pixels upright in the configured reading viewport. */
export function logicalImage(frame: RgbaFrame, screen: Pick<ScreenProfileLikeV2, 'width' | 'height' | 'visibleX' | 'visibleY' | 'visibleWidth' | 'visibleHeight' | 'transform'>): RgbaFrame {
  if (frame.width !== screen.width || frame.height !== screen.height) throw new RangeError('回读图片尺寸与当前屏幕配置不一致')
  const visible = new Uint8ClampedArray(screen.visibleWidth * screen.visibleHeight * 4)
  for (let y = 0; y < screen.visibleHeight; y++) {
    const source = ((screen.visibleY + y) * screen.width + screen.visibleX) * 4
    const target = y * screen.visibleWidth * 4
    visible.set(frame.data.subarray(source, source + screen.visibleWidth * 4), target)
  }
  const transformed = { width: screen.visibleWidth, height: screen.visibleHeight, data: visible }
  const rotation = screen.transform & 3
  const mirrorX = (screen.transform & 4) !== 0
  const mirrorY = (screen.transform & 8) !== 0
  const width = rotation === 1 || rotation === 3 ? transformed.height : transformed.width
  const height = rotation === 1 || rotation === 3 ? transformed.width : transformed.height
  const data = new Uint8ClampedArray(width * height * 4)
  for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) {
    let tx = x
    let ty = y
    if (rotation === 1) { tx = height - 1 - y; ty = x }
    else if (rotation === 2) { tx = width - 1 - x; ty = height - 1 - y }
    else if (rotation === 3) { tx = y; ty = width - 1 - x }
    if (mirrorX) tx = transformed.width - 1 - tx
    if (mirrorY) ty = transformed.height - 1 - ty
    const source = (ty * transformed.width + tx) * 4
    data.set(transformed.data.subarray(source, source + 4), (y * width + x) * 4)
  }
  return { width, height, data }
}

/** Decode the exact device-ready bytes returned by READ_RESOURCE. */
export function decodeStoredImage(payload: Uint8Array, screen: ScreenProfileLikeV2, format: number, segments: number, readingRotation = 0): RgbaFrame {
  if (!Number.isInteger(readingRotation) || readingRotation < 0 || readingRotation > 3) throw new RangeError('图片阅读方向无效')
  const pixels = screen.width * screen.height
  const byteWidth = Math.ceil(screen.width / 8)
  const monoBytes = byteWidth * screen.height
  const twoBitBytes = Math.ceil(pixels / 4)
  const data = new Uint8ClampedArray(pixels * 4)
  const black = [0, 0, 0] as const
  const white = [255, 255, 255] as const
  const red = [220, 48, 38] as const
  const yellow = [235, 194, 25] as const
  const green = [41, 204, 20] as const
  const blue = [0, 0, 255] as const
  const accent = accents[screen.color] || red
  if (format === TransferFormat.MONO_1BPP) {
    if (segments !== 1 || payload.length !== monoBytes) throw new RangeError('黑白图槽数据长度无效')
    for (let y = 0; y < screen.height; y++) for (let x = 0; x < screen.width; x++) {
      const value = (payload[y * byteWidth + (x >> 3)] >> (7 - (x & 7))) & 1
      putPixel(data, y * screen.width + x, value ? white : black)
    }
  } else if (format === TransferFormat.DUAL_1BPP) {
    if (segments !== 2 || payload.length !== monoBytes * 2) throw new RangeError('双色图槽数据长度无效')
    const accentPlane = payload.subarray(monoBytes)
    for (let y = 0; y < screen.height; y++) for (let x = 0; x < screen.width; x++) {
      const index = y * byteWidth + (x >> 3)
      const bit = 7 - (x & 7)
      const isAccent = ((accentPlane[index] >> bit) & 1) === 0
      const isWhite = ((payload[index] >> bit) & 1) !== 0
      putPixel(data, y * screen.width + x, isAccent ? accent : isWhite ? white : black)
    }
  } else if (format === TransferFormat.INDEXED_2BPP) {
    if (segments !== 1 || payload.length !== twoBitBytes) throw new RangeError('四色图槽数据长度无效')
    const palette = [black, white, yellow, red] as const
    for (let pixel = 0; pixel < pixels; pixel++) putPixel(data, pixel, palette[(payload[pixel >> 2] >> (6 - (pixel & 3) * 2)) & 3])
  } else if (format === TransferFormat.PLANAR_2X1BPP) {
    if (segments !== 2 || payload.length !== monoBytes * 2) throw new RangeError('灰阶图槽数据长度无效')
    const ram2 = payload.subarray(monoBytes)
    const levels = [0, 85, 170, 255]
    for (let y = 0; y < screen.height; y++) for (let x = 0; x < screen.width; x++) {
      const index = y * byteWidth + (x >> 3)
      const bit = 7 - (x & 7)
      const low = ((~payload[index]) >> bit) & 1
      const high = ((~ram2[index]) >> bit) & 1
      const level = levels[(high << 1) | low]
      putPixel(data, y * screen.width + x, [level, level, level])
    }
  } else if (format === TransferFormat.INDEXED_4BPP) {
    const expected = Math.ceil(pixels / 2)
    if (segments !== 1 || payload.length !== expected) throw new RangeError('四位索引图槽数据长度无效')
    for (let pixel = 0; pixel < pixels; pixel++) {
      const value = (payload[pixel >> 1] >> ((pixel & 1) ? 0 : 4)) & 0x0f
      const driver = 'driver' in screen ? screen.driver : screen.controller
      if (driver === 0x40 || driver === 0x41) {
        const palette: Record<number, readonly [number, number, number]> = {
          0: black, 1: white, 2: yellow, 3: red, 5: blue, 6: green,
        }
        if (!palette[value]) throw new RangeError('鱼鹰 E6 色码无效')
        putPixel(data, pixel, palette[value])
      } else putPixel(data, pixel, value === 0 ? black : value === 4 ? accent : white)
    }
  } else if (format === TransferFormat.INDEXED_2X2BPP) {
    if (segments !== 2 || payload.length !== twoBitBytes * 2) throw new RangeError('六色图槽数据长度无效')
    const area6 = payload.subarray(twoBitBytes)
    const palette = new Map<string, readonly [number, number, number]>([
      ['1:0', black], ['1:1', white], ['3:1', red], ['1:2', yellow], ['2:1', green], ['0:1', blue],
    ])
    for (let pixel = 0; pixel < pixels; pixel++) {
      const shift = 6 - (pixel & 3) * 2
      const area0Value = (payload[pixel >> 2] >> shift) & 3
      const area6Value = (area6[pixel >> 2] >> shift) & 3
      putPixel(data, pixel, palette.get(`${area0Value}:${area6Value}`) || white)
    }
  } else throw new RangeError(`不支持的图槽格式 ${format}`)
  return transformRgba(logicalImage({ width: screen.width, height: screen.height, data }, screen), (4 - readingRotation) & 3)
}

export function crc32Bytes(data: Uint8Array, initial = 0xffffffff): number {
  let crc = initial
  for (const byte of data) {
    crc = (crc ^ byte) >>> 0
    for (let bit = 0; bit < 8; bit++) crc = ((crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0)) >>> 0
  }
  return crc
}

export function imageCrc32(transfers: ImageTransfer[]): number {
  let crc = 0xffffffff
  for (const transfer of transfers) crc = crc32Bytes(transfer.data, crc)
  return (crc ^ 0xffffffff) >>> 0
}
