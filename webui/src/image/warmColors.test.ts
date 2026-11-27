import { describe, expect, it } from 'vitest'
import { remapCoolColors } from './warmColors'

describe('warm colors', () => {
  it.each([2, 3])('maps saturated blue and green on palette %i while preserving other pixels', color => {
    const unchanged = [216, 153, 118, 255, 133, 138, 137, 255, 20, 90, 184, 0, 255, 255, 255, 255]
    const pixels = new Uint8ClampedArray([20, 90, 184, 255, 35, 173, 85, 255, ...unchanged])
    remapCoolColors(pixels, color)
    expect([...pixels]).toEqual([
      184, color === 3 ? 55 : 20, 20, 255, 173, color === 3 ? 173 : 35, 35, 255, ...unchanged,
    ])
  })

  it.each([1, 4, 10])('leaves palette %i alone', color => {
    const pixels = new Uint8ClampedArray([25, 94, 193, 255])
    remapCoolColors(pixels, color)
    expect([...pixels]).toEqual([25, 94, 193, 255])
  })
})
