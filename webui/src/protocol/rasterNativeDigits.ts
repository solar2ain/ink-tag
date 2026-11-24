import '../../../cli/src/inktag_cli/_assets/fontRoles.js'
import { requireLocalFontGlyphs } from './localFontCoverage'
const compiler = (globalThis as unknown as { InkTagFontRoles: {
  compressNativeDigits(glyphs: unknown[]): number[]
} }).InkTagFontRoles
let sequence = 0
/** Local-only TTF/OTF. Keep grayscale until fitting to the final native cell. */
export async function rasterNativeDigits(file: File, heights: readonly number[]) {
  const family = `InkTagLocalDigits${++sequence}`
  if (file.size > 32 * 1024 * 1024) throw Error('本地字体文件超过 32 MiB')
  const buffer = await file.arrayBuffer()
  requireLocalFontGlyphs(buffer, [...'0123456789:'].map((c) => c.charCodeAt(0)))
  const face = new FontFace(family, buffer)
  await face.load(); document.fonts.add(face)
  try {
    return heights.map((height) => {
      if (!Number.isInteger(height) || height < 1 || height > 127) throw Error('字号超出 U8g2 原生度量范围')
      const measure = document.createElement('canvas').getContext('2d')!
      measure.font = `${height * 2}px "${family}"`; measure.textBaseline = 'alphabetic'
      const chars = '0123456789:'
      const boxes = [...chars].map((c) => measure.measureText(c))
      const top = Math.max(...boxes.map((b) => b.actualBoundingBoxAscent))
      const bottom = Math.max(...boxes.map((b) => b.actualBoundingBoxDescent))
      if (boxes.some((b) => b.actualBoundingBoxLeft + b.actualBoundingBoxRight <= 0) || top + bottom <= 0)
        throw Error('字形源缺少完整数字或冒号')
      const width = Math.max(1, Math.floor(height * 5 / 8))
      const padding = Math.min(Math.max(0, Math.floor((width - 1) / 2)), Math.max(1, Math.floor(height / 32)))
      const advance = width + padding, colon = Math.max(1, Math.floor(width / 2))
      const scale = Math.min((width - 2 * padding) / Math.max(...boxes.slice(0, 10).map((b) => b.actualBoundingBoxLeft + b.actualBoundingBoxRight)),
        (height - 2 * padding) / (top + bottom), Math.max(1, colon - 2) / (boxes[10].actualBoundingBoxLeft + boxes[10].actualBoundingBoxRight))
      const glyphs = [...chars].map((c, i) => {
        const cellWidth = i === 10 ? colon : advance
        const canvas = document.createElement('canvas'); canvas.width = cellWidth; canvas.height = height
        const context = canvas.getContext('2d')!
        context.font = `${height * 2 * scale}px "${family}"`; context.textBaseline = 'alphabetic'; context.fillStyle = 'white'
        const metrics = context.measureText(c), inkWidth = metrics.actualBoundingBoxLeft + metrics.actualBoundingBoxRight
        context.fillText(c, (cellWidth - inkWidth) / 2 + metrics.actualBoundingBoxLeft, padding + top * scale)
        const rgba = context.getImageData(0, 0, cellWidth, height).data
        return { codePoint: c.charCodeAt(0), width: cellWidth, height, x: 0, y: 0, dx: cellWidth,
          pixels: Array.from({ length: cellWidth * height }, (_, p) => rgba[p * 4 + 3] >= 128 ? 1 : 0) }
      })
      return Uint8Array.from(compiler.compressNativeDigits(glyphs))
    })
  } finally { document.fonts.delete(face) }
}
