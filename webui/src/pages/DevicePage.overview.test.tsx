// @vitest-environment jsdom
import { act } from 'react'
import { createRoot, type Root } from 'react-dom/client'
import { MemoryRouter } from 'react-router-dom'
import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import { useDeviceStore } from '../store/device'
import { DevicePage } from './DevicePage'

vi.mock('../protocol/client', () => ({ inktagClient: { supportsLifecycleV2: () => false } }))
vi.mock('../data/db', () => ({ db: { screenProfiles: { orderBy: () => ({ reverse: () => ({ toArray: async () => [] }) }) } } }))

let host: HTMLDivElement
let root: Root
beforeEach(() => {
  vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT', true)
  useDeviceStore.getState().resetDevice()
  host = document.createElement('div')
  document.body.append(host)
  root = createRoot(host)
})
afterEach(async () => {
  await act(async () => root.unmount())
  host.remove()
  useDeviceStore.getState().resetDevice()
  vi.unstubAllGlobals()
})

it('keeps device and client entries in one settings navigation with the short preference label', async()=>{
  await act(async()=>root.render(<MemoryRouter initialEntries={['/device?tab=preferences']}><DevicePage /></MemoryRouter>))
  const nav=host.querySelector('.sub-nav')!
  expect([...nav.querySelectorAll('button')].map(b=>b.textContent)).toEqual(['概览','显示','板卡','交互','激活','固件','日志','测试','偏好','关于'])
  expect(nav.querySelectorAll('.settings-group-title')).toHaveLength(2)
  expect(host.querySelector('.browser-preferences h2')?.textContent).toBe('偏好')
  expect(host.textContent).not.toContain('偏好设置')
  await act(async()=>nav.querySelectorAll('button')[9].click())
  expect(nav.querySelector('button.active')?.textContent).toBe('关于')
})

it.each([
  ['connected', 'tlsr8359_nebular154', 'TLSR8359'],
  ['connected', 'om6626_zkc42b', 'OM6626'],
  ['idle', 'tlsr8359_nebular154', '--'],
] as const)('shows MCU rather than the product board label (%s / %s)', async (connection, boardId, chip) => {
  useDeviceStore.setState({ connection, boardId })
  await act(async () => root.render(<MemoryRouter initialEntries={['/device']}><DevicePage /></MemoryRouter>))
  const rows = Array.from(host.querySelectorAll('.settings-list > div'))
  expect(rows.find(row => row.querySelector('dt')?.textContent === '主控')?.querySelector('dd')?.textContent).toBe(chip)
  expect(rows.some(row => row.querySelector('dt')?.textContent === '主板')).toBe(false)
  expect(host.textContent).not.toContain(boardId)
  expect(useDeviceStore.getState().boardId).toBe(boardId)
})

it.each(['screen', 'hardware'])('keeps routing in the board tab and embeds BUSY in screen Profile (%s)', async tab => {
  await act(async () => root.render(<MemoryRouter initialEntries={[`/device?tab=${tab}`]}><DevicePage /></MemoryRouter>))
  const headings = Array.from(host.querySelectorAll('h2')).map(node => node.textContent)
  expect(headings.includes('屏幕引脚')).toBe(tab === 'hardware')
  expect(headings.includes('LED 引脚')).toBe(tab === 'hardware')
  expect(headings.includes('实体按键')).toBe(tab === 'hardware')
  expect(headings.includes('屏幕 BUSY 极性')).toBe(false)
  expect(host.querySelector('.screen-profile-busy')).toBeNull()
  if (tab === 'hardware') {
    const fields = [...host.querySelectorAll<HTMLInputElement>('.pin-grid input[type="number"]')]
    expect(fields).toHaveLength(10)
    expect(fields.every(field => field.placeholder === '--' && field.value === '')).toBe(true)
    expect(host.textContent).not.toMatch(/TXD|RXD|读取设备/)
  }
})
