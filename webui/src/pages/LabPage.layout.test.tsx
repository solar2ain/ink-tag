// @vitest-environment jsdom
import { act } from 'react'
import { createRoot, type Root } from 'react-dom/client'
import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import { LabPanel } from './LabPage'
import { useDeviceStore } from '../store/device'
import { TestOperationV2 } from '../protocol/generatedCommandsV2'

const client = vi.hoisted(() => ({ sendCanvas: vi.fn(), simulateButton: vi.fn(), testBuzzer: vi.fn(), listTests: vi.fn(), testTimeSet: vi.fn(), testTimeShift: vi.fn() }))
vi.mock('../protocol/client', () => ({ inktagClient: client }))
let host: HTMLDivElement, root: Root
const card = (name: string) => Array.from(host.querySelectorAll<HTMLButtonElement>('.test-card')).find(node => node.querySelector('h2')?.textContent === name)!
beforeEach(async () => {
  vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT', true); vi.clearAllMocks()
  client.listTests.mockResolvedValue([TestOperationV2.TIME_SET, TestOperationV2.TIME_SHIFT, TestOperationV2.LED_CHANNEL, TestOperationV2.RESERVED_03, TestOperationV2.INPUT_SIMULATE])
  useDeviceStore.getState().resetDevice()
  useDeviceStore.setState({ connection: 'connected', protocolVersion: 2, action: 'idle', screen: {schemaVersion: 1, driver: 1, recipe: 1, color: 1, transform: 0, nativeUiScale: 1, width: 152, height: 152, visibleX: 0, visibleY: 0, visibleWidth: 152, visibleHeight: 152} })
  host = document.createElement('div'); document.body.append(host); root = createRoot(host)
  await act(async () => root.render(<LabPanel />))
})
afterEach(async () => { await act(async () => root.unmount()); host.remove(); useDeviceStore.getState().resetDevice(); vi.restoreAllMocks(); vi.unstubAllGlobals() })

it.each([
  ['GR', [TestOperationV2.TIME_SHIFT, TestOperationV2.TIME_SET], true],
  ['OM', [TestOperationV2.TIME_SHIFT, TestOperationV2.TIME_SET], true],
  ['TLSR', [TestOperationV2.TIME_SHIFT, TestOperationV2.TIME_SET], true],
  ['nRF52832 without time operations', [TestOperationV2.INPUT_SIMULATE], false],
  ['nRF52811 without time operations', [TestOperationV2.LED_CHANNEL], false],
  ['DA14585 without tests', [], false],
] as const)('uses the actual %s test list rather than settings schema for time', async (_name, operations, supported) => {
  await act(async () => useDeviceStore.setState({ connection: 'idle' }))
  client.listTests.mockResolvedValueOnce([...operations])
  await act(async () => useDeviceStore.setState({ connection: 'connected', deviceId: _name,
    settingsCapabilities: { ...useDeviceStore.getState().settingsCapabilities, protocolVersion: 1 } }))
  expect(card('日期与时间').disabled).toBe(!supported)
  expect(client.testTimeSet).not.toHaveBeenCalled()
  expect(client.testTimeShift).not.toHaveBeenCalled()
})

it('enables a listed SET independently without enabling unlisted SHIFT', async () => {
  await act(async () => useDeviceStore.setState({ connection: 'idle' }))
  client.listTests.mockResolvedValueOnce([TestOperationV2.TIME_SET])
  await act(async () => useDeviceStore.setState({ connection: 'connected', deviceId: 'set-only',
    settingsCapabilities: { ...useDeviceStore.getState().settingsCapabilities, protocolVersion: 1 } }))
  await act(async () => card('日期与时间').click())
  expect(Array.from(host.querySelectorAll<HTMLButtonElement>('.time-shift-grid button')).every(button => button.disabled)).toBe(true)
  expect(Array.from(host.querySelectorAll<HTMLButtonElement>('footer button')).find(button => button.textContent === '设置并刷新')?.disabled).toBe(false)
})

it('ignores an old device list and permits retry after a read failure', async () => {
  await act(async () => useDeviceStore.setState({ connection: 'idle' }))
  let oldReply!: (operations: number[]) => void
  client.listTests.mockImplementationOnce(() => new Promise(resolve => { oldReply = resolve }))
  await act(async () => useDeviceStore.setState({ connection: 'connected', deviceId: 'old' }))
  client.listTests.mockRejectedValueOnce(new Error('read timeout'))
  await act(async () => useDeviceStore.setState({ deviceId: 'new',
    settingsCapabilities: { ...useDeviceStore.getState().settingsCapabilities, protocolVersion: 1 } }))
  await act(async () => oldReply([TestOperationV2.TIME_SET, TestOperationV2.TIME_SHIFT]))
  expect(card('日期与时间').disabled).toBe(true)
  client.listTests.mockResolvedValueOnce([TestOperationV2.TIME_SET])
  const retry = Array.from(host.querySelectorAll<HTMLButtonElement>('button')).find(button => button.textContent === '重新读取')!
  await act(async () => retry.click())
  expect(card('日期与时间').disabled).toBe(false)
})

