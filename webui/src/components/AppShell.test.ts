import { describe, expect, it } from 'vitest'
import { visibleNavigation } from './AppShell'

describe('main navigation order', () => {
  it('places Find immediately before Settings and keeps preferences inside Settings', () => {
    const labels = visibleNavigation().map((item) => item.label)
    expect(labels.indexOf('查找')).toBe(labels.indexOf('设置') - 1)
    expect(labels).not.toContain('偏好设置')
  })

  it('keeps Find visible and marks it as Pro', () => {
    const find = visibleNavigation().find((item) => item.label === '查找')
    expect(find?.pro).toBe(true)
  })
})
