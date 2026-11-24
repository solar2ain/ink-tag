import { describe, expect, it } from 'vitest'

import {
  classifyLayout,
  layoutDefaultScaleQ8,
  layoutScaleLabel,
  layoutScreensV0,
  layoutScreensV1,
} from './layoutPreviewModel'

describe('base default layout breakpoints', () => {
  it('classifies every supported design resolution into the intended family', () => {
    expect(layoutScreensV1.map((screen) => [screen.width, screen.height, classifyLayout(screen.width, screen.height)])).toEqual([
      [152, 152, 'compact-square'],
      [200, 200, 'compact-square'],
      [240, 240, 'compact-square'],
      [212, 104, 'compact-rect'],
      [250, 128, 'compact-rect'],
      [296, 152, 'compact-rect'],
      [400, 300, 'normal-rect'],
      [600, 448, 'large-rect'],
      [752, 568, 'large-rect'],
      [800, 480, 'large-rect'],
    ])
  })

  it('keeps the documented long-side boundaries stable', () => {
    expect(classifyLayout(299, 140)).toBe('compact-rect')
    expect(classifyLayout(300, 200)).toBe('normal-rect')
    expect(classifyLayout(499, 300)).toBe('normal-rect')
    expect(classifyLayout(500, 300)).toBe('normal-rect')
    expect(classifyLayout(501, 300)).toBe('large-rect')
  })

  it('keeps the default-font gate at 100% for compact/normal and 150% for large', () => {
    expect(layoutScreensV1.map((screen) => [screen.id, layoutScaleLabel(screen.fontScaleQ8)])).toEqual([
      ['square-152', '100%'],
      ['square-200', '100%'],
      ['square-240', '100%'],
      ['rect-212', '100%'],
      ['rect-250', '100%'],
      ['rect-296', '100%'],
      ['normal-400', '100%'],
      ['large-600', '150%'],
      ['large-752', '150%'],
      ['large-800', '150%'],
    ])
  })

  it('lets v1 enlarge large-screen defaults without mutating the frozen v0 baseline', () => {
    expect(layoutScreensV1.map((screen) => [
      screen.id,
      layoutScaleLabel(layoutDefaultScaleQ8(screen, 'v0')),
      layoutScaleLabel(layoutDefaultScaleQ8(screen, 'v1')),
    ])).toEqual([
      ['square-152', '100%', '100%'],
      ['square-200', '100%', '100%'],
      ['square-240', '100%', '100%'],
      ['rect-212', '100%', '100%'],
      ['rect-250', '100%', '100%'],
      ['rect-296', '100%', '100%'],
      ['normal-400', '100%', '100%'],
      ['large-600', '150%', '125%'],
      ['large-752', '150%', '150%'],
      ['large-800', '150%', '150%'],
    ])
  })

  it('keeps 154×154 only in the frozen v0 inventory', () => {
    expect(layoutScreensV0[0]).toMatchObject({ id: 'square-154', width: 154, height: 154 })
    expect(layoutScreensV1[0]).toMatchObject({ id: 'square-152', width: 152, height: 152 })
  })
})
