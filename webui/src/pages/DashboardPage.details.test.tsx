// @vitest-environment jsdom
import { act } from 'react'
import { createRoot, type Root } from 'react-dom/client'
import { MemoryRouter } from 'react-router-dom'
import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import { DashboardPage } from './DashboardPage'
import { useDeviceStore } from '../store/device'

const mock = vi.hoisted(() => ({ native: vi.fn(), canvas: vi.fn(), refresh: vi.fn() }))
vi.mock('../protocol/client', () => ({ inktagClient: { showNativeDeviceInfo: mock.native, sendCanvas: mock.canvas, refreshDeviceInfo: mock.refresh } }))
vi.mock('../components/TemplatePreview', () => ({ TemplatePreview: () => null }))
let host: HTMLDivElement, root: Root
const button = (label: string) => host.querySelector<HTMLButtonElement>(`button[aria-label="${label}"]`)!
const deviceButton = () => Array.from(host.querySelectorAll('button')).find(b => b.textContent === '设备')!
const render = () => act(async () => root.render(<MemoryRouter><DashboardPage /></MemoryRouter>))
beforeEach(() => {
  vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT', true)
  vi.clearAllMocks(); mock.native.mockResolvedValue(undefined); mock.refresh.mockResolvedValue(true)
  useDeviceStore.getState().resetDevice(); useDeviceStore.getState().clearLogs()
  useDeviceStore.setState({ connection: 'connected', deviceId: 'tag-A' })
  host = document.createElement('div'); document.body.append(host); root = createRoot(host)
})
afterEach(async () => {
  await act(async () => root.unmount()); host.remove()
  useDeviceStore.getState().resetDevice(); vi.useRealTimers(); vi.unstubAllGlobals()
})
it('uses supported native device info even when screen readback failed', async () => {
  useDeviceStore.setState({ screen: null, settingsCapabilities: { ...useDeviceStore.getState().settingsCapabilities, nativeDeviceInfo: true } })
  await render(); expect(deviceButton().disabled).toBe(false)
  await act(async () => deviceButton().click())
  expect(mock.native).toHaveBeenCalledOnce(); expect(mock.canvas).not.toHaveBeenCalled()
})
it('does not invent a host-rendered screen when native display is unavailable', async () => {
  await render(); expect(deviceButton().disabled).toBe(true)
  expect(deviceButton().title).toContain('刷新按钮重读')
})
it('places refresh beside the current device heading and prevents duplicate requests', async () => {
  let finish!: (complete: boolean) => void
  mock.refresh.mockImplementationOnce(() => new Promise(resolve => { finish = resolve }))
  await render()
  expect(button('重新读取设备配置').parentElement?.textContent).toContain('当前设备')
  await act(async () => { button('重新读取设备配置').click(); button('重新读取设备配置').click() })
  expect(mock.refresh).toHaveBeenCalledOnce(); expect(button('重新读取设备配置').disabled).toBe(true)
  await act(async () => finish(true))
  expect(button('重新读取设备配置').disabled).toBe(false)
  expect(host.querySelector('[role="status"]')?.textContent).toBe('设备配置已更新。')
  expect(host.querySelector('[role="status"]')?.previousElementSibling).toBe(button('重新读取设备配置'))
})
it('clears successful readback feedback after five seconds', async () => {
  vi.useFakeTimers()
  await render()
  await act(async () => button('重新读取设备配置').click())
  expect(host.querySelector('[role="status"]')?.textContent).toBe('设备配置已更新。')
  await act(async () => vi.advanceTimersByTime(4999))
  expect(host.querySelector('[role="status"]')).not.toBeNull()
  await act(async () => vi.advanceTimersByTime(1))
  expect(host.querySelector('[role="status"]')).toBeNull()
})
it('keeps failed readback feedback until a retry or device change', async () => {
  vi.useFakeTimers()
  mock.refresh.mockRejectedValueOnce(new Error('读取超时'))
  await render()
  await act(async () => button('重新读取设备配置').click())
  await act(async () => vi.advanceTimersByTime(5000))
  expect(host.querySelector('[role="status"]')?.textContent).toContain('读取超时')
})
it.each(['partial', 'error'])('permits an explicit retry after %s read failure', async kind => {
  if (kind === 'partial') mock.refresh.mockResolvedValueOnce(false)
  else mock.refresh.mockRejectedValueOnce(new Error('读取超时'))
  await render(); await act(async () => button('重新读取设备配置').click())
  expect(host.querySelector('[role="status"]')?.textContent).toContain('刷新重试')
  expect(button('重新读取设备配置').disabled).toBe(false)
  await act(async () => button('重新读取设备配置').click())
  expect(mock.refresh).toHaveBeenCalledTimes(2)
  expect(host.querySelector('[role="status"]')?.textContent).toBe('设备配置已更新。')
})
it('does not show the previous device result after switching devices', async () => {
  let finish!: (complete: boolean) => void
  mock.refresh.mockImplementationOnce(() => new Promise(resolve => { finish = resolve }))
  await render(); await act(async () => button('重新读取设备配置').click())
  await act(async () => { useDeviceStore.setState({ deviceId: 'tag-B' }); finish(true) })
  expect(host.querySelector('[role="status"]')).toBeNull()
  expect(useDeviceStore.getState().logs).toHaveLength(0)
})
it.each(['disconnected', 'ota'])('disables readback during %s', async mode => {
  useDeviceStore.setState({ connection: mode === 'ota' ? 'connected' : 'idle', action: mode === 'ota' ? 'ota' : 'idle' })
  await render(); expect(button('重新读取设备配置').disabled).toBe(true)
  await act(async () => button('重新读取设备配置').click()); expect(mock.refresh).not.toHaveBeenCalled()
})
