import { describe, expect, it } from 'vitest'
import { calibratedLegacyStrength, legacyAdjustFrame, legacyDitherFrame, previewFrameToNative, type LegacyImageAlgorithm } from './dither'
import { paletteForColor } from './processing'

const frame = {
  width: 5,
  height: 4,
  data: new Uint8ClampedArray(Array.from({ length: 20 }, (_, index) => [
    index * 13 % 256, index * 29 % 256, index * 47 % 256, 255,
  ]).flat()),
}

describe('independent InkTag image algorithms', () => {
  it.each<LegacyImageAlgorithm>(['legacyHybrid', 'legacyOstromoukhov', 'linearLightSierra', 'floydSteinberg', 'atkinson', 'jarvis', 'stucki', 'burkes', 'sierra2', 'sierra2-4a', 'bayer', 'blueNoise', 'riemersma', 'none'])('%s is deterministic and palette bounded', (algorithm) => {
    const palette = paletteForColor(3)
    const first = legacyDitherFrame(frame, palette, algorithm, 1)
    const second = legacyDitherFrame(frame, palette, algorithm, 1)
    expect(first.data).toEqual(second.data)
    for (let index = 0; index < first.data.length; index += 4) {
      expect(palette.some((entry) => entry.perceived.every((value, channel) => value === first.data[index + channel]))).toBe(true)
    }
  })

  it('uses the old 0-5 strength semantics for direct error diffusion', () => {
    const palette = paletteForColor(3)
    const off = legacyDitherFrame(frame, palette, 'floydSteinberg', 0)
    const normal = legacyDitherFrame(frame, palette, 'floydSteinberg', 1)
    const strong = legacyDitherFrame(frame, palette, 'floydSteinberg', 4)
    expect(normal.data).not.toEqual(off.data)
    expect(strong.data).not.toEqual(normal.data)
  })

  it('uses the Hybrid return buffer and keeps high strength bounded', () => {
    const palette = paletteForColor(2)
    const normal = legacyDitherFrame(frame, palette, 'legacyHybrid', 1)
    const strong = legacyDitherFrame(frame, palette, 'legacyHybrid', 5)
    const blackPixels = Array.from({ length: frame.width * frame.height }, (_, index) => index * 4)
      .filter((offset) => strong.data[offset] === palette[0].perceived[0]
        && strong.data[offset + 1] === palette[0].perceived[1]
        && strong.data[offset + 2] === palette[0].perceived[2]).length
    expect(strong.data).not.toEqual(normal.data)
    expect(blackPixels).toBeLessThan(frame.width * frame.height)
  })

  it('gives Hybrid a stable 0-5 UI scale without overdriving diffusion', () => {
    expect(calibratedLegacyStrength('legacyHybrid', 0)).toBe(0)
    expect(calibratedLegacyStrength('legacyHybrid', 1)).toBe(1)
    expect(calibratedLegacyStrength('legacyHybrid', 2)).toBeGreaterThan(1)
    expect(calibratedLegacyStrength('legacyHybrid', 5)).toBeLessThan(1.65)
    expect(calibratedLegacyStrength('floydSteinberg', 5)).toBe(5)

    const width = 64
    const height = 48
    const gradient = {
      width,
      height,
      data: new Uint8ClampedArray(Array.from({ length: width * height }, (_, index) => {
        const x = index % width
        const y = Math.floor(index / width)
        return [x / (width - 1) * 255, y / (height - 1) * 255, (x + y) / (width + height - 2) * 255, 255]
      }).flat()),
    }
    const palette = paletteForColor(2)
    for (const strength of [1, 2, 5]) {
      const result = legacyDitherFrame(gradient, palette, 'legacyHybrid', strength)
      const black = Array.from({ length: width * height }, (_, index) => index * 4)
        .filter((offset) => palette[0].perceived.every((value, channel) => result.data[offset + channel] === value)).length
      expect(black / (width * height)).toBeLessThan(.85)
      expect(black).toBeGreaterThan(0)
    }
  })

  it('converts perceived preview colors back to immutable device colors', () => {
    const palette = paletteForColor(4)
    const preview = legacyDitherFrame(frame, palette, 'legacyOstromoukhov')
    const native = previewFrameToNative(preview, palette)
    for (let index = 0; index < native.data.length; index += 4) {
      expect(palette.some((entry) => entry.rgb.every((value, channel) => value === native.data[index + channel]))).toBe(true)
    }
  })
})


it('adjusts RGB brightness after contrast without changing alpha or the source', () => {
  const source = { width: 2, height: 1, data: new Uint8ClampedArray([0, 100, 240, 80, 255, 128, 20, 255]) }
  expect(legacyAdjustFrame(source, 1).data).toEqual(source.data)
  expect([...legacyAdjustFrame(source, 1, 20).data]).toEqual([51, 151, 255, 80, 255, 179, 71, 255])
  expect([...legacyAdjustFrame(source, 1, -20).data]).toEqual([0, 49, 189, 80, 204, 77, 0, 255])
  expect([...source.data]).toEqual([0, 100, 240, 80, 255, 128, 20, 255])
  for (const value of [-101, 101, NaN, Infinity]) expect(() => legacyAdjustFrame(source, 1, value)).toThrow('brightness')
})
