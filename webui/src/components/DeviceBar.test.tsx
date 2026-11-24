// @vitest-environment jsdom
import { act } from 'react'
import { createRoot, type Root } from 'react-dom/client'
import { MemoryRouter } from 'react-router-dom'
import { beforeEach, afterEach, expect, it, vi } from 'vitest'
import { DeviceBar } from './DeviceBar'
import { useDeviceStore } from '../store/device'

const records = vi.hoisted(() => new Map<string, Record<string, unknown>>())
const save = vi.hoisted(() => vi.fn(async (id: string, patch: Record<string, unknown>) => {
  records.set(id, {id, ...records.get(id), ...patch})
}))
const known = vi.hoisted(() => vi.fn(async () => [{id: 'one', name: 'IKT_OM_63CF'}]))
const connect = vi.hoisted(() => vi.fn(async (_options: Record<string, unknown>) => {}))
const disconnect = vi.hoisted(() => vi.fn())
vi.mock('../data/db', () => ({db: {devicePreferences: {toArray: async () => [...records.values()]}}}))
vi.mock('../data/devicePreferences', () => ({updateDevicePreference: save}))
vi.mock('../protocol/client', () => ({inktagClient: {
  canRestoreKnownDevices: () => true,
  getKnownDevices: known,
  getLastConnectedDevice: () => null,
  connect,
  disconnect,
}}))

let host: HTMLDivElement, root: Root
const openHistory = async () => { await act(async () => host.querySelector<HTMLButtonElement>('.connection-menu-toggle')!.click()) }
beforeEach(async () => {
  vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT', true)
  vi.stubGlobal('navigator', {bluetooth: {}})
  records.clear(); save.mockClear(); connect.mockClear(); disconnect.mockClear(); known.mockResolvedValue([{id: 'one', name: 'IKT_OM_63CF'}]); useDeviceStore.getState().resetDevice()
  useDeviceStore.setState({deviceId: 'one', deviceName: 'IKT_OM_63CF', localAlias: 'InkTag 63CF', boardId: 'om6626_zkc21b', connection: 'connected'})
  host = document.createElement('div'); document.body.append(host); root = createRoot(host)
  await act(async () => root.render(<MemoryRouter><DeviceBar /></MemoryRouter>))
})
afterEach(async () => { await act(async () => root.unmount()); host.remove(); useDeviceStore.getState().resetDevice(); vi.unstubAllGlobals() })

it('keeps disconnect named when the responsive layout hides its text', async () => {
  const button = host.querySelector<HTMLButtonElement>('.connection-direct')!
  expect(button.getAttribute('aria-label')).toBe('断开设备')
  expect(button.title).toBe('断开设备')
  expect(button.textContent).toBe('断开')
  await act(async () => button.click())
  expect(disconnect).toHaveBeenCalledOnce()
})

it.each(['connected', 'idle'] as const)('blocks connection controls during OTA while %s and restores them afterwards', async (connection) => {
  await act(async () => useDeviceStore.setState({ connection, action: 'ota' }))
  const direct = host.querySelector<HTMLButtonElement>('.connection-direct')!
  expect(direct.disabled).toBe(true)
  expect(host.querySelector<HTMLButtonElement>('.connection-menu-toggle')!.disabled).toBe(true)
  expect(host.querySelector('[title="停止当前操作"]')).toBeNull()
  await act(async () => direct.click())
  expect(disconnect).not.toHaveBeenCalled()
  expect(connect).not.toHaveBeenCalled()
  expect(host.querySelector<HTMLButtonElement>('.log-shortcut')!.disabled).toBe(false)
  await act(async () => useDeviceStore.getState().setAction('idle'))
  expect(host.querySelector<HTMLButtonElement>('.connection-direct')!.disabled).toBe(false)
})

it.each([
  ['connecting-recovery', '正在连接升级模式'],
  ['recovery-transfer', 'Recovery 升级中'],
  ['reconnecting-app', '正在等待应用返回'],
  ['attesting-app', '正在核对应用'],
] as const)('shows the real OTA phase %s without inventing an App connection', async (phase, label) => {
  await act(async () => {
    useDeviceStore.getState().setAction('ota')
    useDeviceStore.getState().setOtaPhase(phase)
    useDeviceStore.getState().resetDevice(true)
  })
  expect(useDeviceStore.getState().connection).toBe('idle')
  expect(useDeviceStore.getState().otaPhase).toBe(phase)
  expect(host.querySelector('.device-summary strong')?.textContent).toBe(label)
  expect(host.querySelector('.device-metrics')?.textContent).not.toContain('离线')
  expect(host.querySelector('.device-metrics')?.textContent).not.toContain('BLE')
  expect(host.querySelector<HTMLButtonElement>('.connection-direct')!.disabled).toBe(true)
  await act(async () => useDeviceStore.getState().setAction('idle'))
  expect(useDeviceStore.getState().otaPhase).toBeNull()
  expect(host.querySelector('.device-summary strong')?.textContent).toBe('未连接')
})