it('orders six separate tests and moves diagnostics out of the test list', () => {
  expect(Array.from(host.querySelectorAll('.test-card h2'), node => node.textContent)).toEqual(['屏幕测试', '残影消除', '日期与时间', '轮播测试', '按键模拟', '灯效测试'])
})
it('opens five screen choices with zero commands, and closes without running', async () => {
  await act(async () => card('屏幕测试').click())
  expect(host.querySelectorAll('input[type=radio]')).toHaveLength(5)
  expect(client.sendCanvas).not.toHaveBeenCalled()
  const close = Array.from(host.querySelectorAll<HTMLButtonElement>('[role=dialog] button')).find(node => node.textContent === '关闭')!
  await act(async () => close.click())
  expect(host.querySelector('[role=dialog]')).toBeNull()
  expect(client.sendCanvas).not.toHaveBeenCalled()
})
it('does not expose a buzzer test even when older firmware advertises it', async () => {
  await act(async () => useDeviceStore.getState().setCapabilities({ auxReserved: true }))
  expect(card('按键模拟').disabled).toBe(true)
  expect(card('蜂鸣器测试')).toBeUndefined()
  expect(client.testBuzzer).not.toHaveBeenCalled()
})

it('keeps offline, OTA and missing-screen gates closed without retired NFC simulation', async () => {
  expect(card('NFC模拟')).toBeUndefined()
  for (const state of [{connection: 'idle' as const}, {connection: 'connected' as const, action: 'ota' as const}, {connection: 'connected' as const, action: 'idle' as const, screen: null}]) {
    await act(async () => useDeviceStore.setState(state))
    expect(card('屏幕测试').disabled).toBe(true)
  }
})
it('distinguishes an offline device from busy, missing-screen and unsupported tests', async () => {
  await act(async () => useDeviceStore.setState({connection: 'idle'}))
  for (const node of host.querySelectorAll<HTMLButtonElement>('.test-card')) {
    expect(node.disabled).toBe(true)
    expect(node.title).toBe('请先连接设备')
    expect(node.getAttribute('aria-label')).toContain('请先连接设备')
  }
  await act(async () => useDeviceStore.setState({connection: 'connected', action: 'ota'}))
  expect(card('按键模拟').title).toBe('设备操作进行中，请稍后')
  await act(async () => useDeviceStore.setState({action: 'idle', screen: null}))
  expect(card('屏幕测试').title).toBe('请先读取或配置屏幕')
  expect(card('NFC模拟')).toBeUndefined()
  expect(card('按键模拟').title).toBe('当前设备不支持此测试')
  expect(client.sendCanvas).not.toHaveBeenCalled()
  expect(client.simulateButton).not.toHaveBeenCalled()
  expect(client.testBuzzer).not.toHaveBeenCalled()
})
it('warns that date tests change device time and require restoring it afterwards', async () => {
  await act(async () => useDeviceStore.setState({settingsCapabilities: {...useDeviceStore.getState().settingsCapabilities, protocolVersion: 1}}))
  await act(async () => card('日期与时间').click())
  expect(host.querySelector('[role=dialog]')?.textContent).toContain('会修改设备时间')
  expect(host.querySelector('[role=dialog]')?.textContent).toContain('测试后请重新校时')
})
it('sends only the selected grid after explicit start and prevents duplicate submissions', async () => {
  const context = {fillRect: vi.fn(), beginPath: vi.fn(), moveTo: vi.fn(), lineTo: vi.fn(), stroke: vi.fn()}
  vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(context as unknown as CanvasRenderingContext2D)
  let finish!: () => void
  client.sendCanvas.mockImplementation(() => new Promise<void>(resolve => {finish = resolve}))
  await act(async () => card('屏幕测试').click())
  await act(async () => host.querySelector<HTMLInputElement>('input[value=grid]')!.click())
  const start = Array.from(host.querySelectorAll<HTMLButtonElement>('[role=dialog] button')).find(node => node.textContent === '开始测试')!
  await act(async () => start.click())
  expect(client.sendCanvas).toHaveBeenCalledTimes(1)
  const [canvas, name] = client.sendCanvas.mock.calls[0]
  expect(name).toBe('test_grid'); expect([canvas.width, canvas.height]).toEqual([152, 152])
  expect(start.disabled).toBe(true)
  await act(async () => start.click()); expect(client.sendCanvas).toHaveBeenCalledTimes(1)
  await act(async () => finish())
  expect(start.disabled).toBe(false)
})
