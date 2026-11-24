import { describe, expect, it, vi } from 'vitest'
import { DevicePixelGfx } from './devicePixelGfx'
import { calendarTextRole, drawNativeScenePart, type NativeTextCommand } from './nativeScene'
import { createNativeReference, nativeReferencePresets, nativeReferenceScene } from './nativeReference'

describe('calendar local typography', () => {
  it('changes only the requested category while preserving the independent cell centers', () => {
    const document = createNativeReference(nativeReferencePresets[0], 400, 300, 3)
    const part = nativeReferenceScene(document).parts.find(part => part.element.type === 'calendar')!
    const runs = part.commands.filter((command): command is NativeTextCommand => command.type === 'text')
    const gfx = new DevicePixelGfx(400, 300)
    const actual: { text: string; x: number; y: number; font: string; width: number }[] = []
    vi.spyOn(gfx, 'print').mockImplementation(text => {
      actual.push({ text: String(text), x: gfx.tx, y: gfx.ty, font: gfx.currentFontId, width: gfx.getUTF8Width(String(text)) })
    })
    drawNativeScenePart(gfx, part, part.element.source, { ...part.element, calendarFonts: { day: 'wqy9-lunar' } })
    expect(actual).toHaveLength(runs.length)
    actual.forEach((item, index) => {
      const original = runs[index]
      if (calendarTextRole(item.text) === 'day') {
        expect(item.font).toBe('wqy9-lunar')
        gfx.setFont(original.font); gfx.setFontScale(original.scale)
        expect(Math.abs(item.x + item.width / 2 - original.x - gfx.getUTF8Width(original.text) / 2)).toBeLessThanOrEqual(.5)
      } else expect(item).toMatchObject({ x: original.x, y: original.y, font: original.font })
    })
  })
  it('keeps an explicit category font when the control font changes, and serializes the override', () => {
    const document = createNativeReference(nativeReferencePresets[0], 400, 300, 3)
    const part = nativeReferenceScene(document).parts.find(part => part.element.type === 'calendar')!
    const element = JSON.parse(JSON.stringify({ ...part.element, fontOverride: 'wqy12-lunar', calendarFonts: { day: 'helv-bold-14-num' } }))
    const gfx = new DevicePixelGfx(400, 300)
    const fonts: string[] = []
    vi.spyOn(gfx, 'print').mockImplementation(text => {
      fonts.push(gfx.currentFontId)
      expect(gfx.currentFontId).toBe(calendarTextRole(String(text)) === 'day' ? 'helv-bold-14-num' : element.fontOverride)
    })
    for (const font of ['wqy12-lunar', 'wqy9-lunar']) {
      element.fontOverride = font
      drawNativeScenePart(gfx, part, part.element.source, element)
    }
    expect(fonts.length).toBeGreaterThan(50)
  })
})