it('clears a failed or abandoned OTA phase instead of showing stale Recovery activity', async () => {
  await act(async () => {
    useDeviceStore.getState().setAction('ota')
    useDeviceStore.getState().setOtaPhase('recovery-transfer')
    useDeviceStore.getState().failAction('ota', 'CRC mismatch')
  })
  expect(useDeviceStore.getState().otaPhase).toBeNull()
  expect(host.querySelector('.device-summary strong')?.textContent).not.toContain('升级')
  await act(async () => useDeviceStore.getState().setOtaPhase('recovery-transfer'))
  expect(useDeviceStore.getState().otaPhase).toBeNull()
})

it('also blocks a remembered-device menu that was already open when OTA began', async () => {
  await openHistory()
  await act(async () => useDeviceStore.setState({ connection: 'idle', action: 'ota' }))
  const remembered = host.querySelector<HTMLButtonElement>('.known-device-row > button')!
  const scan = host.querySelector<HTMLButtonElement>('.scan-device')!
  expect(remembered.disabled).toBe(true)
  expect(scan.disabled).toBe(true)
  await act(async () => { remembered.click(); scan.click() })
  expect(connect).not.toHaveBeenCalled()
})

it('records the reported device name separately from the exact Bluetooth name', async () => {
  expect(save).toHaveBeenCalledWith('one', expect.objectContaining({localAlias: 'InkTag 63CF', lastConnectedName: 'IKT_OM_63CF'}))
  await openHistory()
  expect(host.querySelector('.known-device-list strong')?.textContent).toBe('IKT_OM_63CF')
  expect(host.querySelector('.known-device-name')?.textContent).toBe('InkTag 63CF')
  expect(host.querySelector('.known-device-list')?.textContent).not.toContain('om6626_zkc21b')
})
it('retains the verified App board when Recovery has no board identity', async () => {
  expect(records.get('one')?.lastConnectedBoardId).toBe('om6626_zkc21b')
  await act(async () => useDeviceStore.setState({ boardId: '', firmwareVersion: 'Recovery' }))
  expect(records.get('one')?.lastConnectedBoardId).toBe('om6626_zkc21b')
  expect(save.mock.lastCall?.[1]).not.toHaveProperty('lastConnectedBoardId')
})
it('updates and clears the saved label without losing the displayed Bluetooth-name fallback', async () => {
  await act(async () => useDeviceStore.getState().setIdentity({localAlias: 'Desk Tag'}))
  expect(records.get('one')?.localAlias).toBe('Desk Tag')
  await openHistory()
  expect(host.querySelector('.known-device-list strong')?.textContent).toBe('IKT_OM_63CF')
  expect(host.querySelector('.known-device-name')?.textContent).toBe('Desk Tag')
  await act(async () => useDeviceStore.getState().setIdentity({localAlias: ''}))
  expect(records.get('one')?.localAlias).toBe('')
  expect(host.querySelector('.known-device-list strong')?.textContent).toBe('IKT_OM_63CF')
  expect(host.querySelector('.known-device-name')?.textContent).toBe('IKT_OM_63CF')
})
it('restores the device name from history after unmount and disconnect', async () => {
  await act(async () => root.unmount())
  useDeviceStore.getState().resetDevice()
  root = createRoot(host)
  await act(async () => root.render(<MemoryRouter><DeviceBar /></MemoryRouter>))
  await openHistory()
  expect(host.querySelector('.known-device-list strong')?.textContent).toBe('IKT_OM_63CF')
  expect(host.querySelector('.known-device-name')?.textContent).toBe('InkTag 63CF')
})
it('keeps an offline history title as the exact Bluetooth selection name', async () => {
  known.mockResolvedValue([])
  await act(async () => useDeviceStore.getState().resetDevice())
  await openHistory()
  expect(host.querySelector('.known-device-list strong')?.textContent).toBe('IKT_OM_63CF')
  expect(host.querySelector('.known-device-name')?.textContent).toBe('InkTag 63CF')
  await act(async () => host.querySelector<HTMLButtonElement>('.known-device-row > button')!.click())
  expect(connect).toHaveBeenCalledExactlyOnceWith({onlyInkTag: true, preferredName: 'IKT_OM_63CF'})
})
it('displays the Bluetooth name when the reported device label is missing, without saving a fabricated label', async () => {
  await act(async () => useDeviceStore.getState().setIdentity({localAlias: '  '}))
  await openHistory()
  expect(host.querySelector('.known-device-list strong')?.textContent).toBe('IKT_OM_63CF')
  expect(host.querySelector('.known-device-name')?.textContent).toBe('IKT_OM_63CF')
  expect(records.get('one')?.localAlias).toBe('  ')
})
it('keeps a reported device label visible when it equals the Bluetooth name', async () => {
  await act(async () => useDeviceStore.getState().setIdentity({localAlias: 'IKT_OM_63CF'}))
  await openHistory()
  expect(host.querySelector('.known-device-list strong')?.textContent).toBe('IKT_OM_63CF')
  expect(host.querySelector('.known-device-name')?.textContent).toBe('IKT_OM_63CF')
  expect(records.get('one')?.localAlias).toBe('IKT_OM_63CF')
})
