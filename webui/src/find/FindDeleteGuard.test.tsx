// @vitest-environment jsdom
import { act } from 'react'
import { createRoot, type Root } from 'react-dom/client'
import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import { FindDeleteGuard } from './FindDeleteGuard'
import { useDeviceStore } from '../store/device'

const ble = vi.hoisted(() => ({ connect: vi.fn() }))
vi.mock('../protocol/client', () => ({ inktagClient: ble }))
let host: HTMLDivElement, root: Root
const remove = vi.fn(), backup = vi.fn(), cancel = vi.fn()
const deviceId = 'activation:0123456789ABCDEF'
const button = (name: string) => Array.from(host.querySelectorAll('button')).find(b => b.textContent === name)!
beforeEach(async () => {
  vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT', true); vi.clearAllMocks()
  useDeviceStore.getState().resetDevice()
  host = document.createElement('div'); document.body.append(host); root = createRoot(host)
  await act(async () => root.render(<FindDeleteGuard name="测试标签" deviceId={deviceId} onDelete={remove} onBackup={backup} onCancel={cancel} />))
})
afterEach(async () => { await act(async () => root.unmount()); host.remove(); vi.unstubAllGlobals() })
it('skipping requires a separate warning and cancellation never deletes', async () => {
  await act(async () => button('跳过并继续').click())
  expect(host.textContent).toContain('无法替换设备正在使用的 Find 身份')
  expect(Array.from(host.querySelectorAll('#confirmation-dialog-message strong.find-delete-warning'), e => e.textContent)).toEqual(['重新完整刷机', '完整刷机可能清除设备数据。'])
  expect(remove).not.toHaveBeenCalled()
  await act(async () => button('取消').click())
  expect(remove).not.toHaveBeenCalled()
  await act(async () => button('跳过并继续').click())
  await act(async () => button('仍然删除').click())
  expect(remove).toHaveBeenCalledTimes(1)
})
it('opening backup alone is insufficient; saved acknowledgement permits ordinary deletion', async () => {
  await act(async () => button('导出备份').click())
  expect(backup).toHaveBeenCalledTimes(1)
  expect(button('跳过并继续')).toBeDefined()
  await act(async () => host.querySelector<HTMLInputElement>('input[type=checkbox]')!.click())
  await act(async () => button('确认删除').click())
  expect(remove).toHaveBeenCalledTimes(1)
  expect(host.textContent).not.toContain('只能通过重新完整刷机')
})
it('a different connected label does not satisfy the connection requirement', async () => {
  ble.connect.mockImplementation(async () => useDeviceStore.setState({ connection: 'connected',
    activation: { ...useDeviceStore.getState().activation, activationId: 'FFFFFFFFFFFFFFFF' } }))
  await act(async () => button('连接设备').click())
  expect(ble.connect).toHaveBeenCalledExactlyOnceWith({ onlyInkTag: true })
  expect(host.querySelector('[role=alert]')?.textContent).toContain('对应的标签')
  expect(button('跳过并继续')).toBeDefined()
  expect(remove).not.toHaveBeenCalled()
})
it('matching connection permits one confirmation and errors preserve the dialog', async () => {
  await act(async () => useDeviceStore.setState({ connection: 'connected', activation: {
    ...useDeviceStore.getState().activation, activationId: '0123456789ABCDEF' } }))
  remove.mockRejectedValueOnce(new Error('storage unavailable'))
  await act(async () => button('确认删除').click())
  expect(host.querySelector('[role=alert]')?.textContent).toContain('storage unavailable')
  expect(remove).toHaveBeenCalledTimes(1)
})

it('uses the top-bar InkTag filter and still waits for explicit deletion after a matching connection', async () => {
  ble.connect.mockImplementation(async () => useDeviceStore.setState({ connection: 'connected',
    activation: { ...useDeviceStore.getState().activation, activationId: '0123456789ABCDEF' } }))
  await act(async () => button('连接设备').click())
  expect(ble.connect).toHaveBeenCalledExactlyOnceWith({ onlyInkTag: true })
  expect(button('对应设备已连接').disabled).toBe(true)
  expect(button('确认删除')).toBeDefined()
  expect(remove).not.toHaveBeenCalled()
})
