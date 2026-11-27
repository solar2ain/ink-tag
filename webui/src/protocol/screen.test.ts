import { describe, expect, it } from 'vitest'
import { colorCode, colors, controllerDisplayName, decodeScreenConfig, driverName, encodeScreenConfig, imageStoreCompatibilityChanged, makeScreenConfig, nativeUiScales, screenConfigsEqual, transferFormatFor, TransferFormat, transformRgba, validateScreenConfig } from './screen'

describe('screen configuration wire contract', () => {
  it('requires the full native200 viewport without restricting the older recipe', () => {
    const native = makeScreenConfig({ driver: 0x2f, recipe: 2, color: 2, transform: 0,
      width: 200, height: 200, visibleX: 0, visibleY: 0, visibleWidth: 200, visibleHeight: 200 })
    expect(validateScreenConfig(native)).toBe(true)
    expect([...encodeScreenConfig(native)]).toEqual([1, 47, 2, 2, 0, 200, 0, 200, 0, 0, 0, 0, 0, 200, 0, 200, 0])
    for (const viewport of [
      { visibleWidth: 192 }, { visibleHeight: 192 },
      { visibleX: 8, visibleWidth: 192 }, { visibleY: 8, visibleHeight: 192 },
    ]) {
      expect(validateScreenConfig({ ...native, ...viewport })).toBe(false)
      expect(() => encodeScreenConfig({ ...native, ...viewport })).toThrow(RangeError)
      expect(validateScreenConfig({ ...native, recipe: 1, ...viewport })).toBe(true)
    }
    for (let transform = 0; transform <= 15; transform++) {
      expect(validateScreenConfig({ ...native, transform })).toBe(true)
    }
  })
  it('accepts a standard SSD1680 2.66-inch panel with rotation and mirror', () => {
    const panel = makeScreenConfig({ driver: 0x23, color: 2, transform: 5,
      width: 152, height: 296, visibleX: 0, visibleY: 0, visibleWidth: 152, visibleHeight: 296 })
    expect(decodeScreenConfig(encodeScreenConfig(panel))).toEqual(panel)
    expect(validateScreenConfig({ ...panel, width: 176, visibleWidth: 176 })).toBe(true)
    expect(validateScreenConfig({ ...panel, width: 184, visibleWidth: 184 })).toBe(false)
    expect(validateScreenConfig({ ...panel, width: 153 })).toBe(false)
    expect(validateScreenConfig({ ...panel, height: 297 })).toBe(false)
    expect(validateScreenConfig({ ...panel, recipe: 1 })).toBe(false)
  })
  const uc8176 = makeScreenConfig({
    driver: 0x13, color: 2, transform: 0, width: 400, height: 300,
    visibleX: 0, visibleY: 0, visibleWidth: 400, visibleHeight: 300,
  })

  it('uses the release-v1 17-byte little-endian screen layout', () => {
    expect(Array.from(encodeScreenConfig(uc8176), (byte) => byte.toString(16).padStart(2, '0')).join(''))
      .toBe('011300020090012c010000000090012c01')
    expect(decodeScreenConfig(encodeScreenConfig(uc8176))).toEqual(uc8176)
  })

  it('packs native UI scale into reserved transform bits without changing wire size', () => {
    const scaled = { ...uc8176, nativeUiScale: 3 }
    const encoded = encodeScreenConfig(scaled)
    expect(encoded).toHaveLength(17)
    expect(encoded[4]).toBe(0x30)
    expect(decodeScreenConfig(encoded)).toEqual(scaled)
    expect(imageStoreCompatibilityChanged(uc8176, scaled)).toBe(false)
  })

  it('offers every documented font scale in display order', () => {
    expect(nativeUiScales.map(([, label]) => label)).toEqual(['自动', '100%', '125%', '150%', '175%', '200%'])
    const scaled = { ...uc8176, nativeUiScale: 5 }
    expect(encodeScreenConfig(scaled)[4]).toBe(0x50)
    expect(decodeScreenConfig(encodeScreenConfig(scaled))).toEqual(scaled)
  })

  it('compares decoded and locally-created profiles by value instead of object key order', () => {
    const decoded = decodeScreenConfig(encodeScreenConfig(uc8176))!
    expect(JSON.stringify(decoded)).not.toBe(JSON.stringify(uc8176))
    expect(screenConfigsEqual(decoded, uc8176)).toBe(true)
    expect(screenConfigsEqual(decoded, { ...uc8176, transform: 2 })).toBe(false)
  })

  it('uses concrete controller names and keeps a selectable family controller', () => {
    expect(driverName(0x30)).toBe('JD79661')
    expect(driverName(0x33)).toBe('JD79668AA')
    expect(driverName(0x3f)).toBe('JD79XXX')
    expect(controllerDisplayName(0x3f)).toBe('JD79676')
    expect(controllerDisplayName(0x10)).toBe('UC 原厂驱动')
    expect(controllerDisplayName(0x15)).toBe('UC8276')
    expect(controllerDisplayName(0x33)).toBe('JD79668AA')
    expect(controllerDisplayName(0x2f)).toBe('SSD1681')
    expect(driverName(0x2f)).toBe('SSD1XXX') // Internal family token is not a new controller ID.
    expect(controllerDisplayName(0xff)).toBe('0xff')
  })

  it('keeps the Hanshow JD79668 recipe separate from generic and factory-B3 paths', () => {
    const hanshow = makeScreenConfig({
      driver: 0x33, recipe: 1, color: 3, transform: 8, width: 400, height: 300,
      visibleX: 0, visibleY: 0, visibleWidth: 400, visibleHeight: 300,
    })
    expect(validateScreenConfig(hanshow)).toBe(true)
    expect(driverName(0x33)).toBe('JD79668AA')
    expect(validateScreenConfig({ ...hanshow, color: 2 })).toBe(false)
  })

  it('accepts the exact Nebular 213 BWR/BWRY packed recipe without widening other combinations', () => {
    const nebular = makeScreenConfig({
      driver: 0x3f, recipe: 1, color: 2, transform: 5, width: 128, height: 250,
      visibleX: 0, visibleY: 0, visibleWidth: 122, visibleHeight: 250,
    })
    expect(validateScreenConfig(nebular)).toBe(true)
    expect(transferFormatFor(nebular)).toBe(TransferFormat.INDEXED_2BPP)
    expect(decodeScreenConfig(encodeScreenConfig(nebular))).toEqual(nebular)
    const bwry = { ...nebular, color: 3 }
    expect(validateScreenConfig(bwry)).toBe(true)
    expect(decodeScreenConfig(encodeScreenConfig(bwry))).toEqual(bwry)
    for (const color of [1, 4, 10]) {
      expect(validateScreenConfig({ ...nebular, color })).toBe(false)
    }
    expect(validateScreenConfig({ ...bwry, recipe: 2 })).toBe(false)
    expect(validateScreenConfig({ ...nebular, width: 132 })).toBe(false)
  })

  it('rejects an invalid viewport and unsupported controller color', () => {
    expect(validateScreenConfig({ ...uc8176, visibleX: 1 })).toBe(false)
    expect(validateScreenConfig({ ...uc8176, driver: 0x21, color: 3, width: 152, height: 152, visibleWidth: 152, visibleHeight: 152 })).toBe(false)
    expect(() => encodeScreenConfig({ ...uc8176, transform: 16 })).toThrow(RangeError)
  })

  it('applies rotation and mirrors as independent transform bits', () => {
    const rgba = new Uint8ClampedArray([
      1, 0, 0, 255, 2, 0, 0, 255,
      3, 0, 0, 255, 4, 0, 0, 255,
    ])
    const rotated = transformRgba({ width: 2, height: 2, data: rgba }, 1)
    expect([...rotated.data.filter((_, index) => index % 4 === 0)]).toEqual([3, 1, 4, 2])
    const mirrored = transformRgba({ width: 2, height: 2, data: rgba }, 4)
    expect([...mirrored.data.filter((_, index) => index % 4 === 0)]).toEqual([2, 1, 4, 3])
  })

  it('detects every field that makes stored image data incompatible', () => {
    expect(imageStoreCompatibilityChanged(uc8176, { ...uc8176 })).toBe(false)
    expect(imageStoreCompatibilityChanged(uc8176, { ...uc8176, color: 1 })).toBe(true)
    expect(imageStoreCompatibilityChanged(uc8176, { ...uc8176, recipe: 1 })).toBe(true)
    expect(imageStoreCompatibilityChanged(uc8176, { ...uc8176, transform: 4 })).toBe(true)
    expect(imageStoreCompatibilityChanged(uc8176, { ...uc8176, visibleWidth: 392 })).toBe(true)
    expect(imageStoreCompatibilityChanged(uc8176, { ...uc8176, schemaVersion: 2 })).toBe(false)
  })

  it('labels six-color black as K while keeping the color ID and two-stage format unchanged', () => {
    expect(colorCode(10)).toBe('KWRYGB')
    expect(colors.find(([id]) => id === 10)?.[1]).toBe('KWRYGB（六色）')
    const e6 = makeScreenConfig({ driver: 0x32, recipe: 1, color: 10, transform: 0, nativeUiScale: 2,
      width: 760, height: 568, visibleX: 0, visibleY: 0, visibleWidth: 760, visibleHeight: 568 })
    expect(validateScreenConfig(e6)).toBe(true)
    expect(encodeScreenConfig(e6)[3]).toBe(10)
    expect(decodeScreenConfig(encodeScreenConfig(e6))).toEqual(e6)
    expect(transferFormatFor(e6)).toBe(TransferFormat.INDEXED_2X2BPP)
  })
})
