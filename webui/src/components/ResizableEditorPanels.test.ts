import { describe, expect, it } from 'vitest'
import { fitPanelWidths } from './ResizableEditorPanels'

describe('editor panel widths', () => {
  it('retains preferred widths when the canvas has enough room', () => {
    expect(fitPanelWidths(1200, { left: 260, right: 320 })).toEqual({ left: 260, right: 320 })
  })
  it('shrinks both panels proportionally without squeezing out the canvas', () => {
    const widths = fitPanelWidths(720, { left: 360, right: 420 })
    expect(widths.left).toBeGreaterThanOrEqual(144)
    expect(widths.right).toBeGreaterThanOrEqual(216)
    expect(720 - widths.left - widths.right - 12).toBeGreaterThanOrEqual(263)
  })
  it('bounds persisted extremes and never changes the saved preference during fitting', () => {
    expect(fitPanelWidths(1600, { left: -200, right: 900 })).toEqual({ left: 144, right: 420 })
    const preferred = { left: 260, right: 320 }
    fitPanelWidths(700, preferred)
    expect(fitPanelWidths(1400, preferred)).toEqual(preferred)
  })
})
