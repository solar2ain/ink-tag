import { describe, expect, it } from 'vitest'
import { parseThemeMode, resolveTheme } from './theme'

describe('theme preferences', () => {
  it('accepts only the three stable theme modes', () => {
    expect(parseThemeMode('light')).toBe('light')
    expect(parseThemeMode('dark')).toBe('dark')
    expect(parseThemeMode('unknown')).toBe('system')
    expect(parseThemeMode(null)).toBe('system')
  })

  it('resolves system mode without changing explicit choices', () => {
    expect(resolveTheme('system', true)).toBe('dark')
    expect(resolveTheme('system', false)).toBe('light')
    expect(resolveTheme('light', true)).toBe('light')
    expect(resolveTheme('dark', false)).toBe('dark')
  })
})
