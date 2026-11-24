import { describe, expect, it } from 'vitest'
import { defaultGuiFonts } from './guiAssets'
import { buildFontRoles } from './fontRoles'
import { nativeFontRenderer, renderFontPreview, previewScreen } from './nativeFontPreview'
import { requireLocalFontGlyphs } from './localFontCoverage'
import { matchesInstalledFont } from '../components/NativeFontPreview'

describe('shipping C font preview', () => {
  it('uses actual common regions and applies rotation, viewport and mirrors', async () => {
    const r = await nativeFontRenderer()
    const a = r.defaultSizes({ width: 400, height: 300 })
    const b = r.defaultSizes({ width: 400, height: 300, rotation: 1 })
    expect(a.large).toBeGreaterThan(44)
    expect(a.large).toBeLessThanOrEqual(127)
    expect(b.large).toBeLessThan(a.large)
    expect(r.defaultSizes({ width: 400, height: 300, mirrors: 3 }).large).toBe(a.large)
    expect(() => r.defaultSizes({ width: 400, height: 300, rotation: 4 })).toThrow()
  })
  it('renders actual package bytes and rejects damaged fonts rather than falling back', async () => {
    const p = buildFontRoles(defaultGuiFonts(), {}, 31)
    const frame = await renderFontPreview({ width: 400, height: 300, pro: true }, 'clock', p.bytes)
    expect(frame.pixels.length).toBe(120000)
    expect(new Set(frame.pixels)).toContain(1)
    const damaged = p.bytes.slice(); damaged[damaged.length - 1] ^= 1
    await expect(renderFontPreview({ width: 400, height: 300 }, 'clock', damaged)).rejects.toThrow('rejected')
  })
  it.each(['clock', 'calendar', 'device-info'] as const)('shows %s upright in reading coordinates for every rotation and mirror', async page => {
    for (let rotation = 0; rotation < 4; rotation++) {
      const logical = { width: rotation & 1 ? 280 : 380, height: rotation & 1 ? 380 : 280, pro: true }
      const expected = await renderFontPreview(logical, page)
      for (let mirrors = 0; mirrors < 4; mirrors++) {
        const actual = await renderFontPreview({ width: 400, height: 300, visibleX: 10, visibleY: 5,
          visibleWidth: 380, visibleHeight: 280, rotation, mirrors, pro: true }, page)
        expect([actual.width, actual.height], `rotation=${rotation}, mirrors=${mirrors}`).toEqual([expected.width, expected.height])
        expect(actual.pixels, `rotation=${rotation}, mirrors=${mirrors}`).toEqual(expected.pixels)
      }
    }
  })
  it('requires generation, CRC, byte length and schema for the installed source', () => {
    const p = buildFontRoles(defaultGuiFonts(), {}, 31)
    const info = { version: 1, schema: 1, activeBank: 0, storageMode: 1, generation: 31,
      activeSize: p.totalSize, activeCrc32: p.packageCrc32, maximumSize: 32768 }
    expect(matchesInstalledFont(p, info)).toBe(true)
    expect(matchesInstalledFont(p, { ...info, generation: 32 })).toBe(false)
    expect(matchesInstalledFont(p, { ...info, activeSize: p.totalSize - 1 })).toBe(false)
    expect(matchesInstalledFont(null, info)).toBe(false)
    expect(matchesInstalledFont(p, { ...info, activeCrc32: p.packageCrc32 ^ 1 })).toBe(false)
  })
  it('passes the device viewport/transform without inventing another geometry', () => {
    expect(previewScreen({ width: 400, height: 300, transform: 13, nativeUiScale: 2,
      visibleX: 10, visibleY: 5, visibleWidth: 380, visibleHeight: 280,
      color: 2, driver: 0x26, recipe: 0, schemaVersion: 2 })).toMatchObject({ rotation: 1, mirrors: 3, visibleWidth: 380 })
  })
})

describe('local font cmap coverage', () => {
  it('checks the source has the digits, without browser fallback', () => {
    const b = new ArrayBuffer(80), v = new DataView(b)
    v.setUint32(0, 0x10000); v.setUint16(4, 1); v.setUint32(12, 0x636d6170)
    v.setUint32(20, 28); v.setUint32(24, 52); v.setUint16(30, 1)
    v.setUint16(32, 3); v.setUint16(34, 1); v.setUint32(36, 12)
    const p = 40; v.setUint16(p, 4); v.setUint16(p + 2, 24); v.setUint16(p + 6, 2)
    v.setUint16(p + 14, 58); v.setUint16(p + 18, 48); v.setInt16(p + 20, 1)
    expect(() => requireLocalFontGlyphs(b, [48, 57, 58])).not.toThrow()
    expect(() => requireLocalFontGlyphs(b, [59])).toThrow('不会借用系统字体')
    v.setUint32(36, 1000)
    expect(() => requireLocalFontGlyphs(b, [48])).toThrow('越界')
  })
})
