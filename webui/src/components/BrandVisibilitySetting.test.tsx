// @vitest-environment jsdom
import { act } from 'react'
import { createRoot } from 'react-dom/client'
import { renderToStaticMarkup } from 'react-dom/server'
import { describe, expect, it, vi } from 'vitest'
import { BrandVisibilitySetting } from './BrandVisibilitySetting'

describe('Pro brand display setting', () => {
  it('uses one stable label and the shared display-setting style', () => {
    const html = renderToStaticMarkup(<BrandVisibilitySetting visible saving={false} disabled={false} onChange={() => {}} />)
    expect(html).toContain('class="display-setting"')
    expect(html).toContain('class="display-setting-head"')
    expect(html.match(/显示 InkTag Pro 标识/g)).toHaveLength(1)
    expect(html).toContain('关闭后不叠加全局标识，图片不变。')
    expect(html).toContain('aria-describedby=')
    expect(html).toContain('checked=""')
    expect(html).not.toMatch(/<strong|<svg|class="notice"|&#x20;/)
  })

  it.each([true, false])('sends only the selected visibility value from %s', async (visible) => {
    const container = document.createElement('div')
    const root = createRoot(container)
    const change = vi.fn()
    try {
      await act(async () => root.render(<BrandVisibilitySetting visible={visible} saving={false} disabled={false} onChange={change} />))
      const checkbox = container.querySelector<HTMLInputElement>('input')!
      expect(checkbox.checked).toBe(visible)
      await act(async () => checkbox.click())
      expect(change).toHaveBeenCalledExactlyOnceWith(!visible)
    } finally {
      await act(async () => root.unmount())
    }
  })

  it.each([{ saving: true, disabled: false }, { saving: false, disabled: true }])('blocks edits while saving or disconnected: %j', async (state) => {
    const container = document.createElement('div')
    const root = createRoot(container)
    const change = vi.fn()
    try {
      await act(async () => root.render(<BrandVisibilitySetting visible {...state} onChange={change} />))
      const checkbox = container.querySelector<HTMLInputElement>('input')!
      expect(checkbox.disabled).toBe(true)
      await act(async () => checkbox.click())
      expect(change).not.toHaveBeenCalled()
      expect(container.querySelector('[role="status"]')?.textContent).toBe(state.saving ? '正在保存…' : undefined)
      expect(container.querySelector('label')?.textContent).toBe('显示 InkTag Pro 标识')
    } finally {
      await act(async () => root.unmount())
    }
  })
})
