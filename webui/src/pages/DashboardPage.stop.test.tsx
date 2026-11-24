// @vitest-environment jsdom
import { act } from 'react'
import { createRoot, type Root } from 'react-dom/client'
import { MemoryRouter } from 'react-router-dom'
import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import { DashboardPage } from './DashboardPage'
import { useDeviceStore } from '../store/device'

const mock = vi.hoisted(() => ({ stop: vi.fn<() => Promise<void>>() }))
vi.mock('../protocol/client', () => ({ inktagClient: { stop: mock.stop } }))
vi.mock('../components/TemplatePreview', () => ({ TemplatePreview: () => null }))

let host: HTMLDivElement, root: Root
const pause = () => Array.from(host.querySelectorAll('button')).find(b => b.textContent === '暂停' || b.textContent === '停止中')!
const render = () => act(async () => root.render(<MemoryRouter><DashboardPage /></MemoryRouter>))
beforeEach(() => {
  vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT', true)
  mock.stop.mockReset().mockResolvedValue(undefined)
  useDeviceStore.getState().resetDevice()
  useDeviceStore.getState().clearLogs()
  host = document.createElement('div'); document.body.append(host); root = createRoot(host)
})
afterEach(async () => {
  await act(async () => root.unmount())
  host.remove(); useDeviceStore.getState().resetDevice(); vi.unstubAllGlobals()
})

it.each(['idle', 'clock', 'calendar', 'picture', 'slideshow'] as const)(
  'can stop a connected device while browser action is %s', async action => {
    useDeviceStore.setState({ connection: 'connected', deviceId: 'tag-A', action })
    await render()
    expect(pause().disabled).toBe(false)
    await act(async () => pause().click())
    expect(mock.stop).toHaveBeenCalledOnce()
    expect(useDeviceStore.getState().logs.at(-1)).toMatchObject({ level: 'success' })
  })

it.each(['disconnected', 'ota'] as const)('does not issue display CANCEL during %s', async mode => {
  useDeviceStore.setState({ connection: mode === 'ota' ? 'connected' : 'idle', action: mode === 'ota' ? 'ota' : 'idle' })
  await render()
  expect(pause().disabled).toBe(true)
  await act(async () => pause().click())
  expect(mock.stop).not.toHaveBeenCalled()
})

it('sends once while waiting and remains available after confirmation', async () => {
  let finish!: () => void
  mock.stop.mockImplementationOnce(() => new Promise<void>(resolve => { finish = resolve }))
  useDeviceStore.setState({ connection: 'connected', deviceId: 'tag-A', action: 'idle' })
  await render()
  await act(async () => { pause().click(); pause().click() })
  expect(mock.stop).toHaveBeenCalledOnce()
  expect(pause().disabled).toBe(true)
  await act(async () => finish())
  expect(pause().disabled).toBe(false)
})

it('reports rejection and permits a manual retry without claiming success', async () => {
  mock.stop.mockRejectedValueOnce(new Error('设备拒绝取消'))
  useDeviceStore.setState({ connection: 'connected', deviceId: 'tag-A' })
  await render()
  await act(async () => pause().click())
  expect(useDeviceStore.getState().logs.at(-1)).toMatchObject({ level: 'error', message: '停止失败：设备拒绝取消' })
  expect(pause().disabled).toBe(false)
  await act(async () => pause().click())
  expect(mock.stop).toHaveBeenCalledTimes(2)
})

it('does not report the previous device result on a newly selected device', async () => {
  let finish!: () => void
  mock.stop.mockImplementationOnce(() => new Promise<void>(resolve => { finish = resolve }))
  useDeviceStore.setState({ connection: 'connected', deviceId: 'tag-A' })
  await render()
  await act(async () => pause().click())
  await act(async () => { useDeviceStore.setState({ deviceId: 'tag-B' }); finish() })
  expect(useDeviceStore.getState().logs).toHaveLength(0)
})
