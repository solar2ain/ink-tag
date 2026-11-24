import { describe, expect, it } from 'vitest'
import { decodePinBlock, decodePinReport, encodePinBlock, gpioLabel, pinRoles, pinSetRequest, screenBusyActiveHigh, validatePinUpdate, type PinConfig } from './pinConfig'
import type { ScreenConfig } from '../types'
import registry from '../../../protocol/screen-controllers.json'
import { PinReportFlagV2 } from './generatedCommandsV2'
const config = (): PinConfig => ({ activeLow: 0x03c0, pins: Object.fromEntries(pinRoles.map((role, i) => [role, i < 10 ? i : 255])) as PinConfig['pins'] })
const report = (mask = 0x3ff) => new Uint8Array([1, 2, mask & 255, mask >> 8, ...encodePinBlock(config()), ...encodePinBlock(config())])
describe('complete pin configuration wire', () => {
  it('resolves every exact registered recipe and color', () => {
    for (const rule of registry.rules) for (const color of rule.colors) {
      const sizes = rule.colorFixedSizes.find(value => value.color === color)?.sizes
        ?? (rule.fixedSizes.length ? rule.fixedSizes : [rule.maxSize])
      for (const [width, height] of sizes) {
        const controller = registry.controllers.find(value => value.id === rule.controller)!
        const recipe = controller.recipes.find(value => value.id === rule.recipe)!
        const visibleWidth = [16, 63].includes(rule.controller) && rule.recipe === 1 ? 122 : width
        const screen: ScreenConfig = { schemaVersion: 1, driver: rule.controller, recipe: rule.recipe,
          color, width, height, visibleX: 0, visibleY: 0, visibleWidth, visibleHeight: height,
          transform: 0, nativeUiScale: 0 }
        expect(screenBusyActiveHigh(screen)).toBe(recipe.busyActiveHigh)
      }
    }
  })
  it('negotiates the derived BUSY flag without granting a writable polarity', () => {
    expect(PinReportFlagV2.RECIPE_BUSY_DERIVED).toBe(4)
    for (const flags of [2, 3, 6, 7]) {
      const data = report(); data[1] = flags
      const current = decodePinReport(data)
      expect(current.recipeBusyDerived).toBe(Boolean(flags & 4))
      const candidate = { ...current.effective, activeLow: current.effective.activeLow | 32 }
      expect(() => validatePinUpdate(candidate, current)).toThrow()
      expect(() => validatePinUpdate(current.effective, current)).not.toThrow()
      for (const bit of [8, 16, 32, 64, 128]) {
        data[1] = flags | bit
        expect(() => decodePinReport(data)).toThrow()
      }
    }
  })
  it('labels only known MCU numbering and rejects all editable screen signal polarities', () => {
    expect(gpioLabel(14, 'tlsr8359_hsel')).toBe('PB6 (14)')
    expect(gpioLabel(14, 'om6626_hsel')).toBe('GPIO 14')
    expect(gpioLabel(255, 'tlsr8359_hsel')).toBe('未连接')
    for (let bit = 0; bit < 6; bit++) expect(() => encodePinBlock({ ...config(), activeLow: 1 << bit })).toThrow()
  })
  it.each(['da14585', 'da14585_asel29', 'da14585_asel42', 'da14585_aesl', 'aesl29_da', 'aesl42_da'])('labels DA nibble-encoded GPIOs without changing the wire number for %s', family => {
    expect(gpioLabel(32, family)).toBe('P2.0 (32)')
    expect(gpioLabel(33, family)).toBe('P2.1 (33)')
    expect(gpioLabel(7, family)).toBe('P0.7 (7)')
    expect(gpioLabel(16, family)).toBe('P1.0 (16)')
    expect(gpioLabel(255, family)).toBe('未连接')
    expect(gpioLabel(64, family)).toBe('GPIO 64')
    expect(gpioLabel(1.5, family)).toBe('GPIO 1.5')
    expect(gpioLabel(32, 'nrf52832_priceetag')).toBe('GPIO 32')
    expect(gpioLabel(32, 'om6626_zkcxxb')).toBe('GPIO 32')
  })
  it('uses exact registered screen recipes, never a family guess', () => {
    const screen: ScreenConfig = { schemaVersion: 1, driver: 0x24, recipe: 0, color: 1, transform: 0, nativeUiScale: 1, width: 400, height: 300, visibleX: 0, visibleY: 0, visibleWidth: 400, visibleHeight: 300 }
    expect(screenBusyActiveHigh(screen)).toBe(true)
    expect(screenBusyActiveHigh({ ...screen, recipe: 255 })).toBeNull()
    expect(screenBusyActiveHigh({ ...screen, width: 0 })).toBeNull()
    expect(screenBusyActiveHigh(null)).toBeNull()
  })
  it('matches the common C/Python vector', () => {
    expect(Array.from(encodePinBlock(config())).map(v => v.toString(16).padStart(2, '0')).join('')).toBe('0100c00300010203040506070809ffff')
    expect(decodePinBlock(encodePinBlock(config()))).toEqual(config())
    expect(decodePinReport(report()).effective).toEqual(config())
    expect(Array.from(pinSetRequest(null))).toEqual([2, 0])
    expect(pinSetRequest(config()).length).toBe(18)
  })
  it.each([0, 15, 17, 34, 35, 37])('rejects unknown lengths %s', length => {
    expect(() => decodePinReport(new Uint8Array(length))).toThrow()
  })
  it.each([0, 1, 3])('rejects bad header/flag/role-mask byte %s', index => {
    const data = report(); data[index] = 255
    expect(() => decodePinReport(data)).toThrow()
  })
  it('accepts all defined low-byte editable role combinations', () => {
    for (let mask = 0; mask < 256; mask++) { const data = report(); data[2] = mask; expect(decodePinReport(data).editableRoles).toBe(0x300 | mask) }
  })
  it('rejects duplicate pins, partial display, invalid polarity and unavailable roles', () => {
    const duplicate = config(); duplicate.pins.red = 0
    expect(() => encodePinBlock(duplicate)).toThrow(/重复/)
    const partial = config(); partial.pins.mosi = 255
    expect(() => encodePinBlock(partial)).toThrow(/完整/)
    const polarity = config(); polarity.activeLow |= 1
    expect(() => encodePinBlock(polarity)).toThrow(/掩码/)
    const txd = config(); txd.pins.txd = 30
    expect(() => validatePinUpdate(txd, decodePinReport(report(0x3ff)))).toThrow()
  })
})
