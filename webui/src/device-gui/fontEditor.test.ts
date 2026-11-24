import { buildFontRoles } from '../protocol/fontRoles'
import { describe, expect, it } from 'vitest'
import { defaultGuiFonts, GUI_ASSET_MAX_BYTES } from '../protocol/guiAssets'
import { U8g2, U8g2Font } from '../vendor/u8g2-js/index.js'
import { addCatalogGlyphs, catalogGlyph, catalogMissingGlyphs, extendU8g2Font, missingChineseFontGlyphs } from './fontEditor'
import { glyphCatalogData } from './glyphCatalog.generated'

function render(font: Uint8Array, text: string) {
  const renderer = new U8g2({ width: 32, height: 24, layout: 'vertical' })
  renderer.setFont(U8g2Font.fromArray(font)); renderer.setFontMode(1); renderer.setFontPosTop()
  renderer.clearBuffer(); renderer.drawUTF8(0, 0, text)
  return renderer.displayMemory.slice()
}

describe('device font editor', () => {
  it('ships both full Chinese glyph catalogs, not just the device subsets', async () => {
    for (const face of ['wqy9','wqy12'] as const) {
      expect(glyphCatalogData[face].count).toBe(27535)
      for (const character of ['宝','鹏','龘']) {
        const glyph=await catalogGlyph(face,character)
        expect(glyph?.codePoint).toBe(character.codePointAt(0))
        expect(glyph?.pixels.some(pixel=>pixel===1)).toBe(true)
      }
    }
  })
  it('adds an absent CJK glyph to both Chinese faces without changing numeric faces', async () => {
    const fonts = defaultGuiFonts()
    const character = '鹏'
    expect(missingChineseFontGlyphs(fonts, character)).toEqual([character])
    const next = await addCatalogGlyphs(fonts, character)
    expect(U8g2Font.fromArray(next[0]).hasGlyph(character.codePointAt(0)!)).toBe(true)
    expect(U8g2Font.fromArray(next[1]).hasGlyph(character.codePointAt(0)!)).toBe(true)
    expect(next[2]).toEqual(fonts[2])
    expect(next[3]).toEqual(fonts[3])
  })

  it('reports characters unavailable in the bundled glyph source', async () => {
    expect(await catalogMissingGlyphs('国庆😀')).toEqual(['😀'])
  })

  it('re-encodes an existing source glyph pixel-identically', async () => {
    const fonts = defaultGuiFonts()
    const glyph = await catalogGlyph('wqy9', '国')
    expect(glyph).toBeDefined()
    expect(render(extendU8g2Font(fonts[0], [glyph!]), '国')).toEqual(render(fonts[0], '国'))
  })

  it('keeps a four-character countdown extension inside the device package limit', async () => {
    const pkg = buildFontRoles(await addCatalogGlyphs(defaultGuiFonts(), '鹏程万里'), {}, 2)
    expect(pkg.totalSize).toBeLessThanOrEqual(GUI_ASSET_MAX_BYTES)
  })
})
