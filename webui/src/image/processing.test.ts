import { describe, expect, it } from 'vitest'
import { ditherFrame, paletteForColor } from './processing'

describe('e-paper image processing', () => {
  it('defines every published screen color palette', () => {
    for (let color = 1; color <= 11; color += 1) expect(paletteForColor(color).length).toBeGreaterThanOrEqual(2)
    expect(paletteForColor(4)).toHaveLength(4)
    expect(paletteForColor(9)).toHaveLength(16)
    expect(paletteForColor(10).map(({ name }) => name)).toEqual(['黑', '白', '红', '黄', '绿', '蓝'])
    expect(paletteForColor(11)).toHaveLength(7)
  })

  it('keeps all six E6 pigments available to quantization', () => {
    const palette = paletteForColor(10)
    const data = new Uint8ClampedArray(palette.flatMap(({ rgb }) => [...rgb, 255]))
    const result = ditherFrame(
      { width: palette.length, height: 1, data },
      { algorithm: 'none', strength: 1, contrast: 1, heavyInk: false, color: 10 },
    )
    expect([...result.data]).toEqual([...data])
  })

  it('quantizes to the selected panel palette', () => {
    const frame = { width: 2, height: 1, data: Uint8ClampedArray.from([10, 10, 10, 255, 230, 40, 30, 255]) }
    const result = ditherFrame(frame, { algorithm: 'none', strength: 1, contrast: 1, heavyInk: false, color: 2 })
    expect([...result.data]).toEqual([0, 0, 0, 255, 220, 48, 38, 255])
  })

  it('keeps output dimensions and opacity for every algorithm', () => {
    const frame = { width: 3, height: 2, data: new Uint8ClampedArray(3 * 2 * 4).fill(127) }
    for (const algorithm of ['none', 'floyd-steinberg', 'atkinson', 'sierra-lite', 'bayer'] as const) {
      const result = ditherFrame(frame, { algorithm, strength: 1, contrast: 1.1, heavyInk: true, color: 3 })
      expect(result.width).toBe(3)
      expect(result.height).toBe(2)
      expect([...result.data].filter((_, index) => index % 4 === 3)).toEqual([255, 255, 255, 255, 255, 255])
    }
  })
})
