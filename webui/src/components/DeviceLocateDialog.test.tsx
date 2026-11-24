// @vitest-environment jsdom
import { act } from 'react'
import { createRoot, type Root } from 'react-dom/client'
import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import { DeviceLocateDialog } from './DeviceLocateDialog'
import { useDeviceStore } from '../store/device'

const client = vi.hoisted(() => ({ locateDevice: vi.fn(), stopLocateDevice: vi.fn() }))
vi.mock('../protocol/client', () => ({ inktagClient: client }))
let host: HTMLDivElement, root: Root
const close = vi.fn()
beforeEach(async () => {
  vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT', true)
  vi.resetAllMocks()
  useDeviceStore.getState().resetDevice()
  useDeviceStore.setState({ connection: 'connected', capabilities: {
    ...useDeviceStore.getState().capabilities, led: 'rgb', auxReserved: true,
  } })
  host = document.createElement('div'); document.body.append(host); root = createRoot(host)
  await act(async () => root.render(<DeviceLocateDialog onClose={close} />))
})
afterEach(async () => {
  await act(async () => root.unmount())
  host.remove(); vi.unstubAllGlobals()
})
it('keeps the light locator usable without a buzzer entry', async () => {
  expect(host.textContent).not.toContain('蜂鸣')
  const start = [...host.querySelectorAll('button')].find(node => node.textContent === '开始寻找')!
  await act(async () => start.click())
  expect(client.locateDevice).toHaveBeenCalledTimes(1)
  const stop = [...host.querySelectorAll('button')].find(node => node.textContent === '停止并关闭')!
  await act(async () => stop.click())
  expect(client.stopLocateDevice).toHaveBeenCalledTimes(1)
  expect(close).toHaveBeenCalledTimes(1)
})
it('does not start a hidden buzzer-only locator advertised by older firmware', async () => {
  await act(async () => useDeviceStore.setState({ capabilities: {
    ...useDeviceStore.getState().capabilities, led: 'none', auxReserved: true,
  } }))
  const start = [...host.querySelectorAll('button')].find(node => node.textContent === '开始寻找')!
  expect(start.disabled).toBe(true)
  await act(async () => start.click())
  expect(client.locateDevice).not.toHaveBeenCalled()
})

