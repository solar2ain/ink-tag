import { describe, expect, it } from 'vitest'
import { adjustContrast, ditherImage } from './inktag_dither.js'

const modes = ['blackWhiteColor', 'threeColor', 'fourColor', 'gray4Color', 'sixColor']
const algorithms = ['none', 'floydSteinberg', 'atkinson', 'stucki', 'jarvis', 'burkes',
  'twoRowSierra', 'sierraLite', 'bayer', 'blueNoise', 'riemersma', 'hybrid',
  'adaptiveThreePoint', 'linearLightSierra']
const frame = (width = 17, height = 9, alpha = 255) => ({ width, height,
  data: new Uint8ClampedArray(Array.from({ length: width * height }, (_, i) =>
    [(i * 29) % 256, (i * 47) % 256, (i * 73) % 256, alpha]).flat()) })

describe('independent image core properties', () => {
  it.each(modes)('%s has no mutable per-request state or input mutation', mode => {
    const image = frame(), before = new Uint8ClampedArray(image.data)
    for (const algorithm of algorithms) {
      const a = ditherImage(image, algorithm, .6, mode)
      ditherImage(frame(5, 3), 'hybrid', 4, 'sixColor', { heavyInk: true })
      const b = ditherImage(image, algorithm, .6, mode)
      expect(b.data).toEqual(a.data)
      expect(image.data).toEqual(before)
      for (let i = 3; i < a.data.length; i += 4) expect(a.data[i]).toBe(255)
    }
  })

  it.each(algorithms)('%s leaves neutral text edges achromatic', algorithm => {
    const image = frame()
    for (let i = 0; i < image.data.length; i += 4) image.data[i + 1] = image.data[i + 2] = image.data[i]
    const result = ditherImage(image, algorithm, .6, 'sixColor')
    for (let i = 0; i < result.data.length; i += 4) {
      expect(result.data[i]).toBe(result.data[i + 1])
      expect(result.data[i]).toBe(result.data[i + 2])
    }
  })

  it.each(algorithms)('%s keeps uncovered pixels white even at maximum strength', algorithm => {
    for (const mode of modes) {
      const result = ditherImage(frame(11, 7, 0), algorithm, 5, mode, { heavyInk: true })
      expect([...result.data].every(value => value === 255)).toBe(true)
    }
  })

  it.each(modes)('%s strength zero is exact palette quantization for every option', mode => {
    const image = frame()
    const expected = ditherImage(image, 'none', 1, mode)
    for (const algorithm of algorithms) expect(ditherImage(image, algorithm, 0, mode).data).toEqual(expected.data)
  })

  it.each([[1, 1], [1, 257], [257, 1], [3, 37], [37, 3], [17, 9]])(
    'Hilbert traversal visits every pixel of a %sx%s rectangle', (width, height) => {
      const result = ditherImage(frame(width, height), 'riemersma', 1, 'fourColor')
      for (let i = 3; i < result.data.length; i += 4) expect(result.data[i]).toBe(255)
    },
  )

  it('keeps pure paper/ink colours stable and preserves a gray ramp average', () => {
    const gradient = { width: 256, height: 24, data: new Uint8ClampedArray(
      Array.from({ length: 256 * 24 }, (_, i) => [i % 256, i % 256, i % 256, 255]).flat()) }
    for (const algorithm of ['floydSteinberg', 'jarvis', 'stucki', 'sierraLite', 'hybrid']) {
      const result = ditherImage(gradient, algorithm, 1, 'blackWhiteColor')
      let total = 0
      for (let i = 0; i < result.data.length; i += 4) total += result.data[i]
      expect(Math.abs(total / (256 * 24) - 127.5)).toBeLessThan(5)
    }
  })

  it('rejects malformed frames/parameters before processing, without silently choosing an algorithm', () => {
    expect(() => ditherImage({ width: 1, height: 2, data: new Uint8ClampedArray(4) }, 'none', 1, 'fourColor')).toThrow()
    expect(() => ditherImage(frame(), 'unknown', 1, 'fourColor')).toThrow()
    expect(() => ditherImage(frame(), 'none', NaN, 'fourColor')).toThrow()
    expect(() => ditherImage(frame(), 'none', 1, 'unknown')).toThrow()
    expect(() => adjustContrast(frame(), Infinity)).toThrow()
  })
})
