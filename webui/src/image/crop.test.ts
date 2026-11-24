import { describe, expect, it } from 'vitest'
import { clampImageScale, imageCoverScale, imageDrawSize, zoomPanAtPoint } from './crop'

describe('image crop geometry', () => {
  it('allows zooming out below the cover size while keeping a bounded range', () => {
    expect(clampImageScale(.1)).toBe(.25)
    expect(clampImageScale(.75)).toBe(.75)
    expect(clampImageScale(20)).toBe(6)
  })

  it('accounts for quarter-turn rotations when calculating cover scale', () => {
    expect(imageCoverScale(800, 400, 400, 300, 0)).toBe(.75)
    expect(imageCoverScale(800, 400, 400, 300, 90)).toBe(1)
  })

  it('keeps the image point beneath the pointer fixed while zooming', () => {
    expect(zoomPanAtPoint({ x: 0, y: 0 }, 1, 2, { x: 80, y: -30 })).toEqual({ x: -80, y: 30 })
    expect(zoomPanAtPoint({ x: 12, y: 8 }, 2, 1, { x: 0, y: 0 })).toEqual({ x: 6, y: 4 })
  })

  it('stretches exactly to the target after quarter-turn rotation', () => {
    expect(imageDrawSize(800, 400, 400, 300, 0, 'stretch', 1)).toEqual({ width: 400, height: 300 })
    expect(imageDrawSize(800, 400, 400, 300, 90, 'stretch', 1)).toEqual({ width: 300, height: 400 })
  })

  it('keeps crop mode proportional to the source', () => {
    expect(imageDrawSize(800, 400, 400, 300, 0, 'crop', 1)).toEqual({ width: 600, height: 300 })
  })
})
