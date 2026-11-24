import { describe, expect, it } from 'vitest'
import { canRunManualImageOperation } from './manualOperation'

describe('manual image operations during slideshow', () => {
  it('allows idle and slideshow states but rejects active transfers', () => {
    expect(canRunManualImageOperation('idle')).toBe(true)
    expect(canRunManualImageOperation('slideshow')).toBe(true)
    expect(canRunManualImageOperation('picture')).toBe(false)
    expect(canRunManualImageOperation('ota')).toBe(false)
  })

})
