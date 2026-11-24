import { describe, expect, it } from 'vitest'
import { testPatternPalette } from './LabPage'

describe('screen test palettes', () => {
  it('uses every native six-color entry for the E6 palette and comprehensive pages', () => {
    expect(testPatternPalette(10)).toEqual([
      'rgb(0,0,0)',
      'rgb(255,255,255)',
      'rgb(220,48,38)',
      'rgb(235,194,25)',
      'rgb(19,116,99)',
      'rgb(44,97,165)',
    ])
  })
})
