import { describe, expect, it } from 'vitest'
import { isMobileDevice, MOBILE_LAYOUT_QUERY, shouldUseMobileLayout } from './layout'

const hints = (patch: Partial<Parameters<typeof isMobileDevice>[0]> = {}) => ({
  userAgent: 'Mozilla/5.0 (Windows NT 10.0; Win64; x64)',
  platform: 'Win32',
  maxTouchPoints: 0,
  ...patch,
})

describe('responsive layout mode', () => {
  it('uses 800px as the desktop/mobile boundary', () => {
    expect(MOBILE_LAYOUT_QUERY).toBe('(max-width: 799px)')
    expect(shouldUseMobileLayout(hints(), true)).toBe(true)
    expect(shouldUseMobileLayout(hints(), false)).toBe(false)
  })

  it('uses explicit userAgentData mobile hints when available', () => {
    expect(isMobileDevice(hints({ userAgentData: { mobile: true } }))).toBe(true)
    expect(isMobileDevice(hints({ userAgent: 'Android Mobile', userAgentData: { mobile: false } }))).toBe(false)
  })

  it('recognizes phone and touch iPad fallback user agents', () => {
    expect(isMobileDevice(hints({ userAgent: 'Mozilla/5.0 (iPhone; CPU iPhone OS 18_0 like Mac OS X)' }))).toBe(true)
    expect(isMobileDevice(hints({ platform: 'MacIntel', maxTouchPoints: 5 }))).toBe(true)
    expect(isMobileDevice(hints())).toBe(false)
  })
})
