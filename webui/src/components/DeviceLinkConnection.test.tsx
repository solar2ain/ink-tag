// @vitest-environment jsdom
import { act, StrictMode } from 'react'
import { createRoot, type Root } from 'react-dom/client'
import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import { DeviceLinkConnection } from './DeviceLinkConnection'
import { useDeviceStore } from '../store/device'

const known = vi.hoisted(() => vi.fn<() => Promise<Array<{ id: string; name: string }>>>())
const connectKnown = vi.hoisted(() => vi.fn(async (_device: unknown) => {}))
const connect = vi.hoisted(() => vi.fn(async (_options: unknown) => {}))
vi.mock('../protocol/client', () => ({ inktagClient: { getKnownDevices: known, connectKnownDevice: connectKnown, connect } }))
let host: HTMLDivElement, root: Root
beforeEach(() => {
  vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT', true)
  vi.stubGlobal('navigator', { bluetooth: {} })
  window.history.replaceState(null, '', '/?ble=IKT_TL_1234')
  useDeviceStore.getState().resetDevice()
  known.mockReset().mockResolvedValue([]); connect.mockClear(); connectKnown.mockReset().mockImplementation(async () => {
    useDeviceStore.setState({ connection: 'connected' })
  })
  host = document.createElement('div'); document.body.append(host); root = createRoot(host)
})
afterEach(async () => {
  await act(async () => root.unmount()); host.remove()
  window.history.replaceState(null, '', '/'); useDeviceStore.getState().resetDevice(); vi.unstubAllGlobals()
})
const render = () => act(async () => root.render(<StrictMode><DeviceLinkConnection /></StrictMode>))
const dialog = () => host.querySelector('[role="dialog"]')

it('does not query or connect on a normal visit', async () => {
  window.history.replaceState(null, '', '/'); await render()
  expect(known).not.toHaveBeenCalled(); expect(dialog()).toBeNull()
})
it('automatically connects exactly one authorized exact-name device once in StrictMode', async () => {
  const device = { id: 'exact', name: 'IKT_TL_1234' }
  known.mockResolvedValue([{ id: 'other', name: 'IKT_TL_12345' }, device])
  await render()
  expect(known).toHaveBeenCalledTimes(1)
  expect(connectKnown).toHaveBeenCalledExactlyOnceWith(device)
  expect(connect).not.toHaveBeenCalled(); expect(dialog()).toBeNull()
})
it.each(['absent', 'ambiguous', 'lookup-error', 'connection-error'])('opens a chooser prompt after %s without an unprompted native chooser', async reason => {
  if (reason === 'ambiguous') known.mockResolvedValue([{ id: 'a', name: 'IKT_TL_1234' }, { id: 'b', name: 'IKT_TL_1234' }])
  if (reason === 'lookup-error') known.mockRejectedValue(new Error('permission lookup failed'))
  if (reason === 'connection-error') {
    known.mockResolvedValue([{ id: 'one', name: 'IKT_TL_1234' }])
    connectKnown.mockImplementation(async () => { useDeviceStore.setState({ connection: 'error' }) })
  }
  await render()
  expect(dialog()?.textContent).toContain('IKT_TL_1234')
  expect(connect).not.toHaveBeenCalled()
  await act(async () => host.querySelector<HTMLButtonElement>('footer .primary')!.click())
  expect(connect).toHaveBeenCalledExactlyOnceWith({ onlyInkTag: true, preferredName: 'IKT_TL_1234' })
  expect(dialog()).toBeNull()
})
it('does not interrupt an existing connection', async () => {
  useDeviceStore.setState({ connection: 'connected' }); await render()
  expect(known).not.toHaveBeenCalled(); expect(dialog()).toBeNull()
})
it('does not replace a manual selection started during permission lookup', async () => {
  let resolve!: (value: Array<{ id: string; name: string }>) => void
  known.mockReturnValue(new Promise(done => { resolve = done }))
  await render()
  await act(async () => { useDeviceStore.setState({ connection: 'selecting' }); resolve([{ id: 'a', name: 'IKT_TL_1234' }]) })
  expect(connectKnown).not.toHaveBeenCalled(); expect(dialog()).toBeNull()
})
it('does not reopen or retry after explicit cancellation/disconnection', async () => {
  known.mockResolvedValue([{ id: 'a', name: 'IKT_TL_1234' }])
  connectKnown.mockImplementation(async () => { useDeviceStore.setState({ connection: 'idle' }) })
  await render()
  await act(async () => useDeviceStore.setState({ connection: 'error' }))
  expect(connectKnown).toHaveBeenCalledTimes(1); expect(dialog()).toBeNull()
})
it('lets the user dismiss the fallback without reconnecting', async () => {
  await render()
  await act(async () => dialog()!.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true })))
  await act(async () => useDeviceStore.setState({ connection: 'idle' }))
  expect(dialog()).toBeNull(); expect(connect).not.toHaveBeenCalled()
})
it('shows an unsupported-browser explanation without calling Bluetooth', async () => {
  vi.stubGlobal('navigator', {}); await render()
  expect(dialog()?.textContent).toContain('不支持 Web Bluetooth')
  expect(host.querySelector<HTMLButtonElement>('footer .primary')!.disabled).toBe(true)
  expect(known).not.toHaveBeenCalled()
})
