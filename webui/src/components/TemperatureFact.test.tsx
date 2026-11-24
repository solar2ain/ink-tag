// @vitest-environment jsdom
import { act } from 'react'
import { createRoot } from 'react-dom/client'
import { renderToStaticMarkup } from 'react-dom/server'
import { describe, expect, it } from 'vitest'
import { TemperatureFact } from './TemperatureFact'

describe('combined temperature fact', () => {
  it('explains an absent sample and main-only firmware without inventing a panel source', () => {
    const props = { connected: true, mcuTemperature: null, panelTemperature: null, boardId: 'aesl29_da' }
    const empty = renderToStaticMarkup(<TemperatureFact {...props} temperature={null} />)
    expect(empty).toContain('尚无有效温度样本')
    expect(empty).not.toContain('0 °C')
    const valid = renderToStaticMarkup(<TemperatureFact {...props} temperature={23} />)
    expect(valid).toContain('设备仅返回主温度，未提供独立来源读数')
    expect(valid).toContain('屏幕 -- / MCU 不支持')
    expect(valid).toContain('23 °C')
  })
  it('describes returned validity without promising a refresh-only or universal connection sample', () => {
    const html = renderToStaticMarkup(<TemperatureFact connected temperature={23} mcuTemperature={25} panelTemperature={23} boardId="nrf52832_priceetag" />)
    expect(html).toContain('屏幕 23 °C / MCU 25 °C')
    expect(html).toContain('连接或刷新时是否采样，取决于当前固件和屏幕支持')
    expect(html).not.toContain('屏温在正常刷新时采样')
    expect(html).not.toContain('连接时一定')
  })
  it('reveals sources only at the help button and supports keyboard and touch', async () => {
    const container = document.createElement('div')
    document.body.append(container)
    const root = createRoot(container)
    try {
      await act(async () => root.render(<TemperatureFact connected temperature={22} mcuTemperature={26.5} panelTemperature={22.25} />))
      const fact = container.querySelector('.temperature-fact')!
      const tooltip = container.querySelector<HTMLElement>('[role="tooltip"]')!
      const reading = container.querySelector<HTMLElement>('[aria-label="主温度"]')!
      const button = container.querySelector<HTMLButtonElement>('button')!
      expect(tooltip.hidden).toBe(true)
      await act(async () => fact.dispatchEvent(new MouseEvent('mouseover', { bubbles: true })))
      expect(tooltip.hidden).toBe(true)
      await act(async () => reading.dispatchEvent(new MouseEvent('mouseover', { bubbles: true })))
      expect(tooltip.hidden).toBe(true)
      expect(reading.hasAttribute('tabindex')).toBe(false)
      expect(tooltip.parentElement).toBe(button.parentElement)
      await act(async () => button.dispatchEvent(new MouseEvent('mouseover', { bubbles: true })))
      expect(tooltip.hidden).toBe(false)
      await act(async () => button.dispatchEvent(new MouseEvent('mouseout', { bubbles: true, relatedTarget: document.body })))
      expect(tooltip.hidden).toBe(true)
      await act(async () => button.focus())
      expect(tooltip.hidden).toBe(false)
      await act(async () => button.blur())
      expect(tooltip.hidden).toBe(true)
      await act(async () => button.click())
      expect(tooltip.hidden).toBe(false)
      await act(async () => button.click())
      expect(tooltip.hidden).toBe(true)
    } finally {
      await act(async () => root.unmount())
      container.remove()
    }
  })
  it('shows only the authoritative reading inline and keeps sources in the hidden tooltip', () => {
    const html = renderToStaticMarkup(<TemperatureFact connected temperature={22} mcuTemperature={26.5} panelTemperature={22.25} />)
    const inline = html.match(/<dd[^>]*>(.*?)<\/dd>/)![1]
    expect(inline).toContain('22 °C')
    expect(inline).not.toContain('MCU')
    expect(inline).not.toContain('屏幕')
    expect(html).toContain('屏幕 22.25 °C / MCU 26.5 °C')
    expect(html).toContain('role="tooltip" hidden=""')
    expect(html).toContain('aria-describedby=')
    expect(inline).not.toContain('tabindex=')
    expect(inline).not.toContain('aria-describedby=')
    expect(html).toContain('aria-label="了解温度来源"')
    expect(html).toContain('aria-expanded="false"')
    expect(html).toContain('hidden=""')
  })

  it('keeps unsupported sources unavailable, including stale readings after disconnect', () => {
    const props = { temperature: -2, mcuTemperature: -2.5, panelTemperature: null }
    const online = renderToStaticMarkup(<TemperatureFact connected {...props} />)
    expect(online).toContain('屏幕 -- / MCU -2.5 °C')
    const offline = renderToStaticMarkup(<TemperatureFact connected={false} {...props} />)
    expect(offline).not.toContain('°C')
    expect(offline).not.toContain('未连接')
    expect(offline).toContain('屏幕 -- / MCU --')
    expect(offline.match(/<dd[^>]*>(.*?)<\/dd>/)![1]).toMatch(/>--<\/span>/)
  })

  it('labels known unsupported adapters without confusing a missing sample or disconnection', () => {
    const props = { connected: true, temperature: 23, mcuTemperature: null, panelTemperature: null }
    const nordic = renderToStaticMarkup(<TemperatureFact {...props} boardId="nrf52832_priceetag" />)
    expect(nordic).toContain('屏幕 -- / MCU --')
    const cached = renderToStaticMarkup(<TemperatureFact {...props} boardId="aesl22_nrf" mcuTemperature={25} panelTemperature={22.5} />)
    expect(cached).toContain('屏幕 22.5 °C / MCU 25 °C')
    const expired = renderToStaticMarkup(<TemperatureFact {...props} boardId="aesl22_nrf" mcuTemperature={25} panelTemperature={null} />)
    expect(expired).toContain('屏幕 -- / MCU 25 °C')
    expect(expired).not.toContain('未接入')
    const dialog = renderToStaticMarkup(<TemperatureFact {...props} boardId="da14585_aesl" />)
    expect(dialog).toContain(' / MCU 不支持')
    const telink = renderToStaticMarkup(<TemperatureFact {...props} boardId="hs_nowa154" />)
    expect(telink).toContain('屏幕 -- / MCU 不支持')
    const telinkReading = renderToStaticMarkup(<TemperatureFact {...props} boardId="hs_nowa154" mcuTemperature={25} />)
    expect(telinkReading).toContain('MCU 25 °C')
    const unknown = renderToStaticMarkup(<TemperatureFact {...props} boardId="new-unknown-board" />)
    expect(unknown).toContain('屏幕 --')
    expect(unknown).toContain(' / MCU --')
    const offline = renderToStaticMarkup(<TemperatureFact {...props} connected={false} boardId="nrf52832_priceetag" />)
    expect(offline).toContain('屏幕 -- / MCU --')
    const supplied = renderToStaticMarkup(<TemperatureFact {...props} boardId="nrf52832_priceetag" panelTemperature={24} />)
    expect(supplied).toContain('屏幕 24 °C')
  })
})
