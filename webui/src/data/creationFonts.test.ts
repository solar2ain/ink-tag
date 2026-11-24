import { readFileSync } from 'node:fs'
import { beforeAll, describe, expect, it, vi } from 'vitest'
import { creationPixelFonts, defaultCreationTypography, loadCreationFont, pixelFontSize, rasterCreationText } from './creationFonts'

vi.mock('./optionalAssets', () => ({
  loadOptionalAsset: async (name: string) => {
    const file = name.slice('pixel'.length).toLowerCase()
    return `data:application/gzip;base64,${readFileSync(new URL(`../assets/fonts/creation-${file}.bin.gz`, import.meta.url)).toString('base64')}`
  },
}))

const style = { fontSize: 12, color: '#000000', bold: false }

beforeAll(async () => { await Promise.all(creationPixelFonts.map(font => loadCreationFont(font.asset))) })

describe('creation pixel fonts', () => {
  it('doubles each source pixel without losing strokes at the 2× size', () => {
    const font = creationPixelFonts[0]
    const native = rasterCreationText('过期未读 · 晶鑫', { ...style, fontFamily: font.family })!
    const double = rasterCreationText('过期未读 · 晶鑫', { ...style, fontFamily: font.family, fontSize: 24 })!
    expect(native.mask.some(Boolean)).toBe(true)
    expect([double.width, double.height, double.advance]).toEqual([native.width * 2, native.height * 2, native.advance * 2])
    const repeated = Uint8Array.from(double.mask, (_, i) => {
      const row = Math.floor(i / double.width / 2)
      const column = Math.floor((i % double.width) / 2)
      return native.mask[row * native.width + column]
    })
    expect(double.mask).toEqual(repeated)
    expect(pixelFontSize(font, 17)).toBe(12)
  })

  it('uses all five distinct WenQuanYi native strikes and one-bit glyphs for each family', () => {
    const song = creationPixelFonts[3]
    const masks = new Set(song.sizes.map(fontSize => {
      const raster = rasterCreationText('日历刷新', { ...style, fontFamily: song.family, fontSize })!
      return `${raster.width}/${Buffer.from(raster.mask).toString('base64')}`
    }))
    expect(masks.size).toBe(5)
    for (const font of creationPixelFonts) {
      const mask = rasterCreationText('国', { ...style, fontFamily: font.family, fontSize: font.sizes[0] })!.mask
      expect(new Set(mask)).toEqual(new Set([0, 1]))
    }
  })
  it('uses the Fusion native strikes for small labels and keeps the compact timetable times', () => {
    const fusion = creationPixelFonts.find(font => font.family === 'InkTag Pixel Fusion')!
    expect(fusion.sizes).toEqual([8, 10, 12])
    const glyphs = fusion.sizes.map(fontSize =>
      rasterCreationText('四', { ...style, fontFamily: fusion.family, fontSize })!)
    expect(new Set(glyphs.map(glyph => glyph.advance)).size).toBe(3)
    const small = defaultCreationTypography(212, 104)
    expect(small).toEqual({ fontFamily: fusion.family, fontSize: 10 })
    expect(defaultCreationTypography(400, 300)).toEqual({ fontFamily: 'InkTag Pixel Body', fontSize: 12 })
    const plain = rasterCreationText('08:00', { ...style, ...small })!
    const bold = rasterCreationText('08:00', { ...style, ...small, bold: true })!
    expect(bold.advance).toBe(plain.advance)
  })

  it('keeps Muzai’s original narrow 8 px advance for common Han characters', () => {
    const compact = creationPixelFonts[1]
    expect(rasterCreationText('中文列表', { ...style, fontFamily: compact.family })?.advance).toBe(32)
  })

  it('preserves a blank column in bold timetable headings without widening times', () => {
    const fontFamily = creationPixelFonts[0].family
    const normal = { ...style, fontFamily }
    const bold = { ...normal, bold: true }
    const heading = rasterCreationText('周四', bold)!
    expect(heading.width).toBe(25)
    for (let y = 0; y < heading.height; y++) expect(heading.mask[y * heading.width + 12]).toBe(0)
    expect(rasterCreationText('周四', normal)!.advance).toBe(24)
    expect(rasterCreationText('08:00', bold)!.advance).toBe(rasterCreationText('08:00', normal)!.advance)
  })
})
