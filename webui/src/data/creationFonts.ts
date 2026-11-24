import type { Document, Style } from '../../../packages/inktag-editor-core/src/index'
import defaultFontData from 'virtual:inktag-default-creation-fonts'
import { loadOptionalAsset } from './optionalAssets'

export const creationPixelFonts = [
  { family: 'InkTag Pixel Body', label: 'Z 工坊像素黑体 CN', asset: 'pixelBody', sizes: [12] },
  { family: 'InkTag Pixel Compact', label: '目哉像素', asset: 'pixelCompact', sizes: [12] },
  { family: 'InkTag Pixel Mini', label: '寒蝉点阵体 · 7 px 字形', asset: 'pixelMini', sizes: [8] },
  { family: 'InkTag Pixel Song', label: '文泉驿点阵宋体', asset: 'pixelSong', sizes: [12, 13, 14, 15, 16] },
  { family: 'InkTag Pixel Fusion', label: '缝合像素 · 简体中文', asset: 'pixelFusion', sizes: [8, 10, 12] },
] as const
const song = creationPixelFonts[3]

export function defaultCreationTypography(width: number, height: number) {
  return Math.max(width, height) <= 300
    ? { fontFamily: 'InkTag Pixel Fusion', fontSize: 10 }
    : { fontFamily: 'InkTag Pixel Body', fontSize: 12 }
}

type PixelFont = typeof creationPixelFonts[number]
type Asset = PixelFont['asset']
type Glyph = { advance: number; left: number; top: number; width: number; height: number; bits: Uint8Array }
type Strikes = Map<number, Map<number, Glyph>>
const loaded = new Map<Asset, Strikes>()
const loading = new Map<Asset, Promise<void>>()

export function creationPixelFont(family?: string) {
  if (family === 'serif' || /Noto Serif SC|Source Han Serif SC|Songti SC|SimSun|FangSong/i.test(family ?? '')) return song
  return creationPixelFonts.find(font => font.family === family)
}

export function pixelFontSizes(font: PixelFont, max = 240) {
  return [...new Set(font.sizes.flatMap(size => Array.from({ length: Math.floor(max / size) }, (_, i) => size * (i + 1))))].sort((a, b) => a - b)
}

export function pixelFontSize(font: PixelFont, size: number, max = 240) {
  // When equally close, use the smaller option to keep text within its frame.
  return pixelFontSizes(font, max).reduce((best, value) => Math.abs(value - size) < Math.abs(best - size) ? value : best)
}

function decodeStrikes(bytes: Uint8Array): Strikes {
  const data = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength)
  const strikes: Strikes = new Map()
  let offset = 1
  for (let i = 0; i < data.getUint8(0); i++) {
    const size = data.getUint8(offset++)
    const count = data.getUint32(offset, true)
    offset += 4
    const glyphs = new Map<number, Glyph>()
    for (let g = 0; g < count; g++) {
      const codepoint = data.getUint32(offset, true)
      const advance = data.getUint8(offset + 4), left = data.getInt8(offset + 5), top = data.getInt8(offset + 6)
      const width = data.getUint8(offset + 7), height = data.getUint8(offset + 8)
      offset += 9
      const end = offset + Math.ceil(width / 8) * height
      glyphs.set(codepoint, { advance, left, top, width, height, bits: bytes.subarray(offset, end) })
      offset = end
    }
    strikes.set(size, glyphs)
  }
  if (offset !== bytes.length) throw new Error('Invalid pixel font size')
  return strikes
}

export function loadCreationFont(asset: Asset): Promise<void> {
  if (loaded.has(asset)) return Promise.resolve()
  const pending = loading.get(asset)
  if (pending) return pending
  const source = asset === 'pixelBody' || asset === 'pixelFusion'
    ? Promise.resolve(defaultFontData[asset]) : loadOptionalAsset(asset)
  const request = source.then(async url => {
    const bytes = Uint8Array.from(atob(url.split(',')[1]), character => character.charCodeAt(0))
    const stream = new Blob([bytes]).stream().pipeThrough(new DecompressionStream('gzip'))
    loaded.set(asset, decodeStrikes(new Uint8Array(await new Response(stream).arrayBuffer())))
  }).catch(() => {
    const label = creationPixelFonts.find(font => font.asset === asset)?.label
    throw new Error(`无法加载${label}。请确认页面旁的 assets 文件夹完整后重试。`)
  }).finally(() => loading.delete(asset))
  loading.set(asset, request)
  return request
}

export function creationFontAssets(doc: Document) {
  const assets = new Set<Asset>()
  const add = (style?: Partial<Style>) => {
    if (style?.fontId) return
    const font = creationPixelFont(style?.fontFamily)
    if (font) assets.add(font.asset)
  }
  for (const widget of doc.widgets) {
    add(widget.style)
    for (const cell of widget.cells.flat()) {
      add(cell.style)
      for (const run of cell.richText?.runs ?? []) if (!run.fontId) add(run.style)
    }
  }
  return [...assets].sort()
}

export function rasterCreationText(text: string, style: Style) {
  const font = creationPixelFont(style.fontFamily)
  const strikes = font && loaded.get(font.asset)
  if (!font || !strikes) return
  const size = pixelFontSize(font, style.fontSize)
  const strike = [...font.sizes].reverse().find(native => size % native === 0)!
  const scale = size / strike
  const glyphs = strikes.get(strike)!
  const placed: { glyph: Glyph; x: number }[] = []
  let previousHanRight: number | undefined
  let advance = 0, left = Infinity, top = Infinity, right = -Infinity, bottom = -Infinity
  for (const char of text.replaceAll('\t', '    ')) {
    const glyph = glyphs.get(char.codePointAt(0)!) ?? glyphs.get(0x25a1) ?? glyphs.get(63)
    if (!glyph) break
    const boldHan = style.bold && /\p{Script=Han}/u.test(char)
    // A one-pixel bold pass must leave a blank column between neighboring Han glyphs.
    const gap = boldHan && previousHanRight !== undefined
      ? Math.max(0, previousHanRight + 1 - (advance + glyph.left * scale)) : 0
    if (advance + gap + glyph.advance * scale > 8192) break
    advance += gap
    previousHanRight = undefined
    if (glyph.width && glyph.height) {
      const x = advance + glyph.left * scale
      const glyphRight = x + glyph.width * scale + Number(style.bold)
      placed.push({ glyph, x })
      left = Math.min(left, x)
      right = Math.max(right, glyphRight)
      top = Math.min(top, glyph.top * scale)
      bottom = Math.max(bottom, (glyph.top + glyph.height) * scale)
      if (boldHan) previousHanRight = glyphRight
    }
    advance += glyph.advance * scale
  }
  if (!placed.length) return { advance, left: 0, top: 0, right: 0, bottom: 0, width: 0, height: 0, mask: new Uint8Array() }
  const width = right - left, height = bottom - top
  const mask = new Uint8Array(width * height)
  for (const { glyph, x } of placed) {
    const stride = Math.ceil(glyph.width / 8)
    for (let row = 0; row < glyph.height; row++) for (let col = 0; col < glyph.width; col++) {
      if (!(glyph.bits[row * stride + (col >> 3)] & (0x80 >> (col & 7)))) continue
      const start = x - left + col * scale
      const y = (glyph.top + row) * scale - top
      for (let dy = 0; dy < scale; dy++) {
        const offset = (y + dy) * width + start
        mask.fill(1, offset, offset + scale + Number(style.bold))
      }
    }
  }
  return { advance, left, top, right, bottom, width, height, mask }
}
