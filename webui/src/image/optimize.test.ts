import { describe, expect, it } from 'vitest'
import { algorithmGroups, calibratedPalette, directPreviewFrame, mixDitherStrength, restoreUncoveredPixels, usesDirectStrength } from './optimize'
import { legacyDitherFrame } from './dither'
import { paletteForColor } from './processing'
import { imageDefaults } from './defaults'
import { imageEditorDraft } from './editorDraft'

describe('calibrated image pipeline', () => {
  const frame = { width: 3, height: 2, data: new Uint8ClampedArray([80, 150, 230, 255, 180, 80, 80, 255, 140, 140, 140, 255, 230, 130, 10, 255, 0, 0, 0, 0, 30, 70, 100, 255]) }
  const defaults = { algorithm: 'auto', preset: 'balanced', contrast: 1, strength: 1, heavyInk: false, color: 10 } as const
  it('routes Hybrid through the independent core for all existing palettes', () => {
    for (const color of [1, 2, 3, 4, 10]) {
      expect(directPreviewFrame(frame, { ...defaults, color })).toEqual(legacyDitherFrame(frame, paletteForColor(color), 'legacyHybrid'))
    }
    expect(directPreviewFrame(frame, { ...defaults, algorithm: 'floydSteinberg' })).toEqual(legacyDitherFrame(frame, paletteForColor(10), 'floydSteinberg'))
    expect(directPreviewFrame(frame, { ...defaults, algorithm: 'sierra3' })).toBeNull()
  })
  it('uses the visually reviewed photo defaults without reducing the algorithm choices', () => {
    expect(imageDefaults).toEqual({ contrast: 1.1, strength: .8 })
    expect(imageEditorDraft).toMatchObject({ ...imageDefaults, algorithm: 'auto', heavyInk: false })
    const ids = algorithmGroups.flatMap(group => group.options.map(([id]) => id))
    expect(ids).toHaveLength(16)
    expect(new Set(ids).size).toBe(16)
  })
  it('keeps perceived preview colors separate from native transfer colors', () => {
    const gray = calibratedPalette(4)
    expect(gray.map((entry) => entry.color)).toEqual(['#000000', '#828282', '#cdcdcd', '#ffffff'])
    expect(gray.map((entry) => entry.deviceColor)).toEqual(['#000000', '#555555', '#aaaaaa', '#ffffff'])
    const bwr = calibratedPalette(2)
    expect(bwr.find((entry) => entry.name === '红')).toEqual({ name: '红', color: '#b43232', deviceColor: '#dc3026' })
  })

  it('keeps every legacy selector represented while grouping advanced choices', () => {
    const ids = algorithmGroups.flatMap((group) => group.options.map(([id]) => id))
    for (const id of ['auto', 'floydSteinberg', 'atkinson', 'stucki', 'jarvis', 'bayer', 'blueNoise', 'riemersma', 'sierra2-4a', 'burkes', 'sierra2', 'legacyOstromoukhov', 'linearLightSierra']) {
      expect(ids).toContain(id)
    }
    expect(ids).not.toContain('legacyHybrid')
    expect(ids).not.toContain('e6Perceptual')
  })

  it('mixes quantized and fully dithered output at a stable strength', () => {
    const dithered = new Uint8ClampedArray(64 * 4).fill(255)
    const quantized = new Uint8ClampedArray(64 * 4)
    expect([...mixDitherStrength(dithered.slice(), quantized, 8, 0)].every((value) => value === 0)).toBe(true)
    expect([...mixDitherStrength(dithered.slice(), quantized, 8, 1)].every((value) => value === 255)).toBe(true)
    const half = mixDitherStrength(dithered.slice(), quantized, 8, .5)
    expect([...half].filter((value) => value === 255)).toHaveLength(32 * 4)
  })

  it('restores only pixels outside the image coverage mask', () => {
    const coverage = new Uint8ClampedArray([
      0, 0, 0, 0,
      12, 20, 30, 255,
      0, 0, 0, 0,
    ])
    const output = new Uint8ClampedArray([
      0, 0, 0, 255,
      30, 40, 50, 255,
      180, 50, 50, 255,
    ])
    restoreUncoveredPixels(coverage, output, [220, 215, 205])
    expect([...output]).toEqual([
      220, 215, 205, 255,
      30, 40, 50, 255,
      220, 215, 205, 255,
    ])
  })

  it('uses direct 0-5 strength for the old and common diffusion algorithms', () => {
    for (const algorithm of ['auto', 'floydSteinberg', 'atkinson', 'bayer', 'blueNoise', 'stucki', 'legacyHybrid'] as const) {
      expect(usesDirectStrength(algorithm)).toBe(true)
    }
    expect(usesDirectStrength('sierra3')).toBe(false)
    expect(usesDirectStrength('none')).toBe(false)
  })
})
